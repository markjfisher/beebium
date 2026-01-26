# Service Advertisement Design

## Overview

Service Advertisement enables emulator cores to announce their presence on the local network, allowing frontends and tools to discover running machines without knowing their addresses in advance. This uses DNS-SD (DNS Service Discovery) over mDNS (multicast DNS), commonly known as Bonjour on macOS and Avahi on Linux.

Without service advertisement:
- Users must manually enter `host:port` to connect
- No visibility into what machines are running on the network
- Scripts must maintain lists of known servers

With service advertisement:
- Frontends can show a "Machines on Network" list
- Connect dialog can offer discovered machines
- Automation tools can discover available cores dynamically
- Zero-configuration networking for common use cases

## Design Principles

1. **Optional by default**: Advertisement is opt-in for TTY-launched servers (privacy), opt-out for GUI-launched (convenience)
2. **Platform-native**: Use system DNS-SD APIs where available rather than cross-platform libraries
3. **Graceful degradation**: Missing mDNS support is not an error; servers still function without advertisement
4. **Rich metadata**: TXT records carry useful information about the machine
5. **Collision handling**: Multiple servers on the same host get unique names

## DNS-SD Service Type

### Service Registration

Service type: `_beebium._tcp`

This follows DNS-SD conventions:
- Underscore prefix indicates a service type
- `_tcp` suffix indicates transport protocol
- Registered service types should be unique (no formal registry for hobby projects)

### Instance Naming

The advertised instance name is the machine's name from its identity (Phase 2: Machine Identity):

```
"Teletext Server"._beebium._tcp.local.
```

If name collision occurs, DNS-SD handles this automatically by appending ` (2)`, ` (3)`, etc.

### Hostname via SRV Record

The hostname where the service runs comes from the SRV record, not the instance name or TXT records:

```
Teletext Server._beebium._tcp.local. SRV 0 0 48875 alice-macbook.local.
```

This separation is intentional:
- **Instance name**: What the machine is called (semantic, from `MachineIdentity.name`)
- **SRV target**: Where the machine is running (locational, from DNS-SD)

Clients combine these for display (e.g., "Teletext Server (alice-macbook)"). The server doesn't need to know or report its own hostname — the client knows how it reached the server, and the SRV record provides this for discovered services.

### TXT Records

TXT records carry machine metadata as key=value pairs:

| Key | Value | Example |
|-----|-------|---------|
| `uuid` | Machine UUID | `550e8400-e29b-41d4-a716-446655440000` |
| `model` | Emulated machine model | `BBC Model B` |
| `provenance` | Launch provenance type | `macos-gui` |
| `version` | Beebium version | `0.4.1` |

TXT record constraints (per RFC 6763):
- Each key=value pair must be < 255 bytes
- Total TXT record should be < 1300 bytes (for UDP)
- Keys are case-insensitive ASCII

### SRV Record

The SRV record provides host and port:

```
BBC Model B._beebium._tcp.local. SRV 0 0 48875 alices-macbook.local.
```

## Server-Side Advertisement

### Command-Line Flags

```
--advertise              Enable mDNS service advertisement at startup
--advertise-name <name>  Override advertised instance name
```

Default behaviour:
- `--advertise` is OFF by default in all cases

Advertisement is always opt-in. GUI frontends can enable it via the New Machine dialog or toggle it at runtime via gRPC.

### Runtime Advertisement Control

Advertisement can be enabled or disabled at runtime via gRPC, allowing users to toggle discoverability without restarting the machine.

```protobuf
// In system.proto

message AdvertisementState {
  bool enabled = 1;
  bool available = 2;      // False if platform has no mDNS support
  string advertised_name = 3;  // Actual name being advertised (may differ due to collision)
}

message SetAdvertisementRequest {
  bool enabled = 1;
}

message SetAdvertisementResponse {
  AdvertisementState state = 1;
}

message GetAdvertisementStateRequest {}

message GetAdvertisementStateResponse {
  AdvertisementState state = 1;
}

// In SystemService
rpc SetAdvertisement(SetAdvertisementRequest) returns (SetAdvertisementResponse);
rpc GetAdvertisementState(GetAdvertisementStateRequest) returns (GetAdvertisementStateResponse);
```

**Behaviour:**

- `SetAdvertisement(enabled=true)`: Start advertising if not already. Returns current state.
- `SetAdvertisement(enabled=false)`: Stop advertising. Returns current state.
- If mDNS is unavailable, `state.available` is false and `state.enabled` remains false regardless of request.
- `advertised_name` reflects the actual registered name (may have ` (2)` suffix if collision occurred).

**Use cases:**

- GUI settings panel with "Advertise on network" toggle
- CLI tool: `beebium-cli advertise --enable` / `--disable`
- Scripts that want to make a headless server discoverable after setup

### Advertiser Interface

```cpp
// src/discovery/include/beebium/discovery/Advertiser.hpp

/// Service advertisement interface.
/// Implementations handle platform-specific mDNS registration.
class Advertiser {
public:
    virtual ~Advertiser() = default;

    struct ServiceInfo {
        std::string instance_name;  // Display name
        uint16_t port;              // gRPC port
        std::map<std::string, std::string> txt_records;
    };

    /// Start advertising the service.
    /// Returns true if advertisement started successfully.
    /// Returns false if mDNS is unavailable (not an error condition).
    virtual bool start(const ServiceInfo& info) = 0;

    /// Stop advertising. Safe to call if not started.
    virtual void stop() = 0;

    /// Check if currently advertising.
    virtual bool is_advertising() const = 0;
};
```

### macOS Implementation (dns_sd.h)

macOS provides `dns_sd.h` (part of mDNSResponder) as a system library:

```cpp
// src/discovery/src/BonjourAdvertiser.cpp

#include <dns_sd.h>

class BonjourAdvertiser : public Advertiser {
public:
    bool start(const ServiceInfo& info) override {
        // Build TXT record
        TXTRecordRef txt_ref;
        TXTRecordCreate(&txt_ref, 0, nullptr);
        for (const auto& [key, value] : info.txt_records) {
            TXTRecordSetValue(&txt_ref, key.c_str(),
                              value.size(), value.c_str());
        }

        // Register service
        DNSServiceErrorType err = DNSServiceRegister(
            &service_ref_,
            0,                          // flags
            kDNSServiceInterfaceIndexAny,
            info.instance_name.c_str(), // name (or nullptr for default)
            "_beebium._tcp",            // service type
            nullptr,                    // domain (default = .local)
            nullptr,                    // host (default = this machine)
            htons(info.port),
            TXTRecordGetLength(&txt_ref),
            TXTRecordGetBytesPtr(&txt_ref),
            register_callback,
            this
        );

        TXTRecordDeallocate(&txt_ref);

        if (err != kDNSServiceErr_NoError) {
            return false;
        }

        // Process events in background thread
        start_event_loop();
        return true;
    }

    void stop() override {
        if (service_ref_) {
            DNSServiceRefDeallocate(service_ref_);
            service_ref_ = nullptr;
        }
        stop_event_loop();
    }

    bool is_advertising() const override {
        return service_ref_ != nullptr && registered_;
    }

private:
    DNSServiceRef service_ref_ = nullptr;
    std::atomic<bool> registered_{false};
    std::thread event_thread_;

    static void register_callback(
        DNSServiceRef sdRef,
        DNSServiceFlags flags,
        DNSServiceErrorType errorCode,
        const char* name,
        const char* regtype,
        const char* domain,
        void* context
    ) {
        auto* self = static_cast<BonjourAdvertiser*>(context);
        if (errorCode == kDNSServiceErr_NoError) {
            self->registered_ = true;
            // name may differ from requested if collision occurred
        }
    }

    void start_event_loop() {
        event_thread_ = std::thread([this] {
            while (service_ref_) {
                DNSServiceProcessResult(service_ref_);
            }
        });
    }

    void stop_event_loop() {
        if (event_thread_.joinable()) {
            event_thread_.join();
        }
    }
};
```

### Linux Implementation (Avahi)

Linux typically uses Avahi for mDNS. The Avahi client library provides similar functionality:

```cpp
// src/discovery/src/AvahiAdvertiser.cpp

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>

class AvahiAdvertiser : public Advertiser {
public:
    bool start(const ServiceInfo& info) override {
        // Create simple poll object
        simple_poll_ = avahi_simple_poll_new();
        if (!simple_poll_) return false;

        // Create client
        int error;
        client_ = avahi_client_new(
            avahi_simple_poll_get(simple_poll_),
            AVAHI_CLIENT_NO_FAIL,
            client_callback,
            this,
            &error
        );
        if (!client_) return false;

        // Create entry group
        group_ = avahi_entry_group_new(client_, entry_group_callback, this);
        if (!group_) return false;

        // Build TXT string list
        AvahiStringList* txt = nullptr;
        for (const auto& [key, value] : info.txt_records) {
            txt = avahi_string_list_add_pair(txt, key.c_str(), value.c_str());
        }

        // Add service
        int ret = avahi_entry_group_add_service_strlst(
            group_,
            AVAHI_IF_UNSPEC,
            AVAHI_PROTO_UNSPEC,
            (AvahiPublishFlags)0,
            info.instance_name.c_str(),
            "_beebium._tcp",
            nullptr,  // domain
            nullptr,  // host
            info.port,
            txt
        );
        avahi_string_list_free(txt);

        if (ret < 0) return false;

        // Commit the entry group
        avahi_entry_group_commit(group_);

        // Run event loop in background
        event_thread_ = std::thread([this] {
            avahi_simple_poll_loop(simple_poll_);
        });

        return true;
    }

    void stop() override {
        if (simple_poll_) {
            avahi_simple_poll_quit(simple_poll_);
        }
        if (event_thread_.joinable()) {
            event_thread_.join();
        }
        if (group_) {
            avahi_entry_group_free(group_);
            group_ = nullptr;
        }
        if (client_) {
            avahi_client_free(client_);
            client_ = nullptr;
        }
        if (simple_poll_) {
            avahi_simple_poll_free(simple_poll_);
            simple_poll_ = nullptr;
        }
    }

    bool is_advertising() const override {
        return group_ && avahi_entry_group_get_state(group_) == AVAHI_ENTRY_GROUP_ESTABLISHED;
    }

private:
    AvahiSimplePoll* simple_poll_ = nullptr;
    AvahiClient* client_ = nullptr;
    AvahiEntryGroup* group_ = nullptr;
    std::thread event_thread_;

    static void client_callback(AvahiClient* c, AvahiClientState state, void* userdata) {
        // Handle client state changes
    }

    static void entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* userdata) {
        // Handle registration state changes
    }
};
```

### Windows Implementation (windns.h)

Windows 10 version 1903+ provides native mDNS support via `windns.h`:

```cpp
// src/discovery/src/WindowsAdvertiser.cpp

#include <windns.h>
#include <ws2tcpip.h>

#pragma comment(lib, "dnsapi.lib")

class WindowsAdvertiser : public Advertiser {
public:
    bool start(const ServiceInfo& info) override {
        info_ = info;

        // Build the service instance name
        // Format: "instance._service._tcp.local"
        std::wstring service_name = to_wide(info.instance_name);
        std::wstring full_name = service_name + L"._beebium._tcp.local";

        // Create DNS_SERVICE_INSTANCE
        service_instance_ = {};
        service_instance_.pszInstanceName = const_cast<PWSTR>(full_name.c_str());
        service_instance_.wPort = info.port;
        service_instance_.wPriority = 0;
        service_instance_.wWeight = 0;

        // Build TXT records as key=value strings
        std::vector<std::wstring> txt_strings;
        std::vector<PWSTR> txt_ptrs;
        for (const auto& [key, value] : info.txt_records) {
            txt_strings.push_back(to_wide(key + "=" + value));
        }
        for (auto& s : txt_strings) {
            txt_ptrs.push_back(const_cast<PWSTR>(s.c_str()));
        }
        service_instance_.dwPropertyCount = static_cast<DWORD>(txt_ptrs.size());
        service_instance_.keys = txt_ptrs.data();

        // Prepare registration request
        DNS_SERVICE_REGISTER_REQUEST request = {};
        request.Version = DNS_QUERY_REQUEST_VERSION1;
        request.InterfaceIndex = 0;  // All interfaces
        request.pServiceInstance = &service_instance_;
        request.pRegisterCompletionCallback = register_callback;
        request.pQueryContext = this;
        request.unicastEnabled = FALSE;  // Use mDNS, not unicast DNS

        DWORD status = DnsServiceRegister(&request, &cancel_);
        if (status != DNS_REQUEST_PENDING) {
            return false;
        }

        return true;
    }

    void stop() override {
        if (cancel_.reserved) {
            DnsServiceDeRegister(&cancel_, nullptr);
            cancel_ = {};
        }
        registered_ = false;
    }

    bool is_advertising() const override {
        return registered_;
    }

private:
    ServiceInfo info_;
    DNS_SERVICE_INSTANCE service_instance_{};
    DNS_SERVICE_CANCEL cancel_{};
    std::atomic<bool> registered_{false};

    static void WINAPI register_callback(
        DWORD status,
        PVOID context,
        PDNS_SERVICE_INSTANCE instance
    ) {
        auto* self = static_cast<WindowsAdvertiser*>(context);
        if (status == ERROR_SUCCESS) {
            self->registered_ = true;
        }
    }

    static std::wstring to_wide(const std::string& s) {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring result(len - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), len);
        return result;
    }
};
```

**Windows API notes:**

- Available since Windows 10 1903 (SDK 10.0.18362.0)
- `DnsServiceRegister` is asynchronous; callback fires on completion
- Set `unicastEnabled = FALSE` to use mDNS (not unicast DNS updates)
- Registration is tied to process lifetime; deregister explicitly on shutdown
- [Known limitation](https://github.com/microsoft/WindowsAppSDK/issues/2543): service type enumeration via `_services._dns-sd._udp.local` doesn't work, but direct service browsing does

### Windows Browser Implementation

```cpp
// src/discovery/src/WindowsBrowser.cpp

#include <windns.h>

class WindowsBrowser : public Browser {
public:
    bool start(BrowseCallback callback) override {
        callback_ = std::move(callback);

        DNS_SERVICE_BROWSE_REQUEST request = {};
        request.Version = DNS_QUERY_REQUEST_VERSION1;
        request.InterfaceIndex = 0;
        request.QueryName = L"_beebium._tcp.local";
        request.pBrowseCallback = browse_callback;
        request.pQueryContext = this;

        DWORD status = DnsServiceBrowse(&request, &cancel_);
        return status == DNS_REQUEST_PENDING;
    }

    void stop() override {
        if (cancel_.reserved) {
            DnsServiceBrowseCancel(&cancel_);
            cancel_ = {};
        }
    }

    std::vector<DiscoveredService> services() const override {
        std::lock_guard lock(mutex_);
        std::vector<DiscoveredService> result;
        for (const auto& [key, service] : services_) {
            result.push_back(service);
        }
        return result;
    }

private:
    BrowseCallback callback_;
    DNS_SERVICE_CANCEL cancel_{};
    mutable std::mutex mutex_;
    std::map<std::string, DiscoveredService> services_;

    static void WINAPI browse_callback(
        DWORD status,
        PVOID context,
        PDNS_RECORD records
    ) {
        auto* self = static_cast<WindowsBrowser*>(context);
        // Parse DNS records, resolve services, invoke callback
        // (implementation details omitted for brevity)
    }
};
```

### Null Implementation

For platforms without mDNS support, or when dependencies are unavailable:

```cpp
// src/discovery/src/NullAdvertiser.cpp

class NullAdvertiser : public Advertiser {
public:
    bool start(const ServiceInfo& info) override {
        // Log that advertisement is not available
        return false;
    }

    void stop() override {}

    bool is_advertising() const override { return false; }
};
```

### Factory Function

```cpp
// src/discovery/include/beebium/discovery/Advertiser.hpp

/// Create platform-appropriate advertiser.
/// Returns NullAdvertiser if no mDNS support available.
std::unique_ptr<Advertiser> create_advertiser();
```

### Integration with Server

```cpp
// In ServerMain.cpp

void run_server(const Options& options) {
    // ... existing setup ...

    // Service advertisement
    std::unique_ptr<Advertiser> advertiser;
    if (options.advertise) {
        advertiser = create_advertiser();
        Advertiser::ServiceInfo service_info{
            .instance_name = machine_identity.display_name(),
            .port = options.port,
            .txt_records = {
                {"uuid", machine_identity.uuid()},
                {"model", machine_identity.model()},
                {"provenance", provenance.type},
                {"version", BEEBIUM_VERSION},
            }
        };

        if (advertiser->start(service_info)) {
            std::cout << "Advertising as: " << service_info.instance_name << std::endl;
        } else {
            std::cout << "mDNS advertisement not available" << std::endl;
        }
    }

    // ... run server ...

    // Cleanup
    if (advertiser) {
        advertiser->stop();
    }
}
```

## Discovery Client Library

### Browser Interface

Clients need to discover advertised services:

```cpp
// src/discovery/include/beebium/discovery/Browser.hpp

/// Represents a discovered service.
struct DiscoveredService {
    std::string instance_name;          // Display name
    std::string host;                   // Resolved hostname or IP
    uint16_t port;                      // gRPC port
    std::map<std::string, std::string> txt_records;

    // Convenience accessors
    std::string uuid() const;
    std::string model() const;
    std::string provenance() const;
    std::string version() const;
};

/// Callback for service discovery events.
using BrowseCallback = std::function<void(
    const DiscoveredService& service,
    bool added  // true = service appeared, false = service disappeared
)>;

/// Service browser interface.
class Browser {
public:
    virtual ~Browser() = default;

    /// Start browsing for services.
    /// Callback invoked on discovery thread for each service event.
    virtual bool start(BrowseCallback callback) = 0;

    /// Stop browsing.
    virtual void stop() = 0;

    /// Get currently known services (snapshot).
    virtual std::vector<DiscoveredService> services() const = 0;
};

/// Create platform-appropriate browser.
std::unique_ptr<Browser> create_browser();
```

### macOS Browser Implementation

```cpp
// src/discovery/src/BonjourBrowser.cpp

class BonjourBrowser : public Browser {
public:
    bool start(BrowseCallback callback) override {
        callback_ = std::move(callback);

        DNSServiceErrorType err = DNSServiceBrowse(
            &browse_ref_,
            0,
            kDNSServiceInterfaceIndexAny,
            "_beebium._tcp",
            nullptr,  // domain
            browse_callback,
            this
        );

        if (err != kDNSServiceErr_NoError) {
            return false;
        }

        event_thread_ = std::thread([this] {
            while (browse_ref_) {
                DNSServiceProcessResult(browse_ref_);
            }
        });

        return true;
    }

    void stop() override {
        if (browse_ref_) {
            DNSServiceRefDeallocate(browse_ref_);
            browse_ref_ = nullptr;
        }
        if (event_thread_.joinable()) {
            event_thread_.join();
        }
    }

    std::vector<DiscoveredService> services() const override {
        std::lock_guard lock(mutex_);
        std::vector<DiscoveredService> result;
        for (const auto& [key, service] : services_) {
            result.push_back(service);
        }
        return result;
    }

private:
    DNSServiceRef browse_ref_ = nullptr;
    BrowseCallback callback_;
    std::thread event_thread_;
    mutable std::mutex mutex_;
    std::map<std::string, DiscoveredService> services_;  // keyed by fullname

    static void browse_callback(
        DNSServiceRef sdRef,
        DNSServiceFlags flags,
        uint32_t interfaceIndex,
        DNSServiceErrorType errorCode,
        const char* serviceName,
        const char* regtype,
        const char* replyDomain,
        void* context
    ) {
        auto* self = static_cast<BonjourBrowser*>(context);
        if (errorCode != kDNSServiceErr_NoError) return;

        bool added = (flags & kDNSServiceFlagsAdd);
        std::string fullname = std::string(serviceName) + "." + regtype + replyDomain;

        if (added) {
            // Need to resolve to get host/port/TXT
            self->resolve_service(serviceName, regtype, replyDomain, interfaceIndex);
        } else {
            std::lock_guard lock(self->mutex_);
            auto it = self->services_.find(fullname);
            if (it != self->services_.end()) {
                DiscoveredService service = it->second;
                self->services_.erase(it);
                if (self->callback_) {
                    self->callback_(service, false);
                }
            }
        }
    }

    void resolve_service(const char* name, const char* regtype,
                         const char* domain, uint32_t interface_index) {
        // Resolve in a separate operation to get host/port/TXT
        // (implementation details omitted for brevity)
    }
};
```

## Python Discovery Module

Python clients can use the `zeroconf` library for DNS-SD:

```python
# clients/python/src/beebium/discovery.py

from dataclasses import dataclass
from typing import Callable, Optional
import threading

from zeroconf import ServiceBrowser, ServiceListener, Zeroconf

SERVICE_TYPE = "_beebium._tcp.local."


@dataclass
class DiscoveredMachine:
    """A machine discovered via mDNS."""
    instance_name: str
    host: str
    port: int
    uuid: str
    model: str
    provenance: str
    version: str

    @property
    def address(self) -> str:
        """Return host:port string."""
        return f"{self.host}:{self.port}"


class MachineDiscovery:
    """
    Discover Beebium emulator cores on the local network.

    Example usage:
        discovery = MachineDiscovery()
        discovery.start()

        # Later...
        machines = discovery.machines
        for m in machines:
            print(f"{m.instance_name} at {m.address}")

        discovery.stop()

    Or with callback:
        def on_change(machine, added):
            if added:
                print(f"Found: {machine.instance_name}")
            else:
                print(f"Lost: {machine.instance_name}")

        discovery = MachineDiscovery(callback=on_change)
        discovery.start()
    """

    def __init__(
        self,
        callback: Optional[Callable[[DiscoveredMachine, bool], None]] = None
    ):
        self._callback = callback
        self._zeroconf: Optional[Zeroconf] = None
        self._browser: Optional[ServiceBrowser] = None
        self._machines: dict[str, DiscoveredMachine] = {}
        self._lock = threading.Lock()

    def start(self) -> None:
        """Start discovering machines."""
        self._zeroconf = Zeroconf()
        listener = _Listener(self)
        self._browser = ServiceBrowser(self._zeroconf, SERVICE_TYPE, listener)

    def stop(self) -> None:
        """Stop discovery and release resources."""
        if self._browser:
            self._browser.cancel()
            self._browser = None
        if self._zeroconf:
            self._zeroconf.close()
            self._zeroconf = None
        with self._lock:
            self._machines.clear()

    @property
    def machines(self) -> list[DiscoveredMachine]:
        """Return list of currently discovered machines."""
        with self._lock:
            return list(self._machines.values())

    def _on_service_added(self, name: str, machine: DiscoveredMachine) -> None:
        with self._lock:
            self._machines[name] = machine
        if self._callback:
            self._callback(machine, True)

    def _on_service_removed(self, name: str) -> None:
        with self._lock:
            machine = self._machines.pop(name, None)
        if machine and self._callback:
            self._callback(machine, False)


class _Listener(ServiceListener):
    def __init__(self, discovery: MachineDiscovery):
        self._discovery = discovery

    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        info = zc.get_service_info(type_, name)
        if info is None:
            return

        # Parse TXT records
        txt = {
            k.decode(): v.decode() if v else ""
            for k, v in info.properties.items()
        }

        # Get host address
        addresses = info.parsed_addresses()
        host = addresses[0] if addresses else info.server

        machine = DiscoveredMachine(
            instance_name=name.removesuffix(f".{type_}"),
            host=host,
            port=info.port,
            uuid=txt.get("uuid", ""),
            model=txt.get("model", ""),
            provenance=txt.get("provenance", ""),
            version=txt.get("version", ""),
        )
        self._discovery._on_service_added(name, machine)

    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        self._discovery._on_service_removed(name)

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        # Re-resolve and update
        self.remove_service(zc, type_, name)
        self.add_service(zc, type_, name)


def browse(timeout: float = 2.0) -> list[DiscoveredMachine]:
    """
    One-shot discovery: browse for machines for a given duration.

    Args:
        timeout: How long to browse, in seconds.

    Returns:
        List of discovered machines.

    Example:
        machines = beebium.discovery.browse(timeout=3.0)
        for m in machines:
            print(f"{m.instance_name} at {m.address}")
    """
    import time

    discovery = MachineDiscovery()
    discovery.start()
    time.sleep(timeout)
    machines = discovery.machines
    discovery.stop()
    return machines
```

### Python Dependencies

Add `zeroconf` to the Python client dependencies:

```toml
# clients/python/pyproject.toml
[project.optional-dependencies]
discovery = ["zeroconf>=0.131.0"]
```

This makes discovery optional; users who don't need it avoid the dependency.

## Swift/macOS Discovery

macOS frontends can use `NetService` (deprecated but still functional) or the newer `NWBrowser`:

```swift
// clients/macos/Beebium/Beebium/Discovery/MachineDiscovery.swift

import Foundation
import Network

@MainActor
class MachineDiscovery: ObservableObject {
    struct DiscoveredMachine: Identifiable, Hashable {
        let id: String  // fullname
        let instanceName: String
        let host: String
        let port: UInt16
        let uuid: String
        let model: String
        let provenance: String
        let version: String

        var address: String { "\(host):\(port)" }
    }

    @Published private(set) var machines: [DiscoveredMachine] = []

    private var browser: NWBrowser?
    private var connections: [NWConnection] = []

    func start() {
        let parameters = NWParameters()
        parameters.includePeerToPeer = true

        browser = NWBrowser(for: .bonjour(type: "_beebium._tcp", domain: nil),
                            using: parameters)

        browser?.browseResultsChangedHandler = { [weak self] results, changes in
            Task { @MainActor in
                self?.handleBrowseResults(results)
            }
        }

        browser?.start(queue: .main)
    }

    func stop() {
        browser?.cancel()
        browser = nil
        connections.forEach { $0.cancel() }
        connections.removeAll()
        machines.removeAll()
    }

    private func handleBrowseResults(_ results: Set<NWBrowser.Result>) {
        // Convert browse results to our model
        // Note: NWBrowser.Result doesn't directly provide TXT records;
        // need to resolve via NWConnection to get full metadata
        // (simplified for illustration)
    }
}
```

## Files to Create

### New Directory: `src/discovery/`

```
src/discovery/
├── CMakeLists.txt
├── include/beebium/discovery/
│   ├── Advertiser.hpp
│   └── Browser.hpp
└── src/
    ├── BonjourAdvertiser.cpp      # macOS (dns_sd.h)
    ├── BonjourBrowser.cpp         # macOS (dns_sd.h)
    ├── AvahiAdvertiser.cpp        # Linux (avahi-client)
    ├── AvahiBrowser.cpp           # Linux (avahi-client)
    ├── WindowsAdvertiser.cpp      # Windows (windns.h)
    ├── WindowsBrowser.cpp         # Windows (windns.h)
    └── NullAdvertiser.cpp         # Fallback
```

### Python Client

- `clients/python/src/beebium/discovery.py` (new)
- `clients/python/pyproject.toml` (add optional dependency)

### macOS Frontend

- `clients/macos/Beebium/Beebium/Discovery/MachineDiscovery.swift` (new)

### Windows Frontend (WinUI)

The Windows desktop client will use the WinRT `Windows.Networking.ServiceDiscovery.Dnssd` namespace, which provides a higher-level API more natural for WinUI/C# development:

- `DnssdServiceWatcher` for discovery
- `DnssdServiceInstance` for registration (if the frontend ever needs to advertise)

This is separate from the C++ core's `windns.h` implementation — the frontend uses WinRT, the server uses Win32.

### Modified Files

- `src/server/ServerMain.hpp` — Add `--advertise`, `--no-advertise`, `--advertise-name` flags
- `src/server/ServerMain.cpp` — Integrate advertiser lifecycle
- `CMakeLists.txt` — Add discovery library, conditional platform support

## CMake Configuration

```cmake
# src/discovery/CMakeLists.txt

add_library(beebium-discovery
    src/NullAdvertiser.cpp
    src/NullBrowser.cpp
)

target_include_directories(beebium-discovery PUBLIC include)

# Platform-specific implementations
if(APPLE)
    target_sources(beebium-discovery PRIVATE
        src/BonjourAdvertiser.cpp
        src/BonjourBrowser.cpp
    )
    target_compile_definitions(beebium-discovery PRIVATE BEEBIUM_HAS_BONJOUR)
elseif(WIN32)
    target_sources(beebium-discovery PRIVATE
        src/WindowsAdvertiser.cpp
        src/WindowsBrowser.cpp
    )
    target_link_libraries(beebium-discovery PRIVATE dnsapi)
    target_compile_definitions(beebium-discovery PRIVATE BEEBIUM_HAS_WINDOWS_MDNS)
elseif(UNIX)
    find_package(PkgConfig)
    pkg_check_modules(AVAHI avahi-client)
    if(AVAHI_FOUND)
        target_sources(beebium-discovery PRIVATE
            src/AvahiAdvertiser.cpp
            src/AvahiBrowser.cpp
        )
        target_link_libraries(beebium-discovery PRIVATE ${AVAHI_LIBRARIES})
        target_include_directories(beebium-discovery PRIVATE ${AVAHI_INCLUDE_DIRS})
        target_compile_definitions(beebium-discovery PRIVATE BEEBIUM_HAS_AVAHI)
    endif()
endif()
```

### Platform API Summary

| Platform | API | Library | Header | Notes |
|----------|-----|---------|--------|-------|
| macOS | dns_sd.h | System (mDNSResponder) | `<dns_sd.h>` | Built-in, no dependencies |
| Linux | Avahi | libavahi-client | `<avahi-client/*.h>` | Optional; falls back to Null if not installed |
| Windows | Win32 DNS-SD | dnsapi.lib | `<windns.h>` | Built-in since Windows 10 1903 |

All three platforms use their native mDNS implementations, avoiding external dependencies like Apple's Bonjour SDK for Windows.

## Testing

### Manual Verification

**Server-side advertisement:**

```bash
# Start server with advertisement
./beebium-server --advertise --port 48875

# In another terminal, verify with dns-sd (macOS)
dns-sd -B _beebium._tcp

# Should show something like:
# Browsing for _beebium._tcp
# DATE: ---Mon 20 Jan 2025---
#  3:14:15.926  ...SYM    Add        2   4 local.         _beebium._tcp.       BBC Model B

# Get detailed info
dns-sd -L "BBC Model B" _beebium._tcp

# Should show TXT records:
# uuid=550e8400-e29b-41d4-a716-446655440000
# model=BBC Model B
# provenance=terminal
# version=0.4.1
```

**Python discovery:**

```python
>>> import beebium.discovery
>>> machines = beebium.discovery.browse(timeout=3.0)
>>> for m in machines:
...     print(f"{m.instance_name}: {m.address} ({m.model})")
BBC Model B: 192.168.1.50:48875 (BBC Model B)
```

### Integration Tests (Python)

```python
# tests/integration/test_discovery.py

import pytest
from beebium.server import ServerProcess
from beebium.discovery import MachineDiscovery, browse


@pytest.fixture
def advertised_server(mos_filepath):
    """Launch a server with advertisement enabled."""
    with ServerProcess.launch(
        mos_filepath=mos_filepath,
        extra_args=["--advertise"]
    ) as server:
        yield server


def test_server_is_discoverable(advertised_server):
    """Server with --advertise appears in discovery."""
    # Allow time for mDNS propagation
    machines = browse(timeout=3.0)

    # Find our server by UUID
    server_uuid = advertised_server.client.system.get_status().machine_uuid
    found = [m for m in machines if m.uuid == server_uuid]

    assert len(found) == 1
    machine = found[0]
    assert machine.port == advertised_server.port
    assert machine.model == "BBC Model B"


def test_server_without_advertise_not_discoverable(mos_filepath):
    """Server without --advertise does not appear in discovery."""
    with ServerProcess.launch(mos_filepath=mos_filepath) as server:
        # No --advertise flag
        machines = browse(timeout=3.0)

        server_uuid = server.client.system.get_status().machine_uuid
        found = [m for m in machines if m.uuid == server_uuid]

        assert len(found) == 0


def test_discovery_callback():
    """MachineDiscovery invokes callback on service events."""
    events = []

    def on_change(machine, added):
        events.append((machine.uuid, added))

    discovery = MachineDiscovery(callback=on_change)
    discovery.start()

    with ServerProcess.launch(
        mos_filepath=mos_filepath,
        extra_args=["--advertise"]
    ) as server:
        import time
        time.sleep(3.0)  # Wait for discovery

        # Should have received an "added" event
        server_uuid = server.client.system.get_status().machine_uuid
        assert any(uuid == server_uuid and added for uuid, added in events)

    # After server stops, should receive "removed" event
    time.sleep(2.0)
    assert any(uuid == server_uuid and not added for uuid, added in events)

    discovery.stop()
```

### Unit Tests (C++)

```cpp
// tests/test_advertiser.cpp

TEST_CASE("Advertiser interface") {
    auto advertiser = create_advertiser();

    // NullAdvertiser returns false for start (no mDNS)
    // Platform advertiser behaviour depends on system
    Advertiser::ServiceInfo info{
        .instance_name = "Test Machine",
        .port = 48875,
        .txt_records = {
            {"uuid", "test-uuid"},
            {"model", "Test Model"},
        }
    };

    bool started = advertiser->start(info);
    // Can't assert true/false - depends on platform mDNS availability

    if (started) {
        REQUIRE(advertiser->is_advertising());
        advertiser->stop();
        REQUIRE_FALSE(advertiser->is_advertising());
    }
}
```

## Edge Cases

### No mDNS Daemon

On systems without mDNS (mDNSResponder/Avahi not running):
- `start()` returns false
- Server continues to function normally
- Log message indicates advertisement unavailable

### Name Collision

Multiple servers with the same display name on the network:
- DNS-SD automatically handles this by appending ` (2)`, ` (3)`, etc.
- The final advertised name may differ from requested name
- Server should log the actual registered name

### Network Changes

If network interfaces change while advertising:
- macOS: mDNSResponder handles re-advertisement automatically
- Linux/Avahi: May need to monitor for AVAHI_CLIENT_S_RUNNING state changes

### Server Restart

If a server crashes and restarts quickly:
- Previous service record may still be cached by clients
- DNS-SD TTL typically 120 seconds
- Clients should handle connection failures gracefully

### IPv4/IPv6

DNS-SD can advertise on both IPv4 and IPv6:
- Use `kDNSServiceInterfaceIndexAny` / `AVAHI_IF_UNSPEC`
- Clients may receive both address families
- Connection logic should prefer IPv6 when available, fall back to IPv4

## Security Considerations

Service advertisement makes servers discoverable to anyone on the local network. This is intentional for the use case (finding emulator cores) but has implications:

1. **No authentication**: Discovery doesn't verify server identity; clients should validate via gRPC connection
2. **Information exposure**: TXT records reveal machine model, version, provenance type
3. **Local network only**: mDNS is link-local; doesn't traverse routers
4. **Opt-in by default**: Terminal users must explicitly enable with `--advertise`

For multi-user networks where privacy is a concern, users should not enable advertisement or should use `--advertise-name` to use a non-identifying name.

## Out of Scope: Authorization

**Authorization and authentication are explicitly out of scope for this phase.**

Service discovery and authorization are orthogonal concerns:

- **Discovery** answers: "What machines exist on this network?"
- **Authorization** answers: "Who can connect and do what?"

Discovery reveals existence; it does not grant access. A client that discovers a service via mDNS still needs to establish a gRPC connection, and that connection is where authorization would be enforced.

This separation allows the two features to be implemented independently:

| Scenario | Discovery | Authorization |
|----------|-----------|---------------|
| Single-user LAN, development | Yes | No |
| Manual connection to secured server | No | Yes |
| Shared/untrusted network | Yes | Yes |

When authorization is implemented (as a future phase), the only coupling point with discovery would be an optional TXT record hint (e.g., `auth=required`) so clients can display a lock icon or prompt for credentials before attempting connection. This is a minor addition that can be made at that time.

See the main [lifecycle-management.md](lifecycle-management.md) for the overall phase roadmap.

## Design Decisions

1. **TXT record updates**: No. TXT records are static. Clients query full status via gRPC after connecting.

2. **Service subtype**: No. DNS-SD subtypes (e.g., `_bbc-b._sub._beebium._tcp`) add complexity for marginal benefit.

3. **Custom domain**: No. Servers advertise on `.local` only.

## Open Questions

1. **Windows API maturity**: The native Windows mDNS API (`windns.h`) has a [known bug](https://github.com/microsoft/WindowsAppSDK/issues/2543) where service type enumeration doesn't work. Direct service browsing (which is what Beebium uses) does work. Monitor this issue and test thoroughly on Windows.
