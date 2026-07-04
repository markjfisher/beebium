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

#ifdef BEEBIUM_HAS_AVAHI_BROWSE

#include <beebium/discovery/Browser.hpp>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/address.h>
#include <avahi-common/malloc.h>
#include <avahi-common/strlst.h>
#include <avahi-common/thread-watch.h>

#include <dlfcn.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace beebium::discovery {

namespace {

// Set BEEBIUM_MDNS_TRACE=1 to log the browse/resolve state machine to stderr.
bool mdns_trace_enabled() {
    static const bool enabled = std::getenv("BEEBIUM_MDNS_TRACE") != nullptr;
    return enabled;
}
#define MDNS_TRACE(...)                                        \
    do {                                                       \
        if (mdns_trace_enabled()) {                            \
            std::fprintf(stderr, "[avahi-browser] " __VA_ARGS__); \
            std::fprintf(stderr, "\n");                        \
        }                                                      \
    } while (0)

/// Function pointers into libavahi-client / libavahi-common, resolved at
/// runtime with dlopen/dlsym rather than link-time -- the same policy as
/// AvahiAdvertiser (keep the .deb's runtime deps to the base C/C++ libraries,
/// degrade to a no-op where Avahi is absent). Member types are pinned to the
/// real prototypes via decltype, so a signature mismatch is a compile error.
struct AvahiApi {
    decltype(&avahi_threaded_poll_new) threaded_poll_new;
    decltype(&avahi_threaded_poll_free) threaded_poll_free;
    decltype(&avahi_threaded_poll_get) threaded_poll_get;
    decltype(&avahi_threaded_poll_start) threaded_poll_start;
    decltype(&avahi_threaded_poll_stop) threaded_poll_stop;
    decltype(&avahi_threaded_poll_quit) threaded_poll_quit;

    decltype(&avahi_client_new) client_new;
    decltype(&avahi_client_free) client_free;

    decltype(&avahi_service_browser_new) service_browser_new;
    decltype(&avahi_service_browser_free) service_browser_free;
    decltype(&avahi_service_resolver_new) service_resolver_new;
    decltype(&avahi_service_resolver_free) service_resolver_free;

    decltype(&avahi_string_list_get_pair) string_list_get_pair;
    decltype(&avahi_string_list_get_next) string_list_get_next;
    decltype(&avahi_free) free;
};

/// Resolve the Avahi symbol table once. Returns nullptr if libavahi-client is
/// not installed or any expected symbol is missing, in which case the browser
/// reports unavailable and no-ops.
const AvahiApi* load_avahi_api() {
    static const AvahiApi* const cached = [] () -> const AvahiApi* {
        void* handle = dlopen("libavahi-client.so.3", RTLD_NOW);
        if (!handle) {
            return nullptr;
        }

        auto* api = new AvahiApi{};
        bool ok = true;

#define BEEBIUM_LOAD_AVAHI_SYM(field, symbol)                                  \
    api->field = reinterpret_cast<decltype(api->field)>(dlsym(handle, symbol)); \
    ok = ok && (api->field != nullptr)

        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_new, "avahi_threaded_poll_new");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_free, "avahi_threaded_poll_free");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_get, "avahi_threaded_poll_get");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_start, "avahi_threaded_poll_start");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_stop, "avahi_threaded_poll_stop");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_quit, "avahi_threaded_poll_quit");
        BEEBIUM_LOAD_AVAHI_SYM(client_new, "avahi_client_new");
        BEEBIUM_LOAD_AVAHI_SYM(client_free, "avahi_client_free");
        BEEBIUM_LOAD_AVAHI_SYM(service_browser_new, "avahi_service_browser_new");
        BEEBIUM_LOAD_AVAHI_SYM(service_browser_free, "avahi_service_browser_free");
        BEEBIUM_LOAD_AVAHI_SYM(service_resolver_new, "avahi_service_resolver_new");
        BEEBIUM_LOAD_AVAHI_SYM(service_resolver_free, "avahi_service_resolver_free");
        BEEBIUM_LOAD_AVAHI_SYM(string_list_get_pair, "avahi_string_list_get_pair");
        BEEBIUM_LOAD_AVAHI_SYM(string_list_get_next, "avahi_string_list_get_next");
        BEEBIUM_LOAD_AVAHI_SYM(free, "avahi_free");

#undef BEEBIUM_LOAD_AVAHI_SYM

        if (!ok) {
            delete api;
            return nullptr;
        }
        return api;
    }();
    return cached;
}

}  // namespace

/// Linux Avahi implementation of Browser using libavahi-client.
///
/// Mirrors AvahiAdvertiser: a threaded poll drives the client's asynchronous
/// state machine, and libavahi is loaded at runtime via dlopen so the browser
/// degrades to a no-op where Avahi is absent. Discovery is two-stage --
/// avahi_service_browser_new reports instances coming and going, and each new
/// instance is resolved with avahi_service_resolver_new to obtain its host,
/// IPv4 address, port and TXT records before on_added fires.
///
/// Threading: the browse and resolve callbacks all run on the Avahi poll
/// thread. stop() stops (and joins) that thread before tearing down, so the
/// instance table is only ever touched by one thread at a time and needs no
/// lock of its own. User callbacks fire on the poll thread (the Browser
/// contract allows this).
class AvahiBrowser final : public Browser {
public:
    AvahiBrowser() : api_(load_avahi_api()) {}

    ~AvahiBrowser() override { stop(); }

    bool start(const std::string& service_type,
               BrowserCallbacks callbacks) override {
        stop();

        if (!api_) {
            return false;  // libavahi-client not installed
        }

        service_type_ = service_type;
        callbacks_ = std::move(callbacks);

        threaded_poll_ = api_->threaded_poll_new();
        if (!threaded_poll_) {
            return false;
        }

        // AVAHI_CLIENT_NO_FAIL waits for the daemon rather than failing if it
        // is not yet running. The service browser is created from
        // client_callback once the client reaches S_RUNNING (avahi_client_new
        // may invoke that callback synchronously before returning).
        int error = 0;
        client_ = api_->client_new(api_->threaded_poll_get(threaded_poll_),
                                   AVAHI_CLIENT_NO_FAIL, client_callback, this,
                                   &error);
        if (!client_) {
            api_->threaded_poll_free(threaded_poll_);
            threaded_poll_ = nullptr;
            return false;
        }

        if (api_->threaded_poll_start(threaded_poll_) < 0) {
            api_->client_free(client_);
            client_ = nullptr;
            api_->threaded_poll_free(threaded_poll_);
            threaded_poll_ = nullptr;
            return false;
        }

        browsing_ = true;
        return true;
    }

    void stop() override {
        if (!api_) {
            return;
        }

        // Stop (and join) the poll thread first so no callback runs while we
        // tear down; after this the instance table is ours alone.
        if (threaded_poll_) {
            api_->threaded_poll_stop(threaded_poll_);
        }
        for (AvahiServiceResolver* r : resolvers_) {
            api_->service_resolver_free(r);
        }
        resolvers_.clear();
        announced_.clear();
        if (browser_) {
            api_->service_browser_free(browser_);
            browser_ = nullptr;
        }
        if (client_) {
            api_->client_free(client_);
            client_ = nullptr;
        }
        if (threaded_poll_) {
            api_->threaded_poll_free(threaded_poll_);
            threaded_poll_ = nullptr;
        }
        browsing_ = false;
    }

    BrowserState state() const override {
        return BrowserState{
            .available = (api_ != nullptr),
            .browsing = browsing_.load(),
        };
    }

private:
    const AvahiApi* api_ = nullptr;
    AvahiThreadedPoll* threaded_poll_ = nullptr;
    AvahiClient* client_ = nullptr;
    AvahiServiceBrowser* browser_ = nullptr;
    std::atomic<bool> browsing_{false};

    std::string service_type_;
    BrowserCallbacks callbacks_;
    // Instance names we have already reported via on_added (so on_removed fires
    // once and duplicate resolves are ignored). A browse delivers each instance
    // once per interface/protocol; we resolve each of those results and keep
    // whichever resolves first.
    std::set<std::string> announced_;
    // Resolvers currently in flight, freed on completion or in stop().
    std::set<AvahiServiceResolver*> resolvers_;

    static void client_callback(AvahiClient* client, AvahiClientState state,
                                void* userdata) {
        auto* self = static_cast<AvahiBrowser*>(userdata);
        // Keep client_ valid even when this fires synchronously from within
        // avahi_client_new (before start() assigns the return value): the
        // browse callback creates resolvers against client_.
        self->client_ = client;
        MDNS_TRACE("client state=%d", static_cast<int>(state));

        switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            if (!self->browser_) {
                self->browser_ = self->api_->service_browser_new(
                    client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
                    self->service_type_.c_str(), nullptr,
                    static_cast<AvahiLookupFlags>(0), browse_callback, self);
                MDNS_TRACE("service_browser_new(%s) -> %p",
                           self->service_type_.c_str(),
                           static_cast<void*>(self->browser_));
                if (!self->browser_) {
                    self->api_->threaded_poll_quit(self->threaded_poll_);
                }
            }
            break;

        case AVAHI_CLIENT_FAILURE:
            MDNS_TRACE("client FAILURE");
            self->api_->threaded_poll_quit(self->threaded_poll_);
            break;

        default:
            break;
        }
    }

    static void browse_callback(AvahiServiceBrowser* /*b*/,
                                AvahiIfIndex interface, AvahiProtocol protocol,
                                AvahiBrowserEvent event, const char* name,
                                const char* type, const char* domain,
                                AvahiLookupResultFlags /*flags*/,
                                void* userdata) {
        auto* self = static_cast<AvahiBrowser*>(userdata);
        if (!name) {
            return;
        }

        MDNS_TRACE("browse event=%d name=%s iface=%d proto=%d",
                   static_cast<int>(event), name, static_cast<int>(interface),
                   static_cast<int>(protocol));
        switch (event) {
        case AVAHI_BROWSER_NEW:
            self->begin_resolve(interface, protocol, name, type, domain);
            break;
        case AVAHI_BROWSER_REMOVE:
            self->handle_remove(name);
            break;
        case AVAHI_BROWSER_FAILURE:
            MDNS_TRACE("browse FAILURE");
            self->api_->threaded_poll_quit(self->threaded_poll_);
            break;
        default:
            // AVAHI_BROWSER_CACHE_EXHAUSTED / _ALL_FOR_NOW: nothing to do.
            break;
        }
    }

    void begin_resolve(AvahiIfIndex interface, AvahiProtocol protocol,
                       const char* name, const char* type, const char* domain) {
        // Already discovered on some interface; no need to resolve again.
        if (announced_.count(name)) {
            return;
        }

        // Resolve this browse result on the interface/protocol it arrived on
        // (the canonical avahi-browse pattern). A browse delivers the instance
        // once per interface, so several resolves may be in flight for one
        // name; whichever succeeds first wins, and a resolve that fails on one
        // interface (e.g. a virtual/bridge interface Avahi saw the announcement
        // on but that cannot answer the address query) does not starve the
        // others. aprotocol UNSPEC accepts whatever address family resolves;
        // the IPv4 address (what AUN needs) is picked out in resolve_callback.
        AvahiServiceResolver* resolver = api_->service_resolver_new(
            client_, interface, protocol, name, type, domain,
            AVAHI_PROTO_UNSPEC, static_cast<AvahiLookupFlags>(0),
            resolve_callback, this);
        MDNS_TRACE("service_resolver_new(%s, iface=%d, proto=%d) -> %p", name,
                   static_cast<int>(interface), static_cast<int>(protocol),
                   static_cast<void*>(resolver));
        if (resolver) {
            resolvers_.insert(resolver);
        }
    }

    void handle_remove(const std::string& name) {
        auto it = announced_.find(name);
        if (it == announced_.end()) {
            return;
        }
        announced_.erase(it);
        if (callbacks_.on_removed) {
            callbacks_.on_removed(name);
        }
    }

    static void resolve_callback(AvahiServiceResolver* r, AvahiIfIndex /*iface*/,
                                 AvahiProtocol /*protocol*/,
                                 AvahiResolverEvent event, const char* name,
                                 const char* /*type*/, const char* /*domain*/,
                                 const char* host_name, const AvahiAddress* a,
                                 uint16_t port, AvahiStringList* txt,
                                 AvahiLookupResultFlags /*flags*/,
                                 void* userdata) {
        auto* self = static_cast<AvahiBrowser*>(userdata);
        if (!name) {
            self->api_->service_resolver_free(r);
            return;
        }

        MDNS_TRACE("resolve event=%d name=%s host=%s port=%u",
                   static_cast<int>(event), name, host_name ? host_name : "(null)",
                   static_cast<unsigned>(port));

        // This resolver has completed (success or failure); free it either way.
        // A failure on one interface must not stop the resolvers still racing on
        // the others, so we do not touch announced_ here on failure.
        self->resolvers_.erase(r);

        if (event != AVAHI_RESOLVER_FOUND) {
            self->api_->service_resolver_free(r);
            return;
        }

        // Report each instance at most once, even though several interfaces may
        // resolve it. announced_.insert(...).second is true only the first time.
        bool first = self->announced_.insert(name).second;
        if (first) {
            DiscoveredService svc;
            svc.instance_name = name;
            svc.hostname = host_name ? host_name : "";
            svc.port = port;  // host byte order
            if (a && a->proto == AVAHI_PROTO_INET) {
                // AvahiIPv4Address.address is a uint32_t in network byte order.
                svc.ipv4_addr_net_byte_order = a->data.ipv4.address;
            }
            for (AvahiStringList* l = txt; l != nullptr;
                 l = self->api_->string_list_get_next(l)) {
                char* key = nullptr;
                char* value = nullptr;
                size_t size = 0;
                if (self->api_->string_list_get_pair(l, &key, &value, &size)
                        == 0) {
                    if (key) {
                        svc.txt_records.emplace(
                            std::string(key),
                            value ? std::string(value, size) : std::string());
                    }
                    if (key) self->api_->free(key);
                    if (value) self->api_->free(value);
                }
            }
            self->api_->service_resolver_free(r);
            if (self->callbacks_.on_added) {
                self->callbacks_.on_added(svc);
            }
            return;
        }

        self->api_->service_resolver_free(r);
    }
};

std::unique_ptr<Browser> create_browser() {
    return std::make_unique<AvahiBrowser>();
}

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_AVAHI_BROWSE
