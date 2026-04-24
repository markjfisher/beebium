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

#ifdef BEEBIUM_HAS_BONJOUR_BROWSE

#include <beebium/discovery/Browser.hpp>

#include <dns_sd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace beebium::discovery {

/// macOS Bonjour implementation of Browser using dns_sd.h.
///
/// The DNS-SD discovery flow is three-stage: DNSServiceBrowse finds
/// service instances, DNSServiceResolve resolves an instance to a
/// hostname + port + TXT record, and DNSServiceGetAddrInfo turns the
/// hostname into IPv4/IPv6 addresses. We chain these per instance
/// and only fire on_added once we have a complete DiscoveredService.
///
/// Threading: a single background thread drives DNSServiceProcessResult
/// for the browse ref. The resolve / getaddrinfo refs are processed
/// inline within the browse callback chain via shared_ptr-managed
/// helpers; this keeps the threading footprint to one thread (the
/// callbacks fire on it). All consumer state is taken under
/// mutex_; user callbacks are invoked outside the lock so consumer
/// code can call back into the browser without deadlocking.
class BonjourBrowser final : public Browser {
public:
    BonjourBrowser() = default;

    ~BonjourBrowser() override { stop(); }

    bool start(const std::string& service_type,
               BrowserCallbacks callbacks) override {
        stop();

        {
            std::lock_guard lock(mutex_);
            callbacks_ = std::move(callbacks);
            instances_.clear();
        }

        DNSServiceErrorType err = DNSServiceBrowse(
            &browse_ref_,
            0,                                // flags
            kDNSServiceInterfaceIndexAny,
            service_type.c_str(),
            nullptr,                          // domain (default = .local)
            browse_callback,
            this);

        if (err != kDNSServiceErr_NoError) {
            browse_ref_ = nullptr;
            return false;
        }

        running_ = true;
        browsing_ = true;
        event_thread_ = std::thread([this] { event_loop(); });
        return true;
    }

    void stop() override {
        running_ = false;

        if (browse_ref_) {
            DNSServiceRefDeallocate(browse_ref_);
            browse_ref_ = nullptr;
        }

        if (event_thread_.joinable()) {
            event_thread_.join();
        }

        // Tear down any pending resolve/address refs.
        std::lock_guard lock(mutex_);
        for (auto& [name, inst] : instances_) {
            if (inst.resolve_ref) {
                DNSServiceRefDeallocate(inst.resolve_ref);
                inst.resolve_ref = nullptr;
            }
            if (inst.addr_ref) {
                DNSServiceRefDeallocate(inst.addr_ref);
                inst.addr_ref = nullptr;
            }
        }
        instances_.clear();
        browsing_ = false;
    }

    BrowserState state() const override {
        return BrowserState{
            .available = true,
            .browsing = browsing_.load(),
        };
    }

private:
    // Per-instance state we accumulate as the resolve / addrinfo
    // callbacks fire. We only fire on_added when we've seen at least
    // one address resolution.
    struct Instance {
        DNSServiceRef resolve_ref = nullptr;
        DNSServiceRef addr_ref = nullptr;
        DiscoveredService service;
        bool announced = false;
    };

    DNSServiceRef browse_ref_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> browsing_{false};
    std::thread event_thread_;

    mutable std::mutex mutex_;
    BrowserCallbacks callbacks_;
    std::map<std::string, Instance> instances_;

    void event_loop() {
        // Drive both the browse ref and any resolve/addr refs that
        // were attached to it. We use select() across the union of
        // file descriptors so all in-flight DNS-SD operations make
        // progress on this single thread.
        while (running_) {
            fd_set readfds;
            FD_ZERO(&readfds);
            int max_fd = -1;

            if (browse_ref_) {
                int fd = DNSServiceRefSockFD(browse_ref_);
                if (fd >= 0) {
                    FD_SET(fd, &readfds);
                    if (fd > max_fd) max_fd = fd;
                }
            }

            // Snapshot resolve/addr fds under lock so we don't race
            // with stop()'s teardown.
            std::vector<std::pair<DNSServiceRef, int>> aux_refs;
            {
                std::lock_guard lock(mutex_);
                for (auto& [name, inst] : instances_) {
                    if (inst.resolve_ref) {
                        int fd = DNSServiceRefSockFD(inst.resolve_ref);
                        if (fd >= 0) {
                            FD_SET(fd, &readfds);
                            if (fd > max_fd) max_fd = fd;
                            aux_refs.emplace_back(inst.resolve_ref, fd);
                        }
                    }
                    if (inst.addr_ref) {
                        int fd = DNSServiceRefSockFD(inst.addr_ref);
                        if (fd >= 0) {
                            FD_SET(fd, &readfds);
                            if (fd > max_fd) max_fd = fd;
                            aux_refs.emplace_back(inst.addr_ref, fd);
                        }
                    }
                }
            }

            if (max_fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100 * 1000;
            int n = ::select(max_fd + 1, &readfds, nullptr, nullptr, &tv);
            if (!running_) break;
            if (n <= 0) continue;

            if (browse_ref_ &&
                FD_ISSET(DNSServiceRefSockFD(browse_ref_), &readfds)) {
                DNSServiceProcessResult(browse_ref_);
                if (!running_) break;
            }
            for (auto& [ref, fd] : aux_refs) {
                if (FD_ISSET(fd, &readfds)) {
                    // The ref may have been deallocated since the
                    // snapshot if a remove fired; that's OK -- we
                    // hold the ref pointer copy and DNS-SD is robust
                    // against processing on a still-valid descriptor
                    // we can confirm below.
                    DNSServiceProcessResult(ref);
                    if (!running_) break;
                }
            }
        }
    }

    static void browse_callback(DNSServiceRef /*sdRef*/,
                                DNSServiceFlags flags,
                                uint32_t interfaceIndex,
                                DNSServiceErrorType errorCode,
                                const char* serviceName,
                                const char* regtype,
                                const char* replyDomain,
                                void* context) {
        auto* self = static_cast<BonjourBrowser*>(context);
        if (errorCode != kDNSServiceErr_NoError || !serviceName) return;

        if (flags & kDNSServiceFlagsAdd) {
            self->begin_resolve(serviceName, regtype, replyDomain,
                                interfaceIndex);
        } else {
            self->handle_remove(serviceName);
        }
    }

    void begin_resolve(const std::string& name, const char* regtype,
                       const char* domain, uint32_t interfaceIndex) {
        DNSServiceRef ref = nullptr;
        DNSServiceErrorType err = DNSServiceResolve(
            &ref,
            0,
            interfaceIndex,
            name.c_str(),
            regtype,
            domain,
            resolve_callback,
            this);
        if (err != kDNSServiceErr_NoError) return;

        std::lock_guard lock(mutex_);
        Instance& inst = instances_[name];
        if (inst.resolve_ref) {
            // Replace the previous in-flight resolver.
            DNSServiceRefDeallocate(inst.resolve_ref);
        }
        inst.resolve_ref = ref;
        inst.service.instance_name = name;
    }

    void handle_remove(const std::string& name) {
        BrowserCallbacks cbs;
        bool announced = false;
        {
            std::lock_guard lock(mutex_);
            auto it = instances_.find(name);
            if (it == instances_.end()) return;
            announced = it->second.announced;
            if (it->second.resolve_ref) {
                DNSServiceRefDeallocate(it->second.resolve_ref);
            }
            if (it->second.addr_ref) {
                DNSServiceRefDeallocate(it->second.addr_ref);
            }
            instances_.erase(it);
            cbs = callbacks_;
        }
        if (announced && cbs.on_removed) {
            cbs.on_removed(name);
        }
    }

    static void resolve_callback(DNSServiceRef sdRef,
                                 DNSServiceFlags /*flags*/,
                                 uint32_t interfaceIndex,
                                 DNSServiceErrorType errorCode,
                                 const char* fullname,
                                 const char* hosttarget,
                                 uint16_t port,            // network byte order
                                 uint16_t txtLen,
                                 const unsigned char* txtRecord,
                                 void* context) {
        auto* self = static_cast<BonjourBrowser*>(context);
        if (errorCode != kDNSServiceErr_NoError || !fullname || !hosttarget) {
            return;
        }

        // Extract the instance name from the fullname; DNS-SD
        // fullnames look like "Beebium\0320\0560\046254._aun._udp.local.";
        // the instance-name portion ends at the first ".<service-type>"
        // segment. We split on the first occurrence of "._" since both
        // "_aun._udp" and "_beebium._tcp" begin that way.
        std::string full(fullname);
        std::string name;
        auto pos = full.find("._");
        if (pos != std::string::npos) {
            name = full.substr(0, pos);
            // dns_sd.h emits backslash-escaped octal for special chars
            // (e.g. spaces as "\\032"); collapse those so consumers see
            // the original instance name.
            name = unescape_dns_sd(name);
        } else {
            name = full;
        }

        std::map<std::string, std::string> txt;
        uint16_t count = TXTRecordGetCount(txtLen, txtRecord);
        for (uint16_t i = 0; i < count; ++i) {
            char key[256];
            uint8_t value_len = 0;
            const void* value_ptr = nullptr;
            DNSServiceErrorType e = TXTRecordGetItemAtIndex(
                txtLen, txtRecord, i, sizeof(key), key, &value_len, &value_ptr);
            if (e != kDNSServiceErr_NoError) continue;
            std::string value;
            if (value_ptr && value_len) {
                value.assign(static_cast<const char*>(value_ptr), value_len);
            }
            txt.emplace(std::string(key), std::move(value));
        }

        // Update the per-instance state, then kick off an addrinfo
        // resolution. Replacing the resolve ref tears down this one
        // (we're done with it once the callback returned).
        DNSServiceRef addr_ref = nullptr;
        DNSServiceErrorType ae = DNSServiceGetAddrInfo(
            &addr_ref,
            0,
            interfaceIndex,
            kDNSServiceProtocol_IPv4,
            hosttarget,
            addrinfo_callback,
            self);
        if (ae != kDNSServiceErr_NoError) {
            addr_ref = nullptr;
        }

        std::lock_guard lock(self->mutex_);
        auto it = self->instances_.find(name);
        if (it == self->instances_.end()) return;
        it->second.service.instance_name = name;
        it->second.service.hostname = hosttarget;
        it->second.service.port = ntohs(port);
        it->second.service.txt_records = std::move(txt);
        if (it->second.resolve_ref == sdRef) {
            DNSServiceRefDeallocate(it->second.resolve_ref);
            it->second.resolve_ref = nullptr;
        }
        if (it->second.addr_ref) {
            DNSServiceRefDeallocate(it->second.addr_ref);
        }
        it->second.addr_ref = addr_ref;
    }

    static void addrinfo_callback(DNSServiceRef sdRef,
                                  DNSServiceFlags /*flags*/,
                                  uint32_t /*interfaceIndex*/,
                                  DNSServiceErrorType errorCode,
                                  const char* /*hostname*/,
                                  const sockaddr* address,
                                  uint32_t /*ttl*/,
                                  void* context) {
        auto* self = static_cast<BonjourBrowser*>(context);
        if (errorCode != kDNSServiceErr_NoError || !address) return;
        if (address->sa_family != AF_INET) return;
        const auto* sin = reinterpret_cast<const sockaddr_in*>(address);
        uint32_t ipv4 = sin->sin_addr.s_addr;

        DiscoveredService announce_payload;
        BrowserCallbacks cbs;
        {
            std::lock_guard lock(self->mutex_);
            // Match by sdRef: each resolve fires its own getaddrinfo
            // ref, so the ref uniquely identifies the instance --
            // matching by hostname wouldn't, since two announcements
            // on the same machine share the host.
            for (auto& [name, inst] : self->instances_) {
                if (inst.addr_ref == sdRef) {
                    inst.service.ipv4_addr_net_byte_order = ipv4;
                    announce_payload = inst.service;
                    inst.announced = true;
                    break;
                }
            }
            cbs = self->callbacks_;
        }

        if (!announce_payload.instance_name.empty() && cbs.on_added) {
            cbs.on_added(announce_payload);
        }
    }

    static std::string unescape_dns_sd(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ) {
            if (s[i] == '\\' && i + 3 < s.size() &&
                std::isdigit(static_cast<unsigned char>(s[i + 1])) &&
                std::isdigit(static_cast<unsigned char>(s[i + 2])) &&
                std::isdigit(static_cast<unsigned char>(s[i + 3]))) {
                int v = (s[i + 1] - '0') * 100
                      + (s[i + 2] - '0') * 10
                      + (s[i + 3] - '0');
                out.push_back(static_cast<char>(v));
                i += 4;
            } else if (s[i] == '\\' && i + 1 < s.size()) {
                out.push_back(s[i + 1]);
                i += 2;
            } else {
                out.push_back(s[i]);
                ++i;
            }
        }
        return out;
    }
};

std::unique_ptr<Browser> create_browser() {
    return std::make_unique<BonjourBrowser>();
}

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_BONJOUR_BROWSE
