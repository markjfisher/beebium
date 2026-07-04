// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#ifdef BEEBIUM_HAS_WINDOWS_MDNS_BROWSE

#include <beebium/discovery/Browser.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// The DnsService* discovery APIs are Unicode-only (all their strings are
// PWSTR), and the browse callback delivers wide DNS records. Force wide (W)
// type resolution in this TU so PDNS_RECORD is DNS_RECORDW and
// Data.PTR.pNameHost is a wchar_t*, regardless of the project-wide setting.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windns.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#pragma comment(lib, "dnsapi.lib")

namespace beebium::discovery {

namespace {

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                  static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                        static_cast<int>(s.size()), result.data(), len);
    return result;
}

std::string to_utf8(PCWSTR ws) {
    if (ws == nullptr || ws[0] == L'\0') return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1,
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    // len includes the terminating NUL; drop it from the std::string size.
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1,
                        result.data(), len, nullptr, nullptr);
    return result;
}

// Extract the DNS-SD instance label from a full service name of the form
// "InstanceName._service._proto.local". The instance label is everything
// before the first "._" segment (the DNS-SD service-type prefix always
// begins with an underscore). Mirrors WindowsAdvertiser::extract_instance_name.
std::string extract_instance_name(PCWSTR full_name) {
    std::wstring full(full_name ? full_name : L"");
    auto pos = full.find(L"._");
    if (pos != std::wstring::npos) {
        full = full.substr(0, pos);
    }
    return to_utf8(full.c_str());
}

}  // namespace

/// Windows implementation of Browser using windns.h.
///
/// Discovery is two-stage on Win32: DnsServiceBrowse delivers PTR records
/// naming the service instances, and DnsServiceResolve turns one instance
/// into a DNS_SERVICE_INSTANCE carrying hostname, IPv4 address, port and TXT
/// records in a single callback. (This is simpler than the macOS Bonjour path,
/// which needs a separate DNSServiceGetAddrInfo stage -- the Win32 resolve
/// already includes the address.) Requires Windows 10 version 1903 (Build
/// 18362) or later.
///
/// Threading: the dnsapi callbacks fire on OS thread-pool threads, so more
/// than one may run concurrently. All shared state is taken under mutex_;
/// user callbacks are invoked outside the lock so consumer code may call back
/// into the browser (e.g. stop()) without deadlocking. stop() cancels the
/// browse and then drains any in-flight resolves before freeing their storage,
/// so a resolve callback never runs against freed memory.
class WindowsBrowser final : public Browser {
public:
    WindowsBrowser() = default;

    ~WindowsBrowser() override { stop(); }

    bool start(const std::string& service_type,
               BrowserCallbacks callbacks) override {
        stop();

        {
            std::lock_guard lock(mutex_);
            callbacks_ = std::move(callbacks);
            instances_.clear();
        }

        // DnsServiceBrowse wants the fully-qualified service type in the
        // .local scope, e.g. "_aun._udp.local".
        query_name_ = to_wide(service_type) + L".local";

        ZeroMemory(&browse_request_, sizeof(browse_request_));
        browse_request_.Version = DNS_QUERY_REQUEST_VERSION1;
        browse_request_.InterfaceIndex = 0;  // all interfaces
        browse_request_.QueryName = query_name_.c_str();
        browse_request_.pBrowseCallback = browse_callback;
        browse_request_.pQueryContext = this;

        ZeroMemory(&browse_cancel_, sizeof(browse_cancel_));

        running_ = true;
        DNS_STATUS status = DnsServiceBrowse(&browse_request_, &browse_cancel_);
        if (status != DNS_REQUEST_PENDING) {
            running_ = false;
            return false;
        }

        {
            std::lock_guard lock(mutex_);
            browse_pending_ = true;
        }
        browsing_ = true;
        return true;
    }

    void stop() override {
        bool was_browsing = browsing_.exchange(false);
        running_ = false;

        if (was_browsing) {
            // Requests the browse stop. One final browse callback still fires
            // afterwards with status ERROR_CANCELLED (see browse_callback),
            // which clears browse_pending_; already-started resolves keep
            // running until they complete or time out.
            DnsServiceBrowseCancel(&browse_cancel_);
        }

        // Wait for the browse's terminal callback and any in-flight resolves to
        // finish before we tear down the storage they reference. Each is a
        // one-shot that always completes (success, failure, or cancellation)
        // within the mDNS timeout, so this wait is bounded; the deadline is a
        // backstop only.
        std::unique_lock lock(mutex_);
        drained_.wait_for(lock, std::chrono::seconds(10), [this] {
            return pending_resolves_ == 0 && !browse_pending_;
        });
        instances_.clear();
    }

    BrowserState state() const override {
        return BrowserState{
            .available = true,
            .browsing = browsing_.load(),
        };
    }

private:
    // A resolve operation in flight for one instance. The request and cancel
    // structures, and the wide query-name they point at, must outlive the
    // asynchronous DnsServiceResolve call, so they live here (owned by the
    // instances_ map) rather than on the stack.
    struct ResolveOp {
        WindowsBrowser* browser = nullptr;
        std::wstring key;          // full instance name; the instances_ key
        std::wstring query_name;   // stable storage for QueryName (== key)
        DNS_SERVICE_RESOLVE_REQUEST request{};
        DNS_SERVICE_CANCEL cancel{};
    };

    // Per-instance discovery state, keyed by the full DNS-SD instance name as
    // delivered by the browse (e.g. "Peer._aun._udp.local"). op is non-null
    // only while a resolve is in flight; announced_name holds the decoded
    // label we reported via on_added so on_removed can report the same string.
    struct Instance {
        std::unique_ptr<ResolveOp> op;
        std::string announced_name;
        bool announced = false;
    };

    std::wstring query_name_;
    DNS_SERVICE_BROWSE_REQUEST browse_request_{};
    DNS_SERVICE_CANCEL browse_cancel_{};

    std::atomic<bool> running_{false};
    std::atomic<bool> browsing_{false};

    mutable std::mutex mutex_;
    std::condition_variable drained_;
    BrowserCallbacks callbacks_;
    std::map<std::wstring, Instance> instances_;
    int pending_resolves_ = 0;
    // True from a successful DnsServiceBrowse until the browse's terminal
    // callback (status ERROR_CANCELLED, delivered once after
    // DnsServiceBrowseCancel) has run. stop() waits for it so the browse's
    // request/cancel storage and this object outlive that last callback.
    bool browse_pending_ = false;

    static void browse_callback(DWORD status,
                                PVOID context,
                                PDNS_RECORD records) {
        auto* self = static_cast<WindowsBrowser*>(context);
        if (status == ERROR_CANCELLED) {
            // The single terminal callback delivered after
            // DnsServiceBrowseCancel. Release stop()'s wait; touch nothing
            // else (the object may be tearing down).
            {
                std::lock_guard lock(self->mutex_);
                self->browse_pending_ = false;
            }
            self->drained_.notify_all();
            if (records) DnsRecordListFree(records, DnsFreeRecordList);
            return;
        }
        if (!self->running_.load()) {
            if (records) DnsRecordListFree(records, DnsFreeRecordList);
            return;
        }
        if (status == ERROR_SUCCESS) {
            for (PDNS_RECORD rec = records; rec != nullptr; rec = rec->pNext) {
                if (rec->wType != DNS_TYPE_PTR) continue;
                PCWSTR host = rec->Data.PTR.pNameHost;
                if (host == nullptr) continue;
                std::wstring name(host);
                // A TTL of zero is a DNS-SD "goodbye": the instance is going
                // away. Any positive TTL is an announcement we should resolve.
                if (rec->dwTtl == 0) {
                    self->handle_remove(name);
                } else {
                    self->handle_add(name);
                }
            }
        }
        if (records) DnsRecordListFree(records, DnsFreeRecordList);
    }

    void handle_add(const std::wstring& name) {
        {
            std::lock_guard lock(mutex_);
            // Browse re-delivers known instances on every change; only resolve
            // an instance we have not seen yet.
            if (instances_.find(name) != instances_.end()) return;

            auto op = std::make_unique<ResolveOp>();
            op->browser = this;
            op->key = name;
            op->query_name = name;

            op->request.Version = DNS_QUERY_REQUEST_VERSION1;
            op->request.InterfaceIndex = 0;
            op->request.QueryName = const_cast<PWSTR>(op->query_name.c_str());
            op->request.pResolveCompletionCallback = resolve_callback;
            op->request.pQueryContext = op.get();
            ZeroMemory(&op->cancel, sizeof(op->cancel));

            ResolveOp* op_raw = op.get();
            Instance& inst = instances_[name];
            inst.op = std::move(op);

            DNS_STATUS status =
                DnsServiceResolve(&op_raw->request, &op_raw->cancel);
            if (status != DNS_REQUEST_PENDING) {
                // Immediate failure: drop the instance so a later browse
                // callback can retry it. Nothing is in flight to drain.
                instances_.erase(name);
                return;
            }
            ++pending_resolves_;
        }
    }

    void handle_remove(const std::wstring& name) {
        std::string announced_name;
        BrowserCallbacks cbs;
        {
            std::lock_guard lock(mutex_);
            auto it = instances_.find(name);
            if (it == instances_.end()) return;
            // A resolve may still be in flight for this instance; leave it to
            // drain (stop() waits on it) rather than freeing its storage from
            // under the OS. Only announced instances are erased here so the
            // in-flight resolve's callback can still find its entry.
            if (it->second.op) return;
            if (!it->second.announced) {
                instances_.erase(it);
                return;
            }
            announced_name = it->second.announced_name;
            cbs = callbacks_;
            instances_.erase(it);
        }
        if (cbs.on_removed && !announced_name.empty()) {
            cbs.on_removed(announced_name);
        }
    }

    static void resolve_callback(DWORD status,
                                 PVOID context,
                                 PDNS_SERVICE_INSTANCE instance) {
        auto* op = static_cast<ResolveOp*>(context);
        WindowsBrowser* self = op->browser;
        std::wstring key = op->key;

        DiscoveredService payload;
        bool have_payload = false;
        if (status == ERROR_SUCCESS && instance != nullptr) {
            payload.instance_name =
                extract_instance_name(instance->pszInstanceName);
            payload.hostname = to_utf8(instance->pszHostName);
            payload.port = instance->wPort;  // host byte order
            if (instance->ip4Address != nullptr) {
                // IP4_ADDRESS is a DWORD already in network byte order.
                payload.ipv4_addr_net_byte_order =
                    static_cast<uint32_t>(*instance->ip4Address);
            }
            for (DWORD i = 0; i < instance->dwPropertyCount; ++i) {
                std::string k = to_utf8(instance->keys[i]);
                std::string v = to_utf8(instance->values[i]);
                if (!k.empty()) payload.txt_records.emplace(std::move(k),
                                                            std::move(v));
            }
            have_payload = !payload.instance_name.empty();
        }

        BrowserCallbacks cbs;
        {
            std::lock_guard lock(self->mutex_);
            auto it = self->instances_.find(key);
            if (it != self->instances_.end()) {
                if (have_payload) {
                    it->second.announced_name = payload.instance_name;
                    it->second.announced = true;
                    // The resolve is complete; free its storage (the request
                    // and cancel are one-shot and are not touched again).
                    it->second.op.reset();
                } else {
                    // Resolve failed: drop the entry so a subsequent browse
                    // callback re-attempts discovery for this instance.
                    self->instances_.erase(it);
                }
            }
            cbs = self->callbacks_;
            if (--self->pending_resolves_ == 0) {
                self->drained_.notify_all();
            }
        }

        if (have_payload && cbs.on_added) {
            cbs.on_added(payload);
        }
        if (instance != nullptr) {
            DnsServiceFreeInstance(instance);
        }
    }
};

std::unique_ptr<Browser> create_browser() {
    return std::make_unique<WindowsBrowser>();
}

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_WINDOWS_MDNS_BROWSE
