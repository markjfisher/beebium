# Peripheral Extension Framework

The BBC Micro supported a diverse ecosystem of third-party peripherals: SCSI hard disc adapters, speech synthesizers, music co-processors, teletext adapters, joystick interfaces, and more. These connected via external ports (1 MHz bus, User Port, Analogue Port, RS-423) and varied by machine model -- a Model A had no external ports at all, while a fully-loaded Master AIV could host multiple simultaneous peripherals.

Beebium needs a principled architecture for pluggable peripheral support that:
- Allows peripherals to be added without modifying the emulator core
- Supports both built-in modules (compiled into the server) and plugins (loaded from shared libraries)
- Lets each peripheral expose its own gRPC service for client interaction
- Enforces hardware accuracy -- only peripherals compatible with the emulated machine variant can be attached
- Handles sub-bus topologies (e.g. SCSI targets behind a SCSI host adapter on the 1 MHz bus)

The primary use cases driving this design are SCSI hard disc support (see [Hard Disc Emulation Comparison](hard-disc-comparison.md)) and floppy disc controller variants (see [Floppy Disc Image Format Comparison](floppy-disc-comparison.md)).

## gRPC Service Architecture for Peripherals

### The Naming Problem

Neither domain-driven naming (FloppyDiscService, HardDiscService) nor capability-driven naming (FixedDiscService, RemovableDiscService) scales well:

- A VP415 LaserDisc player is a SCSI target but not a disc drive at all
- An Iomega Jaz drive would be a removable SCSI disc, breaking the fixed/removable split
- "Hard disc" conflates the media type with the controller type (SCSI)
- The floppy controller (WD1770 on SHEILA) and SCSI adapter (on FRED) are fundamentally different hardware on different buses

### Follow the Hardware Topology

The gRPC services should mirror the actual hardware tree rather than classifying by media type:

```
Machine
  ├─ FloppyControllerService          (WD1770 at 0xFE80/0xFE84)
  │    Operations: insert/eject disc image, query drive status
  │
  ├─ OneMHzBusService                 (FRED/JIM, 0xFC00-0xFDFF)
  │    Operations: list attached peripherals, attach/detach peripheral
  │
  └─ ScsiService                      (via host adapter at 0xFC40)
       Operations: list targets, attach/detach target, query bus state
       │
       ├─ Target 0: ScsiDiscTarget    (hard disc image)
       │    Operations: mount/unmount image, query geometry, format
       │
       ├─ Target 1: ScsiVideoTarget   (VP415 LaserDisc)
       │    Operations: load disc, send F-code, query player status
       │
       └─ Target 2: ScsiDiscTarget    (another disc, or Jaz, etc.)
            Operations: mount/unmount image, query geometry, format
```

Key principles:

1. **The existing DiscService should be renamed to FloppyControllerService** (or similar). It manages the WD1770 and its attached drives. It has nothing to do with SCSI.

2. **A OneMHzBusService reports and manages attached peripherals**. Each peripheral type can register its own gRPC service on attachment. This mirrors how Pi1MHz handles multiple simultaneous peripherals at different FRED addresses.

3. **A ScsiService manages the SCSI bus** -- the host adapter and its targets. Targets are polymorphic: a hard disc target and a VP415 target both sit on the same bus but expose different operations. The ScsiService handles bus-level concerns (target enumeration, selection, bus reset) while delegating target-specific operations.

4. **Target-specific sub-services or RPCs** handle the diversity of SCSI devices. A hard disc target supports mount/unmount/format. A VP415 target supports F-code commands and player status queries. Both share the SCSI bus phase protocol but differ in their command sets.

5. **Media type is a property of the target, not of the service**. A ScsiDiscTarget could be backed by a fixed Winchester image, a removable Jaz image, or an iSCSI LUN. The mount/unmount operations are the same; the media characteristics (read-only, removable, capacity) are reported as target properties.

This approach avoids the need to invent taxonomy for every possible SCSI device. New target types (tape drives, optical drives, network-backed storage) slot in without restructuring the service layer. The bus topology is the stable structure; the device types are the variable part.

### Service Lifecycle and Hardware Presence

ScsiService only makes sense if a SCSI adapter is present on the 1 MHz bus. Beebium already has a pattern for this: `DiscService` is always registered as a gRPC service, but `InstallDiscController()` must be called before disc operations work -- because the `DiscControllerSocket` might be empty. The service is the API surface; the socket is the hardware presence.

The same pattern extends to the 1 MHz bus and SCSI:

```
OneMHzBusService                     (always registered)
  ├─ ListPeripherals()               → [{address: 0xFC40, type: "acorn-scsi"}, ...]
  ├─ InstallPeripheral(type, addr)   → plugs hardware into the bus
  └─ RemovePeripheral(addr)          → unplugs hardware

ScsiService                          (always registered; operations return
  │                                   FAILED_PRECONDITION if no adapter installed)
  ├─ ListTargets()                   → [{id: 0, type: "disc"}, {id: 1, type: "vp415"}]
  ├─ AttachDiscTarget(id, url)       → mounts a DAT image at SCSI target ID
  ├─ AttachVideoTarget(id, url)      → attaches VP415 emulation at target ID
  ├─ DetachTarget(id)
  ├─ GetTargetStatus(id)
  └─ SendFCode(id, fcode)            → VP415-specific; error if wrong target type
```

The lifecycle mirrors real hardware:

```
1. Server starts.
   OneMHzBusService and ScsiService registered, but the 1 MHz bus
   has no peripherals. ScsiService calls return FAILED_PRECONDITION.

2. Client calls OneMHzBusService.InstallPeripheral("acorn-scsi", 0xFC40).
   A SCSI host adapter is created and plugged into the FredJimRegion.
   ScsiService now has an adapter to work with.

3. Client calls ScsiService.AttachDiscTarget(0, "file:///path/to/scsi0.dat").
   Target 0 on the SCSI bus becomes a hard disc backed by that image.

4. The emulated BBC Micro boots ADFS, talks to 0xFC40, finds a disc.
```

This mirrors the existing `DiscService.InstallDiscController("acorn-1770")` pattern. The bus management service handles hardware presence; the device-specific service handles device operations.

ScsiService is a separate gRPC service (rather than RPCs on OneMHzBusService) because:

- The SCSI bus has its own topology (targets, LUNs) that deserves its own API surface
- Device-specific operations (F-codes for VP415, geometry for discs) don't belong on a bus management service
- Other 1 MHz peripherals (Music 5000, speech chip) would get their own services too -- OneMHzBusService should not accumulate every peripheral's API
- This matches the existing pattern where DiscService is separate from the machine configuration that installs the WD1770

The result is that OneMHzBusService is thin: it reports what's plugged in and provides install/remove operations. Each peripheral type brings its own service for device-specific interaction. The client discovers what's available by querying OneMHzBusService, then talks to the appropriate device service.

## Extension Registry

### Unified Extension Descriptors

All peripherals -- whether compiled into the server or loaded from shared libraries -- register through the same extension registry before the gRPC server starts. The registry collects extension descriptors; each descriptor provides C API function pointers (init, read, write, tick, etc.), address claims, and an optional `grpc::Service*` for gRPC registration.

The registry does not care how a module was loaded. It just sees a descriptor:

```c
typedef struct {
    const char* name;                          // e.g. "acorn-scsi", "music-5000"
    const char* version;
    uint16_t fred_base;                        // e.g. 0xFC40
    uint16_t fred_end;                         // e.g. 0xFC43
    beebium_ext_init_fn init;                  // called once at startup
    beebium_ext_read_fn read;                  // called on FRED/JIM read
    beebium_ext_write_fn write;                // called on FRED/JIM write
    beebium_ext_tick_fn tick;                  // called on 1 MHz clock (optional)
    beebium_ext_shutdown_fn shutdown;           // called at server shutdown
    void* grpc_service;                        // optional grpc::Service* (opaque to C API)
} beebium_extension_descriptor;
```

### Built-in and Plugin Module Discovery

**Built-in modules** (linked into the server executable) register statically during server initialisation by calling the registry directly:

```cpp
// In main_model_b.cpp or similar
extension_registry.register_extension(scsi_host_adapter_descriptor());
extension_registry.register_extension(econet_clock_descriptor());
```

**Plugin modules** (shared libraries) are discovered by scanning a plugin directory for `.so`/`.dll`/`.dylib` files. Each is loaded via `dlopen`/`LoadLibrary`, and a well-known C entry point is called to obtain the same descriptor:

```c
// Each plugin exports this symbol
const beebium_extension_descriptor* beebium_extension_describe(void);
```

From the registry's perspective, both paths produce identical descriptors:

```
Extension Registry
  ├─ Built-in: SCSI Host Adapter    (linked, registers statically)
  ├─ Built-in: Econet Clock          (linked, registers statically)
  ├─ Plugin:   Music 5000            (music5000.dylib, discovered and loaded)
  └─ Plugin:   Custom peripheral     (myperi.dylib, discovered and loaded)
```

Core peripherals (SCSI host adapter, Econet) would typically be built-in. Niche or experimental peripherals (Music 5000, speech chip, custom educational hardware) could be plugins, keeping the server executables lean while remaining extensible.

### gRPC Service Registration for Plugins

Plugins are loaded during server startup, *before* the gRPC server is started. This avoids all dynamic service registration complexity.

gRPC C++ does **not** support adding services to a running server -- `ServerBuilder::RegisterService()` must be called before `BuildAndStart()`, and there is no public API to register services afterward. By loading plugins before the server starts, each plugin's `grpc::Service*` is collected and registered normally via `ServerBuilder`. Full typed service support and gRPC reflection work without any workarounds.

This reflects real hardware practice: you don't hot-plug cards into a 1 MHz bus on a running BBC Micro. If you want to change attached hardware, restart the machine. This is the same constraint that applies to Tube co-processors -- the parasite server starts alongside the main server, not later. Changing the hardware configuration means restarting the Beebium server process, which is the emulator equivalent of power-cycling the machine.

Three alternative approaches remain available as future options if true hot-loading during emulation becomes desirable:

**Approach A: Generic service with dynamic dispatch.** A `CallbackGenericService` is registered at server startup as a catch-all for RPCs that don't match any built-in service. It receives the full method name (e.g. `/beebium.Music5000Service/SetWaveform`) and raw serialized bytes. Plugins register handlers into a mutable dispatch table keyed by method name. The plugin handles its own protobuf serialization via the C API (receiving/returning `uint8_t*` + length), which avoids C++ ABI coupling entirely. Limitations: gRPC server reflection will not automatically list plugin services; all RPCs are modelled as bidirectional streams at the framework level.

**Approach B: Server restart on plugin load.** When a plugin is loaded, the gRPC server is shut down, rebuilt with all existing services plus the plugin's service, and restarted on the same port. Full typed service support and working reflection, but severs all active streams during the restart.

**Approach C: Plugin runs its own gRPC server.** Each plugin starts a separate gRPC server on its own port. Beebium already uses this pattern: `ParasiteServer` runs an independent gRPC server for Tube co-processor debugging. Service discovery would need extending to advertise plugin server ports alongside the main server.

## Sub-Bus Extensibility

The extension architecture has a second level: some extensions provide their own buses with their own pluggable devices. The SCSI host adapter is the primary example -- it sits on the 1 MHz bus and provides a SCSI bus that supports pluggable targets (hard discs, LaserDisc players, etc.).

The host adapter must not own device-specific logic. It handles the bus protocol (selection, phases, REQ/ACK) and delegates CDB processing to pluggable target implementations.

Each SCSI target is described by its own C API descriptor:

```c
typedef struct {
    const char* name;                            // e.g. "adfs-disc", "vp415"
    uint8_t default_scsi_id;                     // preferred target ID
    beebium_scsi_init_fn init;
    beebium_scsi_handle_cdb_fn handle_cdb;       // receives raw CDB bytes + data buffer
    beebium_scsi_get_status_fn get_status;
    beebium_scsi_shutdown_fn shutdown;
    void* grpc_service;                          // optional target-specific gRPC service
} beebium_scsi_target_descriptor;
```

The host adapter calls `target[N].handle_cdb()` when a CDB arrives for target N. A hard disc target processes READ/WRITE against a DAT file. A VP415 target processes Group 6 F-code commands. The adapter is identical in both cases -- it just moves bytes between the 6502 bus and the target. (For SCSI protocol details, see [Hard Disc Emulation Comparison](hard-disc-comparison.md).)

This means the SCSI host adapter could be built-in with hard disc targets built-in, while a VP415 target ships as a separate plugin. Or all three could be plugins. The combinations work because the interfaces are the same:

```
Extension Registry (1 MHz bus)
  ├─ SCSI Host Adapter (built-in)   provides →  SCSI Target Registry
  ├─ Music 5000 (plugin)
  └─ ...

SCSI Target Registry (owned by host adapter)
  ├─ Hard Disc target (built-in)
  ├─ VP415 target (plugin)
  └─ ...
```

**Key design principle**: the SCSI host adapter must be designed from day one with the target registry as its core abstraction, even if the only initial target is a hard disc. Adding the `handle_cdb` dispatch through a target interface (rather than handling READ/WRITE directly in the adapter) costs almost nothing upfront but prevents a major restructuring when VP415 or other SCSI device support is added later.

## Generalised Extension Point Architecture

### Named Extension Points and Dependency Graph

Real BBC Micro hardware has multi-port devices (e.g. a joystick interface that connects to both the User Port and the Analogue Port simultaneously) and sub-bus topologies. A general model uses **named extension points** and **dependency graph resolution**.

**Extension points** are named registries where extensions can attach. Some are built-in (always available), others are created dynamically by extensions:

| Extension Point | Type | Provider |
|----------------|------|----------|
| `1mhz-bus` | Built-in | Machine hardware |
| `user-port` | Built-in | User VIA Port B |
| `analogue-port` | Built-in | Machine hardware |
| `scsi` | Dynamic | SCSI Host Adapter extension |

**Extensions** declare what they attach to and what they provide:

```c
typedef struct {
    const char* name;                    // e.g. "acorn-scsi", "vp415", "joystick"
    const char** attaches_to;            // extension points consumed (NULL-terminated)
    const char** provides;               // extension points created (NULL-terminated)
    beebium_ext_init_fn init;
    beebium_ext_shutdown_fn shutdown;
    void* grpc_service;                  // optional grpc::Service*
    // ... handler function pointers specific to the extension point type
} beebium_extension_descriptor;
```

A single plugin (DLL/SO) can contain multiple extensions. The joystick interface plugin would register two extensions: one attaching to `user-port`, one to `analogue-port`. The SCSI host adapter registers one extension attaching to `1mhz-bus` and providing `scsi`.

**Startup resolves a dependency DAG via topological sort:**

```
1. Enumerate built-in extension points: 1mhz-bus, user-port, analogue-port
2. Discover all plugins (built-in modules + shared libraries)
   - Call beebium_extension_describe() on each
   - Collect all extension descriptors
3. Build dependency graph from attaches_to / provides declarations
4. Topological sort (error on cycles with diagnostic message)
5. Init extensions in dependency order:
   a. SCSI Host Adapter (needs 1mhz-bus) → creates "scsi" extension point
   b. Music 5000 (needs 1mhz-bus)
   c. Hard Disc target (needs scsi)
   d. VP415 target (needs scsi)
   e. Joystick (needs user-port AND analogue-port)
6. Collect grpc::Service* from all initialised extensions
7. ServerBuilder registers all services, BuildAndStart()
```

Each extension is simple and focused. The VP415 plugin knows nothing about the 1 MHz bus -- it declares `attaches_to = {"scsi"}` and implements `handle_cdb`. The joystick plugin knows nothing about SCSI -- it declares `attaches_to = {"user-port", "analogue-port"}` and implements port handlers. The dependency graph resolution is infrastructure-layer complexity, done once, keeping individual extensions lean.

Cycles (extension A provides bus X, extension B on bus X provides bus Y, extension A attaches to bus Y) are detected by the topological sort and reported as startup errors. In practice, the BBC Micro's peripheral topology is a tree, so cycles would indicate a misconfigured or buggy plugin.

This architecture means the SCSI extension point is not special -- it's just another named registry, created by whichever extension provides it. If someone later builds a different SCSI adapter (e.g. a Torch SASI adapter at 0xFDF0), it could provide its own `torch-sasi` extension point with the same target interface, and existing SCSI target plugins would work with either adapter by declaring `attaches_to = {"scsi"}` or `attaches_to = {"torch-sasi"}` as appropriate.

### Extension Points as Hardware Specification

The set of extension points registered at startup defines the machine variant's external hardware. Each hardware policy class (`ModelBHardware`, `ModelBPlusHardware`, etc.) registers the extension points that correspond to its physical ports:

| Extension Point | Model A | Model B | Model B+ | Master 128 | Master AIV |
|----------------|---------|---------|----------|------------|------------|
| `1mhz-bus` | - | Yes | Yes | Yes (ext) | Yes (ext+int) |
| `user-port` | - | Yes | Yes | Yes | Yes |
| `analogue-port` | - | Yes | Yes | Yes | Yes |
| `printer-port` | - | Yes | Yes | Yes | Yes |
| `rs423` | - | Yes | Yes | Yes | Yes |
| `tube` | - | Yes | Yes | Yes | Yes (65C102) |
| `cartridge-slots` | - | - | - | Yes (2) | Yes (2) |

The Model A shipped without the User VIA chip (IC69 socket empty), so it has no User Port, no printer port, and no analogue port -- all of which depend on the User VIA. The Model A's extension point set is empty: it has no external peripheral ports at all.

A plugin that declares `attaches_to = {"1mhz-bus"}` will fail to load on a Model A with a clear diagnostic: "extension point '1mhz-bus' not available on this machine". No special-case code is needed -- the absence of the extension point *is* the hardware limitation. You cannot plug a 1 MHz bus Teletext Adapter into a Model A, and the emulator correctly refuses to do so for the same structural reason the real hardware cannot.

This scales in both directions. A fully-loaded Master AIV with SCSI adapter, VP415, hard disc, Music 5000, and joystick interface works because all the required extension points are present and the dependency graph resolves cleanly. A bare Model A works because no extension points are registered and no plugins attempt to attach to absent ports.

The hardware policy classes already define what hardware is present (RAM size, ROM slots, I/O address map). Extension points extend this pattern to external ports, making the machine variant definition the single source of truth for what can be attached.

### Built-in vs Plugin: The Internal/External Boundary

The guiding principle for whether an extension is built into the server executable or shipped as a plugin follows the real hardware's internal/external boundary:

- **Internal hardware** (factory-fitted, soldered, or socketed inside the machine): **built into the server executable**. It is part of the machine definition and is always present.
- **External hardware** (plugged into a port by the user): **loaded from a plugin**. It is an add-on that the user chooses to attach.

For example, the Master AIV's SCSI host adapter was factory-fitted to the internal 1 MHz bus socket (PL12) on the motherboard. It is part of what makes a Master AIV a Master AIV. In Beebium, `beebium-master-aiv` links the SCSI host adapter code directly and registers it during hardware init -- the adapter is always present, just like the real machine.

A Model B owner who wanted SCSI bought an adapter card and plugged it into the external 1 MHz bus connector. In Beebium, `beebium-model-b` loads `scsi_host_adapter.dylib` from the plugin directory at startup -- the adapter is only present if the user provides the plugin, just like buying the expansion card.

The underlying code is identical -- same extension descriptor, same bus phase state machine, same `handle_cdb` target dispatch. Only the linkage differs:

```
beebium-master-aiv
  └─ links scsi_host_adapter.o directly
     (registered as built-in during hardware policy init)

beebium-model-b
  └─ loads scsi_host_adapter.dylib at startup
     (discovered in plugin directory, attaches to "1mhz-bus")
```

This means:
- The Master AIV server works without any plugin directory -- its SCSI adapter is always present
- A Model B server is lean by default, gaining SCSI only when the plugin is provided
- SCSI target plugins (hard disc, VP415) work with either, without knowing whether the adapter was built-in or loaded from a shared library
- The same source code compiles to both a static library (for built-in use) and a shared library (for plugin use), with no `#ifdef` or conditional compilation needed

## References

- [Hard Disc Emulation Comparison](hard-disc-comparison.md) -- SCSI protocol details, controller comparisons, image formats, hardware references (BeebSCSI, Pi1MHz), AIV/VP415 LaserDisc support, iSCSI backend
- [Floppy Disc Image Format Comparison](floppy-disc-comparison.md) -- floppy controller comparisons across emulators
- [Disc Subsystem](disc-subsystem.md) -- WD1770 floppy architecture and the `DiscControllerSocket` pattern that inspired the extension registry
- [gRPC Server Interface](grpc-server.md) -- existing service definitions
- [Clock Architecture](clock-architecture.md) -- bus stretching for 1 MHz peripherals
