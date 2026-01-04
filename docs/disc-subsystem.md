# Disc Subsystem

This document describes Beebium's disc subsystem architecture for emulating BBC Micro floppy disc storage.

## Architecture Overview

The disc subsystem uses a layered architecture separating storage, physical drive emulation, and controller logic:

```
┌─────────────────────────────────────────────────────────────┐
│                    ModelBPlusHardware                       │
│  ┌─────────────────┐  ┌─────────────────────────────────┐   │
│  │ DiscControlReg  │  │           WD1770                │   │
│  │    (0xFE80)     │──│      (0xFE84-0xFE87)            │   │
│  └─────────────────┘  └──────────┬──────────────────────┘   │
│                                  │                          │
│           ┌──────────────────────┼──────────────────────┐   │
│           │                      │                      │   │
│     ┌─────▼─────┐          ┌─────▼─────┐                │   │
│     │ DiscDrive │          │ DiscDrive │                │   │
│     │  (drive0) │          │  (drive1) │                │   │
│     └─────┬─────┘          └─────┬─────┘                │   │
│           │                      │                      │   │
│     ┌─────▼─────┐          ┌─────▼─────┐                │   │
│     │ DiscImage │          │ DiscImage │                │   │
│     │ (SSD/DSD) │          │ (SSD/DSD) │                │   │
│     └───────────┘          └───────────┘                │   │
└─────────────────────────────────────────────────────────────┘
```

### Layer Responsibilities

| Layer | Class | Responsibility |
|-------|-------|----------------|
| Storage | `DiscImage` | Sector-level read/write, format geometry |
| Drive | `DiscDrive` | Head positioning, motor control, disc insertion |
| Controller | `WD1770` | Command execution, timing, status/interrupt signals |
| Hardware | `ModelBPlusHardware` | Memory-mapped registers, NMI gating |

## Components

### DiscImage (Abstract Interface)

**Header:** `src/core/include/beebium/disc/DiscImage.hpp`

Abstract interface for disc storage backends. Provides sector-level access independent of physical format.

```cpp
class DiscImage {
public:
    // Metadata
    virtual std::string name() const = 0;
    virtual bool is_write_protected() const = 0;
    virtual void set_write_protected(bool) = 0;

    // Geometry
    virtual uint8_t sides() const = 0;              // 1 or 2
    virtual uint8_t tracks_per_side() const = 0;    // 40 or 80
    virtual uint8_t sectors_per_track() const = 0;  // 10 for DFS
    virtual uint16_t sector_size() const = 0;       // 256 for DFS

    // Sector I/O
    virtual bool read_sector(uint8_t side, uint8_t track, uint8_t sector,
                            std::span<uint8_t> buffer) = 0;
    virtual bool write_sector(uint8_t side, uint8_t track, uint8_t sector,
                             std::span<const uint8_t> buffer) = 0;
    virtual void flush() = 0;
};
```

**Implementations:**

| Class | Header | Purpose |
|-------|--------|---------|
| `FileDiscImage` | `disc/FileDiscImage.hpp` | File-backed SSD/DSD images |
| `MemoryDiscImage` | `disc/MemoryDiscImage.hpp` | In-memory images for testing |

### FileDiscImage

**Header:** `src/core/include/beebium/disc/FileDiscImage.hpp`
**Source:** `src/core/src/disc/FileDiscImage.cpp`

Loads disc images from filesystem. Entire image is read into memory; writes go through immediately.

```cpp
// Load existing image (throws std::runtime_error on failure)
auto disc = FileDiscImage::load("/path/to/disc.ssd");

// Create new empty image
auto disc = FileDiscImage::create("/path/to/new.ssd", geometry);
```

**Error conditions:**
- File not found: `"Disc image not found: <path>"`
- Permission denied: `"Cannot open disc image (permission denied?): <path>"`
- Unknown format: `"Unrecognized disc image format (size=N, ext=X): <path>"`

**Write protection:** Automatically detected from filesystem permissions. A read-only file appears as a write-protected disc.

### DiscGeometry

**Header:** `src/core/include/beebium/disc/DiscGeometry.hpp`
**Source:** `src/core/src/disc/DiscGeometry.cpp`

Describes disc format and provides sector offset calculations.

```cpp
struct DiscGeometry {
    DiscFormat format;           // SSD or DSD
    uint8_t sides;               // 1 or 2
    uint8_t tracks_per_side;     // 40 or 80
    uint8_t sectors_per_track;   // 10
    uint16_t sector_size;        // 256

    size_t total_size() const;
    std::optional<size_t> sector_offset(uint8_t side, uint8_t track, uint8_t sector) const;

    static std::optional<DiscGeometry> detect_from_size(size_t file_size, std::string_view extension);
};
```

### DiscDrive

**Header:** `src/core/include/beebium/disc/DiscDrive.hpp`

Emulates physical floppy drive mechanics: head positioning, motor control, disc insertion/ejection.

```cpp
class DiscDrive {
public:
    // Disc management
    void insert(std::unique_ptr<DiscImage> disc);
    std::unique_ptr<DiscImage> eject();
    bool has_disc() const;
    DiscImage* disc() const;

    // Head positioning (0-79 tracks)
    void step_in();              // Toward higher tracks
    void step_out();             // Toward track 0
    void seek(uint8_t track);
    uint8_t current_track() const;
    bool at_track_0() const;

    // Motor control
    void set_motor(bool on);
    bool motor_on() const;

    // Sector access (at current track)
    bool read_sector(uint8_t side, uint8_t sector, std::span<uint8_t> buffer);
    bool write_sector(uint8_t side, uint8_t sector, std::span<const uint8_t> buffer);
    bool is_write_protected() const;
};
```

### WD1770

**Header:** `src/core/include/beebium/disc/WD1770.hpp`

Emulates the Western Digital WD1770 Floppy Disc Controller. This is a large header-only implementation (~770 lines) containing the full state machine.

#### Register Interface

| Offset | Read | Write |
|--------|------|-------|
| 0 | Status | Command |
| 1 | Track | Track |
| 2 | Sector | Sector |
| 3 | Data | Data |

#### Command Types

| Type | Commands | Function |
|------|----------|----------|
| I | Restore, Seek, Step, Step-In, Step-Out | Head positioning |
| II | Read Sector, Write Sector | Sector I/O |
| III | Read Address, Read Track, Write Track | Track-level I/O |
| IV | Force Interrupt | Command termination/interrupt control |

#### Status Register Bits

The meaning of status bits depends on command type:

| Bit | Type I | Type II/III |
|-----|--------|-------------|
| 0 | BUSY | BUSY |
| 1 | INDEX | DRQ |
| 2 | TRACK0 | LOST_DATA |
| 3 | CRC_ERROR | CRC_ERROR |
| 4 | SEEK_ERROR | RNF |
| 5 | SPIN_UP | RECORD_TYPE |
| 6 | WRITE_PROT | WRITE_PROT |
| 7 | MOTOR_ON | MOTOR_ON |

#### Key Methods

```cpp
class WD1770 {
public:
    // Register access
    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t value);

    // Clock (1MHz)
    void tick();

    // Interrupt signals
    bool drq() const;        // Data request
    bool intrq() const;      // Interrupt request
    bool nmi_pending() const;  // For NmiAggregator

    // Drive attachment
    void attach_drive(int drive_num, DiscDrive* drive);

    // External control signals
    void set_side(uint8_t side);
    void set_drive(uint8_t drive);
    void set_density(bool double_density);

    void reset();
};
```

#### Force Interrupt (Type IV) Command

The Force Interrupt command `0xDx` uses bits I0-I3 to control interrupt behavior:

| Command | I3 | I2 | I1 | I0 | Behavior |
|---------|----|----|----|----|----------|
| `0xD0` | 0 | 0 | 0 | 0 | Terminate command, no interrupt |
| `0xD8` | 1 | 0 | 0 | 0 | Immediate interrupt |
| `0xD4` | 0 | 1 | 0 | 0 | Interrupt on index pulse |
| `0xD2` | 0 | 0 | 1 | 0 | Interrupt on ready→not-ready |
| `0xD1` | 0 | 0 | 0 | 1 | Interrupt on not-ready→ready |

## Hardware Integration (Model B+)

**Header:** `src/core/include/beebium/ModelBPlusHardware.hpp`

The Model B+ has a built-in WD1770 disc controller. The hardware class owns the controller and drives:

```cpp
class ModelBPlusHardware {
    WD1770 disc_controller;
    DiscDrive disc_drive_0;
    DiscDrive disc_drive_1;
    // ...
};
```

### Memory Map

| Address | Function |
|---------|----------|
| 0xFE80 | Disc control register |
| 0xFE84 | WD1770 Status/Command |
| 0xFE85 | WD1770 Track |
| 0xFE86 | WD1770 Sector |
| 0xFE87 | WD1770 Data |

### Disc Control Register (0xFE80)

| Bit | Function |
|-----|----------|
| 0 | Drive 0 select (active high) |
| 1 | Drive 1 select (active high) |
| 2 | Side select (0=side 0, 1=side 1) |
| 3 | Density (0=double/MFM, 1=single/FM) |
| 4 | Motor on (active high) |
| 5 | WD1770 reset (active low) |
| 6 | NMI enable (nominally gates INTRQ/DRQ to NMI, see [NMI Gating](#nmi-gating-bit-6)) |

Note: Bits 0 and 1 are active-high drive selects, not a binary drive number. DFS sets bit 0 for drive 0, bit 1 for drive 1.

**Important:** This register is **write-only**. Reading returns 0xFF (open bus). See [8271 vs 1770 Detection](#8271-vs-1770-detection) below.

### 8271 vs 1770 Detection

The Model B+ motherboard was designed to accept either an Intel 8271 or WD1770 disc controller. In practice, only the WD1770 was ever fitted (soldered in). However, the address decoding deliberately swaps the register addresses between the two controllers to allow DFS to detect which is present:

| Controller | Command/Status Registers | Data/Control Registers |
|------------|-------------------------|------------------------|
| Intel 8271 | 0xFE80-0xFE83 (readable) | 0xFE84-0xFE87 (DACK) |
| WD1770 | 0xFE80-0xFE83 (write-only latch) | 0xFE84-0xFE87 (registers) |

From the B+ Service Manual (section 5.5.2):

> "It can be seen that the 1770 controller and the 8271 controller address space has been swapped. This is to allow the disc system software to distinguish between the two devices."

**Detection mechanism:** DFS reads from 0xFE80. If it receives a valid response (indicating 8271 command/status registers), it uses 8271 protocol. If it receives 0xFF (open bus from the write-only IC17 latch), it knows a WD1770 is fitted and uses the registers at 0xFE84-0xFE87.

The disc control register (IC17) is a write-only latch that provides:
- Drive/side/density selection
- Motor control
- WD1770 reset
- NMI enable gating

Since Beebium only emulates Model B+ with WD1770 (the only configuration ever manufactured), reading 0xFE80-0xFE83 returns 0xFF.

### NMI Handling

The WD1770 generates NMI via two signals that are OR'd together:
- **DRQ** (Data Request): Asserted when a byte is ready to be read or the controller is ready to accept a byte for writing
- **INTRQ** (Interrupt Request): Asserted when a command completes

From the B+ Service Manual:
> Two interrupt signals come from a 1770, pins 27 and 28. The two interrupts are inverted and wire NORed on to the notNMI line by two parts of IC7 (quad NAND gate).

#### 1MHz Clock Timing

**Critical:** The WD1770 operates at 1MHz, not the CPU's 2MHz clock. NMI state must only be updated on 1MHz clock edges (every other 2MHz cycle). This is implemented in `Machine::step()`:

```cpp
void step() {
    // ... tick CPU and VIAs ...

    // NMI handling - only update on 1MHz clock edges
    if ((state_.cycle_count & 1) == 0) {
        uint8_t nmi_mask = state_.memory.poll_nmi();
        M6502_SetDeviceNMI(&state_.cpu, kDiscNmiDeviceMask, nmi_mask ? 1 : 0);
    }

    ++state_.cycle_count;
}
```

If NMI is updated every 2MHz cycle, DRQ can toggle too rapidly: after the NMI handler reads the data register (clearing DRQ), the very next cycle would tick the WD1770 and set DRQ for the next byte, creating a new falling edge on /NMI before the handler completes RTI. This causes NMIs to stack up infinitely.

#### Inter-Byte Timing

At 250kbps MFM (double density), bytes arrive approximately every 64µs. The WD1770 implementation enforces this timing with a `byte_delay_` counter:

```cpp
// In tick_read_sector():
data_ = sector_buffer_[byte_counter_];
drq_ = true;
byte_delay_ = US_PER_BYTE;  // 64 ticks at 1MHz
```

The `tick()` method decrements `byte_delay_` and only advances the state machine when it reaches zero. This ensures the NMI handler has sufficient time (~64µs = ~128 CPU cycles) to complete before the next DRQ assertion.

#### NMI Gating (Bit 6)

The disc control register bit 6 is nominally an "NMI enable" bit. However, following the B2 emulator's approach, Beebium does **not** gate NMI via this bit because:

1. B2's `DiscInterfaceControl` struct has no NMI enable field
2. DFS does not appear to set bit 6 before disc operations
3. Real software works correctly without gating

The `poll_nmi()` implementation simply returns the WD1770's NMI state:

```cpp
uint8_t poll_nmi() {
    uint8_t nmi = disc_controller.nmi_pending() ? 0x01 : 0x00;
    disc_controller.tick();
    return nmi;
}
```

Note the ordering: NMI state is sampled **before** ticking the controller. This is critical for edge detection—after the CPU reads the DATA register (clearing DRQ), `poll_nmi()` returns 0. Then `tick()` may set DRQ for the next byte. On the next `poll_nmi()` call, we return 1, creating a clean 0→1 edge for the 6502's edge-triggered NMI detection.

## File Formats

### SSD (Single-Sided Disc)

Linear sector layout: Track 0 sectors, Track 1 sectors, etc.

| Variant | Tracks | Size |
|---------|--------|------|
| 40-track | 40 | 102,400 bytes |
| 80-track | 80 | 204,800 bytes |

**Sector offset:** `track * 10 * 256 + sector * 256`

### DSD (Double-Sided Disc)

Interleaved layout: Track 0 Side 0, Track 0 Side 1, Track 1 Side 0, etc.

| Variant | Tracks | Size |
|---------|--------|------|
| 40-track | 40 | 204,800 bytes |
| 80-track | 80 | 409,600 bytes |

**Sector offset:** `track * 2 * 10 * 256 + side * 10 * 256 + sector * 256`

### DFS Constants

- Sector size: 256 bytes
- Sectors per track: 10
- Tracks per side: 40 or 80

## Configuration

### Command-Line Options

```
beebium-model-b-plus --drive0 <filepath> --drive1 <filepath>
```

| Option | Description |
|--------|-------------|
| `--drive0 <filepath>` | Insert disc image into drive 0 |
| `--drive1 <filepath>` | Insert disc image into drive 1 |

**Write protection:** Determined by filesystem permissions. Read-only files appear as write-protected discs.

**Error handling:** Exits with error if file not found or format unrecognized.

### Programmatic Configuration

```cpp
ModelBPlus machine;

// Load and insert disc image
auto disc = FileDiscImage::load("game.ssd");
machine.memory().disc_drive_0.insert(std::move(disc));

// Or create in-memory disc for testing
auto disc = MemoryDiscImage::create_ssd();
machine.memory().disc_drive_0.insert(std::move(disc));
```

## Implementation Status

### Complete

- All WD1770 command types (I-IV)
- SSD/DSD format detection and I/O
- Step rate timing (6ms, 12ms, 20ms, 30ms)
- DRQ/INTRQ signal generation with proper timing
- Inter-byte timing (64µs between bytes at 250kbps MFM)
- 1MHz clock synchronization for NMI updates
- Model B+ hardware integration
- Command-line disc configuration

### Capability Gaps

See [Implementation Gaps](#implementation-gaps) section below for detailed list of unimplemented features that may affect compatibility with copy-protected software.

## Test Coverage

| Category | Tests | Assertions |
|----------|-------|------------|
| DiscGeometry | 20+ | Format detection, offset calculation |
| DiscImage | 9 | Sector I/O, write protection |
| DiscDrive | 15 | Head positioning, insertion/ejection |
| WD1770 | 73 | All command types, timing, status |
| NMI Aggregator | 7 | NMI signal aggregation |
| Integration | 12 | Model B+ register access, DFS *CAT command |

---

## Implementation Gaps

The following features are not implemented. They may be needed for compatibility with specific software:

### High Priority (if compatibility issues arise)

- **Index pulse generation**: I2 flag in Force Interrupt sets INTRQ immediately rather than waiting for index pulse (~200ms per revolution at 300 RPM).
- **Lost data detection**: Currently waits indefinitely for DRQ service. Real hardware sets LOST_DATA after ~64µs.

### Medium Priority

- **Motor spin-up timing**: Commands execute immediately; real WD1770 waits 6 revolutions (~1.2s).
- **Head load/settle timing**: 15ms or 30ms delay not implemented.
- **CRC error detection**: Sector CRC not verified on read.

### Low Priority (copy protection)

- **Write Track format parsing**: Format data stream not parsed for sync bytes, address marks.
- **Deleted data address mark**: RECORD_TYPE status bit not supported.
- **I0/I1 ready transition detection**: Rarely used by BBC software.

### Future Hardware

- **Intel 8271**: Required for Model B disc compatibility (uses 0xFE80-0xFE83 for command/status registers, completely different command protocol). Note: The Model B+ was designed to support 8271 but only WD1770 was ever fitted.
- **WD1772**: Faster step rates (2ms, 3ms, 6ms, 12ms).

---

## References

### Primary Sources

| Source | URL | Content |
|--------|-----|---------|
| WD1770 Datasheet | [PDF](https://cdn.hackaday.io/files/256641098008576/WD177x-00.pdf) | Official timing specifications |
| WD1772 Annotated | [PDF](http://info-coach.fr/atari/documents/_mydoc/WD1772-JLG.pdf) | Corrected diagrams/tables |
| Cloud9 WD1770 Docs | [HTML](https://www.cloud9.co.uk/james/BBCMicro/Documentation/wd1770.html) | Signal descriptions, timing |
| Stardot NMI Timing | [Forum](https://stardot.org.uk/forums/viewtopic.php?t=16114) | Real hardware measurements |
| Atari-Forum WD1772 | [Forum](https://www.atari-forum.com/viewtopic.php?t=27448) | Undocumented behaviors |

### Reference Implementations

| Emulator | Location | Notes |
|----------|----------|-------|
| B2 | `/Users/rjs/Code/b2/` | Primary architectural reference |
| MAME | [GitHub](https://github.com/mamedev/mame/blob/master/src/devices/machine/wd_fdc.h) | Comprehensive WD FDC family |
| BeebEm | `/Users/rjs/Code/beebem-mac/` | Both 8271 and WD1770 |
| B-Em | `/Users/rjs/Code/b-em/` | Timing constants |

### Timing Data

#### Step Rates

| Controller | Rate 0 | Rate 1 | Rate 2 | Rate 3 |
|------------|--------|--------|--------|--------|
| WD1770 | 6ms | 12ms | 20ms | 30ms |
| WD1772 | 2ms | 3ms | 6ms | 12ms |

#### Transfer Timing

- Byte transfer: ~64µs (MFM, 300 RPM)
- Command start delay: 16µs
- Settle time: 30ms (WD1770), 15ms (WD1772)
- Motor spin-up: 6 revolutions (~1.2s at 300 RPM)

#### NMI Timing (from real hardware tests)

| Source | X Register | Notes |
|--------|------------|-------|
| Real BBC B | 91-147 | Varies with disc state |
| jsbeeb/b-em/beebem | 216 | |
| MAME/B2 | 217 | |

# Interesting links

- [BBC Micro Disk Controllers](http://www.adsb.co.uk/bbc/disk_controllers/)
- 