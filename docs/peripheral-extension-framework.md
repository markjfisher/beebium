# Peripheral Extension Framework

## BBC Micro Peripheral Interfaces

The BBC Micro provided several external interfaces for connecting third-party hardware:

| Interface | Address Space | Signal Levels | Present On |
|-----------|--------------|---------------|------------|
| 1 MHz Bus (FRED/JIM) | 0xFC00-0xFDFF | TTL (ext), CMOS (int) | Model B, B+, Master |
| User Port | User VIA Port B | TTL | Model B, B+, Master |
| Analogue Port | ADC channels 0-3 | Analogue | Model B, B+, Master |
| Printer Port | User VIA Port A | TTL | Model B, B+, Master |
| RS-423 Serial | ACIA (6850) | RS-423 | Model B, B+, Master |
| Tube | 0xFEE0-0xFEEF | TTL | Model B, B+, Master |
| Cartridge Slots | Sideways ROM space | TTL | Master only |

Not all machines had all interfaces. The Model A shipped without the User VIA chip (IC69 socket empty), so it had no User Port, no printer port, and no analogue port. The Electron had none of these interfaces without add-on hardware.

A rich ecosystem of peripherals connected to these interfaces: SCSI hard disc adapters, speech synthesizers (TMS5220), music co-processors (Music 5000), teletext adapters, IEEE-488 (GPIB) controllers, joystick interfaces, Econet network adapters, and many more.

### Two Challenges for Emulation

Two properties of BBC Micro peripherals make them difficult to model with a simple "one device, one port" abstraction:

**1. Multi-port devices.** A single hardware expansion can connect to multiple interfaces simultaneously. For example, the Voltmace Delta 14B/1 joystick interface connected to both the User Port (for digital inputs) and the Analogue Port (for analogue axes) simultaneously. The device is conceptually one unit, but it spans two distinct hardware interfaces.

**2. Sub-bus topologies.** Some peripherals don't just connect to the machine -- they introduce an entirely new bus to which further arbitrary devices can be attached. For example:

- An **Acorn SCSI Host Adapter** plugs into the 1 MHz bus and provides a SCSI bus. Hard disc drives, LaserDisc players, and other SCSI devices then connect to that SCSI bus, not directly to the BBC Micro.
- An **IEEE-488 adapter** plugs into the 1 MHz bus and provides a GPIB bus. Instruments, plotters, and other IEEE-488 devices connect to the GPIB bus.
- The **Tube** interface provides a processor bus to which various co-processors (6502, Z80, 68000, ARM) can connect.

These sub-buses can host diverse and open-ended sets of devices. The SCSI bus might have a hard disc at target 0 and a Philips VP415 LaserDisc player at target 1 -- two completely different device types sharing one bus, connected through one adapter, on one port.

## Concrete Example: SCSI Hard Disc and LaserDisc

The SCSI subsystem illustrates both challenges and drives the framework design. For full SCSI protocol details, see [Hard Disc Emulation Comparison](hard-disc-comparison.md).

### The Hardware Topology

```
BBC Micro
  └─ 1 MHz Bus (FRED page, 0xFC00-0xFCFF)
       └─ Acorn SCSI Host Adapter (registers at 0xFC40-0xFC43)
            └─ SCSI Bus
                 ├─ Target 0: 20 MB Winchester hard disc
                 ├─ Target 1: Philips VP415 LaserDisc player
                 └─ Target 2: (empty)
```

The host adapter is a 1 MHz bus peripheral. The hard disc and LaserDisc player are SCSI bus devices. These are different kinds of thing at different levels of the topology: the adapter handles bus signalling and phase protocols; the targets handle SCSI commands (READ/WRITE for the disc, F-codes for the VP415). The adapter doesn't know or care what its targets do with the commands it delivers.

### Internal vs External Adapters

The same SCSI host adapter exists in two physical forms:

- **External** (Model B): A card plugged into the external 1 MHz bus connector by the user. An add-on purchase.
- **Internal** (Master AIV): Factory-fitted to the internal 1 MHz bus socket (PL12) on the motherboard. Part of the machine definition.

The SCSI protocol is identical in both cases. The adapter code is the same. Only the physical attachment differs -- and this distinction matters for how we model it in the emulator: the Master AIV's adapter is always present (it's built into the machine), while the Model B's adapter is optional (the user chose to buy it).

### What the Framework Must Support

From this single example, several requirements emerge:

1. **Pluggable 1 MHz bus** -- the SCSI adapter must register at 0xFC40-0xFC43 on the FRED page without modifying the emulator core.
2. **Sub-bus creation** -- the SCSI adapter must be able to provide a "scsi" bus to which targets can attach.
3. **Polymorphic targets** -- a hard disc target and a VP415 target have completely different command sets but share the same bus attachment mechanism.
4. **Built-in or plugin** -- the same adapter code must work as a built-in module (Master AIV) or a dynamically loaded plugin (Model B).
5. **Machine-variant constraints** -- a Model A has no 1 MHz bus, so the SCSI adapter cannot be attached. This should be enforced structurally, not by special-case code.
6. **gRPC services per device** -- the SCSI adapter needs a ScsiService for bus management; the VP415 target might need its own RPCs for F-code control. Each device at any level of the topology should be able to expose its own gRPC service.

## Extension Points and Extensions

The framework generalises from the SCSI example using two concepts:

**Extension points** are named attachment sites provided by the machine or by other extensions. They represent places where hardware can be connected:

| Extension Point | Type | Provider |
|----------------|------|----------|
| `1mhz-bus` | Built-in | Machine hardware (FRED/JIM region) |
| `user-port` | Built-in | User VIA Port B |
| `analogue-port` | Built-in | Machine hardware (ADC) |
| `printer-port` | Built-in | User VIA Port A |
| `rs423` | Built-in | ACIA (6850) |
| `tube` | Built-in | Tube interface |
| `scsi` | Dynamic | Created by SCSI Host Adapter extension |
| `gpib` | Dynamic | Created by IEEE-488 adapter extension |

Built-in extension points are registered by the machine's hardware policy class at startup. Dynamic extension points are created by extensions during their initialisation -- the SCSI host adapter creates the `scsi` extension point; an IEEE-488 adapter would create `gpib`.

**Extensions** are modules that attach to extension points and optionally provide new ones. Each extension is a C++ class implementing the `PeripheralExtension` interface:

```cpp
class PeripheralExtension {
public:
    virtual ~PeripheralExtension() = default;

    // Identity and dependencies
    virtual std::string_view name() const = 0;            // e.g. "acorn-scsi", "vp415"
    virtual std::span<const std::string_view> attaches_to() const = 0;
    virtual std::span<const std::string_view> provides() const = 0;

    // Lifecycle -- ExtensionContext provides access to the ports declared in attaches_to
    virtual void init(ExtensionContext& ctx) = 0;
    virtual void shutdown() = 0;

    // Optional gRPC services for client interaction
    virtual std::vector<grpc::Service*> grpc_services() { return {}; }
};
```

The base class handles identity, dependencies, lifecycle, and gRPC services -- concerns common to all peripherals regardless of which port they attach to.

I/O methods are deliberately **not** on `PeripheralExtension`. A 1 MHz bus peripheral has address-decoded register I/O; a User Port peripheral has 8-bit parallel I/O with handshake lines; an Analogue Port peripheral has ADC channels. These are fundamentally different interaction protocols. Putting them all on the base class (or using a per-port-type subclass hierarchy) would conflate "connects to" with "is-a" -- a Voltmace Delta 14B/1 joystick that connects to both the User Port and Analogue Port would require diamond inheritance from two port-typed base classes.

Instead, each extension point type defines a **port handle** (owned by the machine) and a **device callback interface** (implemented by the extension). The extension *uses* ports rather than *being* a port-typed object:

```cpp
// Port handles -- owned by the machine hardware, passed to extensions via ExtensionContext

class OneMHzBusPort {
public:
    void claim_addresses(uint16_t base, uint16_t end,
                         OneMHzBusDevice& device);       // register for read/write callbacks
};

class UserPort {
public:
    void set_port_b_handler(UserPortDevice& device);     // register for port B I/O
};

class AnaloguePort {
public:
    void set_channel_handler(int channel,
                             AnalogueDevice& device);    // register for ADC reads
};

// Device callback interfaces -- implemented by extensions, one per port type

struct OneMHzBusDevice {
    virtual ~OneMHzBusDevice() = default;
    virtual uint8_t read(uint16_t address) = 0;
    virtual void write(uint16_t address, uint8_t value) = 0;
    virtual void tick() {}                                // 1 MHz clock (optional)
};

struct UserPortDevice {
    virtual ~UserPortDevice() = default;
    virtual uint8_t read_port() = 0;
    virtual void write_port(uint8_t value) = 0;
    virtual void cb1_edge(bool rising) {}                 // handshake line
    virtual void cb2_edge(bool rising) {}                 // handshake line
};

struct AnalogueDevice {
    virtual ~AnalogueDevice() = default;
    virtual uint16_t read_channel() = 0;                  // ADC value (0-65535)
};
```

An extension implements the device callback interfaces for the ports it uses, and receives port handles during `init()` via the `ExtensionContext`. There is no diamond inheritance -- `PeripheralExtension` is the only base class in the extension hierarchy. The device interfaces (`OneMHzBusDevice`, `UserPortDevice`, `AnalogueDevice`) model a *uses* relationship, not *is-a*:

```cpp
// SCSI host adapter -- uses 1 MHz bus only
class AcornScsiHostAdapter : public PeripheralExtension,
                             public OneMHzBusDevice {
public:
    void init(ExtensionContext& ctx) override {
        ctx.get<OneMHzBusPort>().claim_addresses(0xFC40, 0xFC43, *this);
    }
    uint8_t read(uint16_t address) override { /* SCSI register read */ }
    void write(uint16_t address, uint8_t value) override { /* SCSI register write */ }
};

// Voltmace Delta 14B/1 -- uses User Port AND Analogue Port
class VoltmaceDelta14B1 : public PeripheralExtension,
                          public UserPortDevice,
                          public AnalogueDevice {
public:
    void init(ExtensionContext& ctx) override {
        ctx.get<UserPort>().set_port_b_handler(*this);
        ctx.get<AnaloguePort>().set_channel_handler(0, *this);
        ctx.get<AnaloguePort>().set_channel_handler(1, *this);
    }
    uint8_t read_port() override { /* digital joystick buttons */ }
    void write_port(uint8_t value) override { /* output lines */ }
    uint16_t read_channel() override { /* analogue axis position */ }
};
```

Only `OneMHzBusPort` and `OneMHzBusDevice` need to exist now. The other port handles and device interfaces can be added when those extension points are implemented, without changing `PeripheralExtension`, the extension registry, or any existing extensions.

The framework uses C++ throughout -- plugins are compiled with the same compiler and version as the host, so there are no ABI compatibility concerns. This avoids the friction of forcing C++ objects through a C interface (manually constructed vtables, `void*` casts for `grpc::Service*`, C string handling) for a benefit that is purely theoretical in this context.

### Mapping the SCSI Example

| Extension | attaches_to | provides | Notes |
|-----------|-------------|----------|-------|
| SCSI Host Adapter | `1mhz-bus` | `scsi` | Registers at 0xFC40-0xFC43; creates SCSI target registry |
| Hard Disc target | `scsi` | - | Handles READ/WRITE CDBs against a DAT image file |
| VP415 target | `scsi` | - | Handles Group 6 F-code CDBs for LaserDisc control |

The Voltmace Delta 14B/1 joystick interface demonstrates multi-port attachment -- a single extension that uses two ports:

| Extension | attaches_to | provides | Notes |
|-----------|-------------|----------|-------|
| Voltmace Delta 14B/1 | `user-port`, `analogue-port` | - | One extension, implements `UserPortDevice` + `AnalogueDevice` |

The Voltmace is one `PeripheralExtension` that declares `attaches_to = {"user-port", "analogue-port"}`. During `init()`, it receives handles to both ports via `ExtensionContext` and registers itself as the device callback for each. No diamond inheritance, no multiple `PeripheralExtension` subclasses -- just one extension implementing two small device interfaces.

## Extension Points as Hardware Specification

The set of extension points registered at startup defines what can be attached to the emulated machine. Each hardware policy class (`ModelBHardware`, `ModelBPlusHardware`, etc.) registers the extension points corresponding to its physical ports:

| Extension Point | Model A | Model B | Model B+ | Master 128 | Master AIV |
|----------------|---------|---------|----------|------------|------------|
| `1mhz-bus` | - | Yes | Yes | Yes (ext) | Yes (ext+int) |
| `user-port` | - | Yes | Yes | Yes | Yes |
| `analogue-port` | - | Yes | Yes | Yes | Yes |
| `printer-port` | - | Yes | Yes | Yes | Yes |
| `rs423` | - | Yes | Yes | Yes | Yes |
| `tube` | - | Yes | Yes | Yes | Yes (65C102) |
| `cartridge-slots` | - | - | - | Yes (2) | Yes (2) |

A plugin declaring `attaches_to = {"1mhz-bus"}` fails to load on a Model A with a clear diagnostic: "extension point '1mhz-bus' not available on this machine". No special-case code is needed -- the absence of the extension point *is* the hardware limitation. You cannot plug a Teletext Adapter into a Model A, and the emulator correctly refuses for the same structural reason the real hardware cannot.

This scales in both directions. A fully-loaded Master AIV with SCSI adapter, VP415, hard disc, Music 5000, and joystick interface works because all required extension points are present and the dependency graph resolves cleanly. A bare Model A works because no extension points are registered and no plugins attempt to attach to absent ports.

## Dependency Resolution

Extensions form a directed acyclic graph (DAG) through their `attaches_to` and `provides` declarations. The framework resolves this graph at startup via topological sort:

```
1. Enumerate built-in extension points: 1mhz-bus, user-port, analogue-port, ...
2. Discover all extensions (built-in modules + plugins)
3. Build dependency graph from attaches_to / provides declarations
4. Topological sort (error on cycles with diagnostic message)
5. Init extensions in dependency order:
   a. SCSI Host Adapter (needs 1mhz-bus) --> creates "scsi" extension point
   b. Music 5000 (needs 1mhz-bus)
   c. Hard Disc target (needs scsi)
   d. VP415 target (needs scsi)
   e. Voltmace Delta 14B/1 (needs user-port AND analogue-port)
6. Collect grpc::Service* instances from all initialised extensions
7. ServerBuilder registers all services, BuildAndStart()
```

Each extension is simple and focused. The VP415 plugin knows nothing about the 1 MHz bus -- it declares `attaches_to = {"scsi"}` and implements `handle_cdb`. The Voltmace plugin knows nothing about SCSI -- it declares `attaches_to = {"user-port", "analogue-port"}` and implements port handlers. The dependency graph resolution is infrastructure-layer complexity, done once, keeping individual extensions lean.

Cycles are detected by the topological sort and reported as startup errors. In practice, the BBC Micro's peripheral topology is a tree, so cycles would indicate a misconfigured or buggy plugin.

## Built-in Modules and Plugins

Extensions can be either compiled into the server executable or loaded from shared libraries. The extension registry treats both identically -- it just sees descriptors.

### Built-in Modules

Built-in modules are linked into the server executable and register statically during server initialisation:

```cpp
// In main_model_b.cpp or similar
extension_registry.register_extension(std::make_unique<ScsiHostAdapterExtension>());
extension_registry.register_extension(std::make_unique<AcornUserPortRtcExtension>());
```

### Plugin Modules

Plugin modules are shared libraries discovered by scanning a plugin directory for `.so`/`.dll`/`.dylib` files. Each is loaded via `dlopen`/`LoadLibrary`, and a well-known entry point is called to obtain a `PeripheralExtension` instance. The entry point uses `extern "C"` linkage solely to provide a stable symbol name for `dlsym` -- the returned object and all subsequent interaction is C++:

```cpp
// Each plugin exports this symbol
extern "C" std::unique_ptr<beebium::PeripheralExtension> beebium_create_extension();
```

Plugins must be compiled with the same compiler and version as the Beebium server. This is a deliberate trade-off: it sacrifices cross-compiler and cross-language plugin compatibility in exchange for a natural C++ interface with no casting, no manual vtables, and no `void*` smuggling of `grpc::Service*` pointers. In practice, plugins will be built alongside Beebium using the same toolchain.

### The Registry Doesn't Care

From the registry's perspective, both paths produce `PeripheralExtension` objects with the same interface:

```
Extension Registry
  +-  Built-in: ScsiHostAdapterExtension    (linked, registered statically)
  +-  Built-in: AcornUserPortRtcExtension        (linked, registered statically)
  +-  Plugin:   Music5000Extension          (music5000.dylib, discovered and loaded)
  +-  Plugin:   CustomExtension             (myperi.dylib, discovered and loaded)
```

### The Internal/External Boundary

The guiding principle for whether an extension is built-in or a plugin follows the real hardware:

- **Internal hardware** (factory-fitted, soldered, or socketed inside the machine): **built into the server executable**. It is part of the machine definition and is always present.
- **External hardware** (plugged into a port by the user): **loaded from a plugin**. It is an add-on that the user chooses to attach.

The Master AIV's SCSI host adapter was factory-fitted to PL12 on the motherboard -- `beebium-master-aiv` links the adapter code directly. A Model B owner bought a SCSI card and plugged it into the external 1 MHz bus -- `beebium-model-b` loads `scsi_host_adapter.dylib` from the plugin directory.

The underlying code is identical. Only the linkage differs:

```
beebium-master-aiv
  +-- links scsi_host_adapter.o directly
     (registered as built-in during hardware policy init)

beebium-model-b
  +-- loads scsi_host_adapter.dylib at startup
     (discovered in plugin directory, attaches to "1mhz-bus")
```

The same source compiles to both a static library (for built-in use) and a shared library (for plugin use), with no conditional compilation needed. SCSI target plugins (hard disc, VP415) work with either, without knowing whether the adapter was built-in or loaded from a shared library. The `PeripheralExtension` and `ScsiTarget` interfaces are the same in both cases -- polymorphism through C++ virtual dispatch, not through function pointer tables or `void*` casts.

## Sub-Bus Target Interface

Extensions that provide sub-buses (like the SCSI host adapter) need their own target registry. The host adapter handles bus protocol (selection, phases, REQ/ACK handshaking) and delegates command processing to pluggable target implementations.

Each SCSI target implements a C++ interface:

```cpp
class ScsiTarget {
public:
    virtual ~ScsiTarget() = default;

    virtual std::string_view name() const = 0;       // e.g. "adfs-disc", "vp415"
    virtual uint8_t default_scsi_id() const = 0;     // preferred target ID

    // Command processing -- receives raw CDB and a data buffer for the response
    virtual ScsiStatus handle_cdb(std::span<const uint8_t> cdb,
                                  std::span<uint8_t> data_buffer) = 0;

    virtual void init() = 0;
    virtual void shutdown() = 0;

    // Optional gRPC services for target-specific client interaction
    virtual std::vector<grpc::Service*> grpc_services() { return {}; }
};
```

The host adapter calls `target[N]->handle_cdb()` when a CDB arrives for target N. A hard disc target processes READ/WRITE against a DAT file. A VP415 target processes Group 6 F-code commands. The adapter is identical in both cases -- it just moves bytes between the 6502 bus and the target.

**Key design principle**: the host adapter must be designed from day one with the target registry as its core abstraction, even if the only initial target is a hard disc. Adding `handle_cdb` dispatch through a target interface costs almost nothing upfront but prevents a major restructuring when VP415 or other device support is added later.

## gRPC Service Architecture

### Follow the Hardware Topology

gRPC services should mirror the hardware tree rather than classifying by media type. Neither domain-driven naming (FloppyDiscService, HardDiscService) nor capability-driven naming (FixedDiscService, RemovableDiscService) scales -- a VP415 is not a disc, a Jaz drive is a removable SCSI disc, and "hard disc" conflates media with controller.

```
Machine
  +-- FloppyControllerService          (WD1770 at 0xFE80/0xFE84)
  |    Operations: insert/eject disc image, query drive status
  |
  +-- OneMHzBusService                 (FRED/JIM, 0xFC00-0xFDFF)
  |    Operations: list attached peripherals, attach/detach peripheral
  |
  +-- ScsiService                      (via host adapter at 0xFC40)
       Operations: list targets, attach/detach target, query bus state
       |
       +-- Target 0: ScsiDiscTarget    (hard disc image)
       |    Operations: mount/unmount image, query geometry, format
       |
       +-- Target 1: ScsiVideoTarget   (VP415 LaserDisc)
       |    Operations: load disc, send F-code, query player status
       |
       +-- Target 2: ScsiDiscTarget    (another disc, or Jaz, etc.)
            Operations: mount/unmount image, query geometry, format
```

Key principles:

1. **The existing DiscService should be renamed to FloppyControllerService** (or similar). It manages the WD1770 and has nothing to do with SCSI.

2. **OneMHzBusService is thin** -- it reports what's plugged in and provides install/remove operations. Each peripheral type brings its own service for device-specific interaction.

3. **ScsiService manages the SCSI bus** -- targets are polymorphic. Bus-level concerns (enumeration, selection, reset) are separate from target-specific operations.

4. **Media type is a property of the target, not of the service**. A ScsiDiscTarget could be backed by a Winchester image, a Jaz image, or an iSCSI LUN.

### Service Lifecycle and Hardware Presence

ScsiService only makes sense if a SCSI adapter is present on the 1 MHz bus. Beebium already has a pattern for this: `DiscService` is always registered as a gRPC service, but `InstallDiscController()` must be called before disc operations work -- because the `DiscControllerSocket` might be empty. The service is the API surface; the socket is the hardware presence.

```
OneMHzBusService                     (always registered)
  +-- ListPeripherals()               -> [{address: 0xFC40, type: "acorn-scsi"}, ...]
  +-- InstallPeripheral(type, addr)   -> plugs hardware into the bus
  +-- RemovePeripheral(addr)          -> unplugs hardware

ScsiService                          (always registered; operations return
  |                                   FAILED_PRECONDITION if no adapter installed)
  +-- ListTargets()                   -> [{id: 0, type: "disc"}, {id: 1, type: "vp415"}]
  +-- AttachDiscTarget(id, url)       -> mounts a DAT image at SCSI target ID
  +-- AttachVideoTarget(id, url)      -> attaches VP415 emulation at target ID
  +-- DetachTarget(id)
  +-- GetTargetStatus(id)
  +-- SendFCode(id, fcode)            -> VP415-specific; error if wrong target type
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

ScsiService is separate from OneMHzBusService because the SCSI bus has its own topology (targets, LUNs) that deserves its own API surface. Other 1 MHz peripherals (Music 5000, speech chip) would get their own services too -- OneMHzBusService should not accumulate every peripheral's API.

### gRPC Service Registration for Plugins

gRPC C++ does **not** support adding services to a running server -- `ServerBuilder::RegisterService()` must be called before `BuildAndStart()`. Plugins are therefore loaded during server startup, before the gRPC server starts. Each plugin's `grpc_services()` are collected and registered normally via `ServerBuilder`. Full typed service support and gRPC reflection work without any workarounds.

This reflects real hardware practice: you don't hot-plug cards into a 1 MHz bus on a running BBC Micro. Changing the hardware configuration means restarting the Beebium server process -- the emulator equivalent of power-cycling the machine.

Three alternative approaches remain available if true hot-loading becomes desirable in future:

- **Approach A: Generic service with dynamic dispatch.** A `CallbackGenericService` catches unmatched RPCs and dispatches to plugin handlers via a mutable method-name table. Limitation: gRPC reflection won't list plugin services, and all RPCs are modelled as bidirectional streams at the framework level.

- **Approach B: Server restart on plugin load.** Shut down the gRPC server, rebuild with the new service, restart on the same port. Severs active streams but Beebium clients already handle reconnection.

- **Approach C: Plugin runs its own gRPC server.** Beebium already uses this pattern: `ParasiteServer` runs an independent gRPC server for Tube co-processor debugging.

## Startup Sequence

```
1.  Beebium server starts, parses command-line options
2.  Hardware policy class registers built-in extension points
    (1mhz-bus, user-port, analogue-port, etc. -- varies by machine variant)
3.  Built-in extensions register their descriptors with the extension registry
4.  Plugin directory scanned; each .so/.dll/.dylib loaded,
    beebium_create_extension() called to obtain its PeripheralExtension instance
5.  All descriptors validated (no address conflicts, compatible API version)
6.  Dependency graph built from attaches_to / provides declarations
7.  Topological sort determines init order (error on cycles)
8.  Extensions initialised in dependency order:
    - Bus-level extensions first (SCSI adapter, Music 5000, etc.)
    - Sub-bus devices second (SCSI hard disc target, VP415 target, etc.)
9.  grpc::Service* instances collected from all initialised extensions
    (each extension may provide zero or more services)
10. ServerBuilder registers all built-in gRPC services + extension services
11. BuildAndStart() -- gRPC server begins accepting connections
12. Emulation begins
```

## References

- [Hard Disc Emulation Comparison](hard-disc-comparison.md) -- SCSI protocol details, controller comparisons, image formats, hardware references (BeebSCSI, Pi1MHz), AIV/VP415 LaserDisc support, iSCSI backend
- [Floppy Disc Image Format Comparison](floppy-disc-comparison.md) -- floppy controller comparisons across emulators
- [Disc Subsystem](disc-subsystem.md) -- WD1770 floppy architecture and the `DiscControllerSocket` pattern that inspired the extension registry
- [gRPC Server Interface](grpc-server.md) -- existing service definitions
- [Clock Architecture](clock-architecture.md) -- bus stretching for 1 MHz peripherals
