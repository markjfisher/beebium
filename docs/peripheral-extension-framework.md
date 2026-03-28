# Peripheral Extension Framework

## Overview

The BBC Micro supported diverse third-party peripherals connected via external ports (1 MHz bus, User Port, Analogue Port, RS-423, Tube). Beebium's peripheral extension framework provides a principled architecture for adding these peripherals as pluggable modules -- either compiled into the server executable (built-in) or loaded from shared libraries at startup (plugins).

Two properties of BBC Micro peripherals drive the framework design:

**Multi-port devices.** A single peripheral can connect to multiple interfaces simultaneously. The Voltmace Delta 14B/1 joystick interface connects to both the User Port and the Analogue Port. The framework models this with composition: one extension implementing multiple device callback interfaces.

**Sub-bus topologies.** Some peripherals introduce new buses. An Acorn SCSI Host Adapter plugs into the 1 MHz bus and provides a SCSI bus to which hard drives and LaserDisc players connect. An IEEE-488 adapter provides a GPIB bus. The framework supports this through dynamic extension points: an extension can declare that it provides a new named attachment site for other extensions.

For the SCSI use case that motivated this framework, see [Hard Disc Emulation Comparison](hard-disc-comparison.md).

## Architecture

### Extension Points

Extension points are named attachment sites where extensions can register. Built-in extension points are provided by the machine hardware; dynamic extension points are created by extensions during initialisation.

| Extension Point | Type | Provider |
|----------------|------|----------|
| `1mhz-bus` | Built-in | Machine hardware (FRED/JIM region, 0xFC00-0xFDFF) |
| `user-port` | Built-in | User VIA Port B (future) |
| `analogue-port` | Built-in | ADC (future) |
| `scsi` | Dynamic | Created by SCSI Host Adapter extension (future) |

Which built-in extension points are available depends on the machine variant. A Model A has none (no User VIA, no 1 MHz bus). A Model B has `1mhz-bus`, `user-port`, `analogue-port`, etc. The hardware policy class registers the extension points corresponding to its physical ports.

### Extensions

Each extension is a C++ class that inherits from `PeripheralExtension` and optionally implements device callback interfaces for the ports it uses. Extensions declare their dependencies (`attaches_to`) and what new extension points they create (`provides`).

```cpp
// src/core/include/beebium/extension/PeripheralExtension.hpp

class PeripheralExtension {
public:
    virtual ~PeripheralExtension() = default;

    // Manifest is the single source of truth for metadata.
    void set_manifest(ExtensionManifest manifest);
    const ExtensionManifest& manifest() const;
    virtual std::string_view name() const;         // default reads from manifest
    std::string_view description() const;          // reads from manifest

    // Dependencies and provisions
    virtual std::span<const std::string_view> attaches_to() const = 0;
    virtual std::span<const std::string_view> provides() const = 0;

    // Lifecycle
    virtual void init(ExtensionContext& ctx) = 0;
    virtual void shutdown() = 0;

    // gRPC services (zero or more)
    virtual std::vector<grpc::Service*> grpc_services() { return {}; }
};
```

### Port Handles and Device Callbacks

I/O methods are not on `PeripheralExtension`. Each extension point type defines a **port handle** (owned by the machine) and a **device callback interface** (implemented by the extension). This composition-based design avoids diamond inheritance for multi-port devices.

Currently implemented:

```cpp
// src/core/include/beebium/extension/OneMHzBusPort.hpp

class OneMHzBusPort {
public:
    uint8_t read(uint16_t offset);                    // MemoryMappedDevice interface
    void write(uint16_t offset, uint8_t value);       // MemoryMappedDevice interface
    void claim_addresses(uint16_t base, uint16_t end, // register device for address range
                         OneMHzBusDevice& device);
    void tick();                                       // tick all registered devices
    bool is_claimed(uint16_t offset) const;
};

// src/core/include/beebium/extension/OneMHzBusDevice.hpp

struct OneMHzBusDevice {
    virtual ~OneMHzBusDevice() = default;
    virtual uint8_t read(uint16_t offset) = 0;        // offset relative to 0xFC00
    virtual void write(uint16_t offset, uint8_t value) = 0;
    virtual void tick() {}
};
```

The `offset` parameter in `OneMHzBusDevice` is relative to 0xFC00 (the start of the FRED page), matching what the MemoryMap's Region binding computes. FRED occupies offsets 0x0000-0x00FF; JIM occupies 0x0100-0x01FF. Unclaimed addresses return 0xFF on read (74LS245 transceiver behaviour).

Future port types (UserPort, AnaloguePort) will follow the same pattern: a port handle class and a device callback interface, added without changing existing code.

### Dependency Resolution

The `ExtensionRegistry` collects extensions and resolves their dependency graph via topological sort (Kahn's algorithm) at startup:

1. Built-in extension points (registered by the hardware policy) are always satisfied
2. Extensions whose `attaches_to` dependencies are all satisfied are initialised first
3. After initialisation, any extension points they `provide` become available
4. This repeats until all extensions are initialised, or a cycle/unsatisfied dependency is detected

Extensions are shut down in reverse initialisation order.

### gRPC Services

Extensions can provide gRPC services by returning them from `grpc_services()`. These are collected after `init()` and registered with the gRPC `ServerBuilder` before the server starts.

A core `PeripheralExtensionService` (always present) lets frontends discover loaded extensions via a `ListExtensions` RPC.

### Extension Manifest

Each extension has an `ExtensionManifest` containing its name, description, and library filename. The manifest is the **single source of truth** for extension metadata -- `PeripheralExtension::name()` reads from the manifest rather than being hard-coded.

For plugins, the manifest is read from a `manifest.json` file alongside the shared library:

```json
{
    "name": "test-scratch-ram",
    "description": "8 bytes of scratch RAM at 0xFC50 for framework testing",
    "library": "test-scratch-ram"
}
```

For built-in extensions, the manifest is constructed programmatically in the factory method.

### Built-in vs Plugin

The guiding principle follows the real hardware's internal/external boundary:

- **Internal hardware** (factory-fitted): built into the server executable
- **External hardware** (user-attached): loaded from a plugin

The same source code compiles to both a static library (for built-in use) and a shared library (for plugin use). Plugin loading is opt-in via command-line flags:

- `--extension-dir <path>` -- directory containing extension plugins
- `--extension <name>` -- (repeatable) load a named extension

The `PluginLoader` scans the extension directory for `manifest.json` files without loading any code. Only extensions explicitly named with `--extension` are loaded.

## Developer Guide: Creating a Built-in Extension

This walkthrough uses TestScratchRam (8 bytes of RAM at 0xFC50-0xFC57) as the reference implementation.

### 1. Create the Extension Directory

```
src/extensions/my-extension/
    CMakeLists.txt
    MyExtension.hpp
    MyExtension.cpp
    my_extension.proto         (if providing gRPC services)
    MyExtensionService.hpp     (if providing gRPC services)
    manifest.json              (for plugin builds)
    plugin_entry.cpp           (for plugin builds)
```

Add the subdirectory to `src/extensions/CMakeLists.txt`:

```cmake
add_subdirectory(my-extension)
```

### 2. Define the Extension Class

The extension inherits from `PeripheralExtension` and the device callback interface for each port it uses:

```cpp
// MyExtension.hpp
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/OneMHzBusDevice.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

namespace beebium {

class MyExtension : public PeripheralExtension,
                    public OneMHzBusDevice {
public:
    MyExtension();
    ~MyExtension() override;

    static std::unique_ptr<MyExtension> create();

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"1mhz-bus"};
        return deps;
    }

    std::span<const std::string_view> provides() const override { return {}; }

    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // OneMHzBusDevice
    uint8_t read(uint16_t offset) override;
    void write(uint16_t offset, uint8_t value) override;
};

}  // namespace beebium
```

**Key points:**
- `name()` is inherited from `PeripheralExtension` and reads from the manifest -- do not override it
- `attaches_to()` declares which extension points are needed
- `provides()` declares which new extension points this extension creates (empty for leaf extensions)
- The constructor and destructor must be out-of-line if the class has `unique_ptr` members with incomplete types (pimpl for gRPC service)

### 3. Implement the Extension

```cpp
// MyExtension.cpp
#include "MyExtension.hpp"

namespace beebium {

MyExtension::MyExtension() = default;
MyExtension::~MyExtension() = default;

std::unique_ptr<MyExtension> MyExtension::create() {
    auto ext = std::unique_ptr<MyExtension>(new MyExtension());
    ext->set_manifest(ExtensionManifest{
        "my-extension",
        "Description of my extension",
        "my-extension",
        {}  // empty path for built-in
    });
    return ext;
}

void MyExtension::init(ExtensionContext& ctx) {
    ctx.get<OneMHzBusPort>().claim_addresses(0x60, 0x63, *this);
    // Create gRPC service here if needed
}

void MyExtension::shutdown() {
    // Clean up gRPC service here if needed
}

uint8_t MyExtension::read(uint16_t offset) {
    // Handle read at offset (relative to 0xFC00)
    return 0xFF;
}

void MyExtension::write(uint16_t offset, uint8_t value) {
    // Handle write at offset (relative to 0xFC00)
}

}  // namespace beebium
```

**Key points:**
- Use `create()` factory method to set the manifest programmatically
- In `init()`, claim addresses on the bus port via `ExtensionContext`
- Addresses are offsets relative to 0xFC00, not absolute 16-bit addresses
- `claim_addresses()` throws if the range overlaps with another device

### 4. Add a gRPC Service (Optional)

Define a proto file:

```protobuf
// my_extension.proto
syntax = "proto3";
package beebium;

service MyExtensionService {
    rpc GetStatus(GetMyStatusRequest) returns (GetMyStatusResponse);
}
```

Implement the service:

```cpp
// MyExtensionService.hpp
#include "my_extension.grpc.pb.h"
#include <grpcpp/grpcpp.h>

namespace beebium {

class MyExtensionServiceImpl final : public MyExtensionService::Service {
public:
    explicit MyExtensionServiceImpl(MyExtension& ext) : ext_(ext) {}
    // ... implement RPC methods ...
private:
    MyExtension& ext_;
};

}  // namespace beebium
```

Override `grpc_services()` in the extension to return the service:

```cpp
std::vector<grpc::Service*> MyExtension::grpc_services() {
    if (service_) return {service_.get()};
    return {};
}
```

### 5. Build Configuration

```cmake
# src/extensions/my-extension/CMakeLists.txt

# Compile proto (if providing gRPC services)
beebium_compile_proto(
    TARGET beebium_ext_my_extension
    PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/my_extension.proto
    PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
)

# Static library (built-in)
add_library(beebium_ext_my_extension STATIC
    MyExtension.cpp
    ${beebium_ext_my_extension_PROTO_SRCS}
    ${beebium_ext_my_extension_GRPC_SRCS}
)

target_include_directories(beebium_ext_my_extension PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${beebium_ext_my_extension_OUT_DIR}
)

target_link_libraries(beebium_ext_my_extension PUBLIC
    beebium_core gRPC::grpc++ protobuf::libprotobuf
)
```

### 6. Register as Built-in

In `src/server/include/beebium/server/ServerMain.hpp`, include the header and register the extension alongside other built-ins:

```cpp
#include "MyExtension.hpp"

// In StartSubcommand::invoke(), after registry setup:
extension_registry.register_extension(beebium::MyExtension::create());
```

Link the server executables against `beebium_ext_my_extension` in `src/server/CMakeLists.txt`.

## Developer Guide: Creating a Plugin Extension

A plugin extension uses the same source code as a built-in but adds a shared library target and a plugin entry point.

### 1. Add Plugin Entry Point

```cpp
// plugin_entry.cpp
#include "MyExtension.hpp"
#include <beebium/extension/ExtensionManifest.hpp>

extern "C" {

__attribute__((visibility("default")))
beebium::PeripheralExtension* beebium_create_extension(
        const beebium::ExtensionManifest& manifest) {
    auto* ext = new beebium::MyExtension();
    ext->set_manifest(manifest);
    return ext;
}

}
```

**Key points:**
- The entry point is `extern "C"` for `dlsym` lookup
- It receives the manifest (parsed from `manifest.json` by the `PluginLoader`)
- It returns a raw pointer -- the framework takes ownership via `std::unique_ptr`
- Use `__attribute__((visibility("default")))` to ensure the symbol is exported

### 2. Add Manifest

```json
{
    "name": "my-extension",
    "description": "Description of my extension",
    "library": "my-extension"
}
```

The `library` field is the filename stem; the platform adds `.dylib`, `.so`, or `.dll`.

### 3. Add Shared Library Target

```cmake
# Append to src/extensions/my-extension/CMakeLists.txt

if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_plugin_my_extension SHARED
        MyExtension.cpp
        plugin_entry.cpp
        ${beebium_ext_my_extension_PROTO_SRCS}
        ${beebium_ext_my_extension_GRPC_SRCS}
    )

    target_include_directories(beebium_plugin_my_extension PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${beebium_ext_my_extension_OUT_DIR}
    )

    target_link_libraries(beebium_plugin_my_extension PRIVATE
        beebium_core gRPC::grpc++ protobuf::libprotobuf
    )

    set_target_properties(beebium_plugin_my_extension PROPERTIES
        PREFIX "" OUTPUT_NAME "my-extension"
    )

    add_custom_command(TARGET beebium_plugin_my_extension POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
            $<TARGET_FILE_DIR:beebium_plugin_my_extension>/manifest.json
    )
endif()
```

### 4. Load at Runtime

```bash
beebium-model-b start --extension-dir /path/to/plugins --extension my-extension
```

The `PluginLoader` reads `manifest.json` from subdirectories of `--extension-dir`, then loads only the extensions named by `--extension`. Extensions not named are never loaded.

## Startup Sequence

```
1.  Server starts, parses command-line options
2.  Hardware policy class registers built-in extension points
    (e.g. "1mhz-bus" for Model B; nothing for Model A)
3.  Built-in extensions register with the extension registry
4.  If --extension-dir specified, PluginLoader scans for manifest.json files
5.  For each --extension <name>, the matching manifest is found and the
    shared library is loaded via dlopen; entry point called with manifest
6.  All extensions validated (no address conflicts, compatible API version)
7.  Dependency graph built from attaches_to / provides declarations
8.  Topological sort determines init order (error on cycles)
9.  Extensions initialised in dependency order
10. grpc::Service* instances collected from all initialised extensions
11. PeripheralExtensionService created (core discovery service)
12. ServerBuilder registers all built-in + extension + discovery services
13. BuildAndStart() -- gRPC server begins accepting connections
14. Emulation begins
```

## Source Tree Layout

```
src/
  core/
    include/beebium/extension/
      PeripheralExtension.hpp     # Base class for all extensions
      ExtensionManifest.hpp       # Manifest data structure
      ExtensionContext.hpp         # Type-safe port handle access
      ExtensionRegistry.hpp        # Dependency resolution and lifecycle
      OneMHzBusPort.hpp            # 1 MHz bus port handle (replaces FredJimRegion)
      OneMHzBusDevice.hpp          # Device callback interface for 1 MHz bus
      PluginLoader.hpp             # Manifest scanning and dlopen loading
    src/
      PluginLoader.cpp             # Platform-specific dlopen/LoadLibrary
  extensions/
    CMakeLists.txt                 # Umbrella for all extensions
    test-scratch-ram/              # Example: TestScratchRam
      CMakeLists.txt               # Static + shared library targets
      TestScratchRam.hpp           # Extension class
      TestScratchRam.cpp           # Implementation (init, shutdown, service)
      ScratchRamService.hpp        # gRPC service implementation
      scratch_ram.proto            # Proto definition
      manifest.json                # Plugin manifest
      plugin_entry.cpp             # Plugin entry point
  service/
    proto/
      peripheral_extension.proto   # Core discovery service proto
    include/beebium/service/
      PeripheralExtensionService.hpp  # Core discovery service
      Server.hpp                   # Modified: accepts extension_services span
  server/
    include/beebium/server/
      ServerMain.hpp               # Modified: wires registry into startup
  cmake/
    BeebiumProto.cmake             # Reusable proto compilation function
```

## References

- [Hard Disc Emulation Comparison](hard-disc-comparison.md) -- SCSI protocol details, controller comparisons, image formats, hardware references (BeebSCSI, Pi1MHz), AIV/VP415 LaserDisc support, iSCSI backend
- [Floppy Disc Image Format Comparison](floppy-disc-comparison.md) -- floppy controller comparisons across emulators
- [Disc Subsystem](disc-subsystem.md) -- WD1770 floppy architecture and the `DiscControllerSocket` pattern that inspired the extension registry
- [gRPC Server Interface](grpc-server.md) -- existing service definitions
- [Clock Architecture](clock-architecture.md) -- bus stretching for 1 MHz peripherals
