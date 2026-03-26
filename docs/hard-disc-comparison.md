# Hard Disc Emulation Comparison

A survey of hard disc support across BBC Micro emulators (B-Em, b2, jsbeeb, beebjit, BeebEm) and hardware emulation projects (BeebSCSI, Pi1MHz), compared to Beebium.

## Support Overview

| Emulator | Hard Disc Support | Controllers | Image Formats |
|----------|------------------|-------------|---------------|
| B-Em | Yes | SCSI, IDE | DAT+DSC, HDF |
| b2 | No | - | - |
| jsbeeb | No | - | - |
| beebjit | No | - | - |
| BeebEm | Yes | SCSI, IDE, SASI | DAT+DSC |
| Beebium | No | - | - |
| **Hardware** | | | |
| BeebSCSI | Yes (hardware) | SCSI | DAT+DSC, DAT+CFG |
| Pi1MHz | Yes (hardware) | SCSI | DAT+DSC, DAT+CFG |

Only B-Em and BeebEm implement hard disc emulation in software. b2, jsbeeb, and beebjit are floppy-only (beebjit's README explicitly lists hard drives as out of scope).

Two hardware projects -- BeebSCSI and Pi1MHz -- provide high-quality SCSI emulation that plugs into a real BBC Micro's 1 MHz bus. Their implementations are more complete than the software emulators and serve as authoritative references for the Acorn SCSI host adapter protocol.

## Hard Disc Controllers

### Real Hardware Background

The BBC Micro's hard disc ecosystem was built around the 1 MHz expansion bus. The two main approaches were:

- **SCSI via Acorn ADFS board**: An Acorn-designed host adapter with a WD2793-based data separator, connected to a SASI/SCSI bridge (typically Adaptec ACB-4000) and then a standard SCSI hard drive. The host adapter presents a simplified SASI-like register interface at 0xFC40-0xFC43 on the FRED page.
- **IDE (later)**: Direct IDE register interface at 0xFC40-0xFC47, used by various third-party boards.

Both share the same base address (0xFC40) on the FRED page and are mutually exclusive -- a machine would have one or the other, never both.

A third variant existed: the **Torch SASI controller** at `0xFDF0-0xFDF3` on the JIM page, used with Torch Z80 Communicator systems. Despite being associated with the Z80 coprocessor, this controller sits on the **host BBC Micro's 1 MHz bus** -- the Z80 parasite has no independent storage hardware and accesses discs indirectly through a non-standard Tube-like communication link to the host. The Torch SASI controller talked to a Xebec S1410 bridge board connected to a physical Winchester drive (10 or 20 MB).

### Controller Comparison

| Feature | B-Em SCSI | B-Em IDE | BeebEm SCSI | BeebEm IDE | BeebEm SASI |
|---------|-----------|----------|-------------|------------|-------------|
| Max drives | 4 (LUN 0-3) | 2 | 4 (LUN 0-3) | 4 | 1 |
| Sector size | 256 bytes | 256 bytes (512 padded) | 256 bytes | 256 bytes | 256 bytes |
| I/O base | 0xFC40 | 0xFC40 | 0xFC40 | 0xFC40 | 0xFC40 |
| Registers | 4 (+00 to +03) | 9 (+00 to +08) | 4 (+00 to +03) | 8 (+00 to +07) | 4 (+00 to +03) |
| Protocol | Phase-based bus | ATA registers | Phase-based bus | ATA registers | Phase-based bus |
| I/O page | FRED | FRED | FRED | FRED | JIM |
| Machine target | Model B, Master | Model B, Master | Model B, Master | Model B, Master | Torch Z80 host |

## SCSI Command Sets

The software emulators implement minimal command sets sufficient for ADFS, while the hardware projects (BeebSCSI, Pi1MHz) implement substantially more complete SCSI-1 compliance.

| Command | Code | B-Em | BeebEm | BeebSCSI | Pi1MHz | Purpose |
|---------|------|------|--------|----------|--------|---------|
| TEST UNIT READY | 0x00 | Yes | Yes | Yes | Yes | Check drive presence |
| REZERO UNIT | 0x01 | - | Yes (SASI) | Yes | Yes | Seek to track 0 |
| REQUEST SENSE | 0x03 | Yes | Yes | Yes | Yes | Error status |
| FORMAT UNIT | 0x04 | Yes | Yes | Yes | Yes | Initialise disc |
| REASSIGN BLOCKS | 0x07 | - | - | - | Yes | Defect mapping |
| READ (6) | 0x08 | Yes | Yes | Yes | Yes | Read sectors (21-bit LBA) |
| VERIFY | 0x09 | - | Yes (SASI) | - | - | Verify sectors |
| WRITE (6) | 0x0A | Yes | Yes | Yes | Yes | Write sectors (21-bit LBA) |
| SEEK | 0x0B | - | Yes (SASI) | Yes | Yes | Seek to block |
| SET GEOMETRY | 0x0C | - | Yes (SASI) | - | - | Configure geometry |
| TRANSLATE | 0x0F | Yes | - | Yes | Yes | LBA to CHS translation |
| INQUIRY | 0x12 | - | - | - | Yes | Device identification |
| MODE SELECT (6) | 0x15 | Yes | - | Yes | Yes | Set device parameters |
| MODE SENSE (6) | 0x1A | Yes | - | Yes | Yes | Read device parameters |
| START/STOP UNIT | 0x1B | Yes | - | Yes | Yes | Spindle control |
| SEND DIAGNOSTIC | 0x1D | - | - | - | Yes | Self-test |
| READ CAPACITY | 0x25 | - | - | - | Yes | Query disc size |
| VERIFY (10) | 0x2F | Yes | Yes | Yes | Yes | Range check |
| READ DEFECT DATA | 0x37 | - | - | - | Yes | Defect list |
| RAM DIAGNOSTICS | 0xE0 | - | Yes (SASI) | - | - | Self-test |
| CONTROLLER DIAG | 0xE4 | - | Yes (SASI) | - | - | Self-test |
| *Vendor-specific* | 0x10-0x14 | - | - | Yes | Yes | BeebSCSI extensions |

The minimum viable set for ADFS operation is: TEST UNIT READY (0x00), REQUEST SENSE (0x03), READ (0x08), and WRITE (0x0A). However, MODE SELECT (0x15) and MODE SENSE (0x1A) are needed for geometry configuration, and FORMAT (0x04) is needed for disc initialisation.

## IDE Command Sets

| Command | Code | B-Em | BeebEm | Purpose |
|---------|------|------|--------|---------|
| RESTORE | 0x10 | Yes | - | Seek to cylinder 0 |
| READ SECTOR(S) | 0x20 | Yes | Yes | Read sector data |
| WRITE SECTOR(S) | 0x30 | Yes | Yes | Write sector data |
| READ VERIFY | 0x40 | Yes | - | Verify readable |
| FORMAT TRACK | 0x50 | Yes | - | Format track |
| SET PARAMETERS | 0x91 | Yes | Yes | Configure geometry |
| SEEK | 0x70 | Yes | - | Seek to cylinder |
| IDLE | 0xE3 | Yes | - | Power management |
| IDENTIFY DEVICE | 0xEC | Yes | - | Drive identification |

## SCSI Bus Protocol

Both B-Em and BeebEm implement a phase-based SCSI/SASI bus protocol. The bus transitions through phases in sequence:

```
BUS FREE → SELECTION → COMMAND → EXECUTE → DATA (Read/Write) → STATUS → MESSAGE → BUS FREE
```

### Bus Signals

| Signal | Direction | Purpose |
|--------|-----------|---------|
| BSY | Target→Host | Target is active |
| SEL | Host→Target | Target selection |
| CD | Target→Host | Command (1) vs Data (0) |
| IO | Target→Host | Input to host (1) vs Output from host (0) |
| MSG | Target→Host | Message phase active |
| REQ | Target→Host | Target ready for transfer |
| IRQ | Target→Host | Interrupt request |

### Register Interface (0xFC40-0xFC43)

| Offset | Read | Write |
|--------|------|-------|
| +00 | Data register | Data register |
| +01 | Status (CD, IO, MSG, BSY, REQ, IRQ) | - |
| +02 | - | Select register |
| +03 | - | IRQ enable |

The status register encodes bus phase via CD, IO, and MSG bits:

| CD | IO | MSG | Phase |
|----|----|----|-------|
| 1 | 0 | 0 | Command |
| 0 | 1 | 0 | Data In (read) |
| 0 | 0 | 0 | Data Out (write) |
| 1 | 1 | 0 | Status |
| 1 | 1 | 1 | Message |

## Hard Disc Image Formats

### DAT + DSC (SCSI/SASI)

Used by both B-Em and BeebEm for SCSI and SASI drives.

**DAT file**: Raw sector data. Each sector is 256 bytes, stored sequentially at offset `LBA * 256`. No header, no metadata, no container structure.

**DSC file**: 22-byte binary geometry descriptor.
- Bytes 13-14: Cylinder count (little-endian)
- Byte 15: Head count
- Sectors per track: fixed at 33
- Total capacity: `cylinders * heads * 33 * 256` bytes

Example (BeebEm scsi0.dsc): 306 cylinders, 4 heads = 40,392 sectors = ~9.9 MB

If no DSC file is present, geometry is auto-calculated from the DAT file size.

**Format detection**: B-Em checks for a "Hugo" signature at offset 0x200 (standard ADFS) or a null-padded "Hugo" at offset 0x400 (IDE-padded format) to distinguish between SCSI and IDE sector layouts.

### HDF (IDE)

Used by B-Em only for IDE drives.

**HDF file**: Raw IDE disc image with 256-byte sectors. Sector addressing: `(cylinder * heads_per_cylinder + head) * sectors_per_track + sector) * 256`.

Default geometry: 101 cylinders, 16 heads, 63 sectors/track. Configurable via the ATA SET PARAMETERS (0x91) command.

### Pre-formatted Images

| Emulator | Type | Drives | Sizes |
|----------|------|--------|-------|
| B-Em | SCSI (DAT) | scsi0-scsi3 | ~10 MB each |
| B-Em | IDE (HDF) | hd4, hd5 | Variable |
| BeebEm | SCSI (DAT) | scsi0-scsi3 | ~10 MB (scsi0), stubs (scsi1-3) |
| BeebEm | IDE (DAT) | ide0-ide3 | ~4 MB each |
| BeebEm | SASI (DAT) | sasi0 | 16 MB |

### Image Creation

B-Em includes an `hdfmt` utility that creates ADFS-formatted hard disc images:
- Creates paired DAT + DSC files
- Initialises ADFS root directory ("Hugo" signature)
- Initialises free space map (sectors 0-4)
- Auto-adjusts size to fit geometry (33 sectors/track, 4 heads)
- Usage: `hdfmt <basename> <size>` (supports k/m/g suffixes)

BeebEm provides pre-formatted images but no creation tool.

## Filing System Requirements

Hard disc access on the BBC Micro requires ADFS (Advanced Disc Filing System). Different ADFS ROM versions support different controllers:

| ROM | Version | Controller | Machine |
|-----|---------|------------|---------|
| ADFS 1.30 | SCSI | Acorn SCSI | Model B |
| ADFS 1.33 | IDE | IDE board | Model B |
| ADFS 1.50 | SCSI | Acorn SCSI | Master 128 |
| ADFS 1.53 | IDE | IDE board | Master 128 |
| HADFS 5.30+ | IDE | IDE board | Various |

Hard disc drives appear as ADFS drives 0-3, with floppy drives at 4-5.

## 1 MHz Bus Architecture

The hard disc controllers sit on the BBC Micro's 1 MHz expansion bus, which provides access to two I/O pages:

| Page | Address | Name | Purpose |
|------|---------|------|---------|
| FRED | 0xFC00-0xFCFF | Fast Read/Expand Data | Peripheral control registers |
| JIM | 0xFD00-0xFDFF | Joint I/O Memory | Paged data memory window |

### Bus Timing

All accesses to FRED and JIM are stretched to 1 MHz timing (1 microsecond per access), regardless of the CPU's native clock speed. This is important for cycle-accurate emulation -- the CPU stalls during 1 MHz bus accesses.

### Address Allocation

The SCSI/IDE controller uses only FRED page addresses:

| Address | Peripheral |
|---------|------------|
| 0xFC00-0xFC03 | ExtMem paging (b2 Opus Challenger) |
| 0xFC08-0xFC0F | Acorn Speech chip (TMS5220) |
| 0xFC10-0xFC13 | Teletext adapter |
| 0xFC18-0xFC1F | IEEE-488 |
| 0xFC20-0xFC27 | Music 5000 / BeebOPL |
| 0xFC28-0xFC2F | Joystick (some boards) |
| 0xFC30-0xFC3F | Rombox / User |
| **0xFC40-0xFC47** | **Hard disc (SCSI or IDE)** |
| 0xFC48-0xFC4F | Reserved |
| 0xFCF8-0xFCFF | Opus Challenger disc controller |

JIM (0xFD00-0xFDFF) is used as a 256-byte data window by some peripherals (e.g. Opus Challenger's sideways RAM), but the standard SCSI/IDE controllers do not use it -- all data transfer happens byte-by-byte through the data register at 0xFC40.

## Data Transfer Mechanism

Both SCSI and IDE use programmed I/O (PIO). There is no DMA on the BBC Micro's 1 MHz bus.

**SCSI**: The host adapter uses REQ/ACK handshaking. The CPU reads/writes one byte at a time from the data register (0xFC40), with the REQ signal in the status register indicating when the next byte is ready. ADFS typically transfers one 256-byte sector per SCSI READ/WRITE command, though the protocol supports up to 256 sectors per command.

**IDE**: The CPU reads/writes bytes from the data register after checking the DRQ (Data Request) bit in the status register. B-Em's implementation uses a split data register: even bytes at 0xFC40, odd bytes at 0xFC48 (16-bit word split across two 8-bit registers). BeebEm uses a simpler byte-streaming model through 0xFC40 alone.


## Hardware Reference Implementations

### BeebSCSI

[BeebSCSI](https://github.com/simoninns/BeebSCSI) is a hardware device that plugs into a real BBC Micro's 1 MHz bus and emulates an Acorn SCSI host adapter. It uses a Xilinx XC9572XL CPLD for bus interface logic and an Atmel AT90USB1287 AVR for SCSI command processing, with disc images stored on an SD card.

#### Architecture

The CPLD/AVR split maps cleanly onto how a software emulator should be structured:

- **CPLD** (bus interface): Address decoding for 0xFC40-0xFC44, data bus buffering, status register output, signal conditioning. In a software emulator, this is the memory-mapped I/O handler registered on the FRED page.
- **AVR** (SCSI logic): Phase state machine, command dispatch, sector read/write against SD card storage. In a software emulator, this is the SCSI controller class.

#### Register Interface

| Address | Read | Write |
|---------|------|-------|
| 0xFC40 | SCSI data bus | SCSI data bus |
| 0xFC41 | Status (MSG, BSY, REQ, I/O, C/D) | - |
| 0xFC42 | - | Assert nSEL (selection) |
| 0xFC43 | - | IRQ enable/disable |

The status register bit layout:

| Bit | Signal | Meaning |
|-----|--------|---------|
| 7 | CND | Command/Data |
| 6 | INO | Input/Output direction |
| 5 | REQ | Request (ready for transfer) |
| 4 | IRQ | Interrupt pending |
| 1 | BSY | Busy |
| 0 | MSG | Message phase |

All signals follow inverted (active-low) logic per the SCSI standard.

#### Image Format

BeebSCSI uses the same DAT+DSC format as B-Em and BeebEm, stored on a FAT-formatted SD card:

```
/BeebSCSI0/          <-- Jukebox directory 0 (up to 8 directories)
    scsi0.dat        <-- LUN 0 raw image (256-byte sectors)
    scsi0.dsc        <-- LUN 0 geometry descriptor (22 bytes)
    scsi0.cfg        <-- LUN 0 extended config (optional, text-based)
    ...
    scsi7.dat        <-- Up to 8 LUNs per directory
```

The CFG file is a BeebSCSI extension that provides richer geometry and SCSI mode page configuration than the 22-byte DSC format. It can specify custom sectors-per-track values (e.g. RLL drives with 61 SPT vs the standard MFM 33 SPT).

#### DSC Format Detail

The 22-byte DSC descriptor follows the SCSI MODE SELECT parameter format:

| Offset | Length | Field |
|--------|--------|-------|
| 0-3 | 4 | Mode Select Parameter List header |
| 4-11 | 8 | Extent Descriptor (block size at bytes 9-11, always 0x000100 = 256) |
| 12 | 1 | List format code (1) |
| 13-14 | 2 | Cylinder count (big-endian) |
| 15 | 1 | Head count |
| 16-17 | 2 | Reduced write current cylinder |
| 18-19 | 2 | Write pre-compensation cylinder |
| 20 | 1 | Landing zone position |
| 21 | 1 | Step pulse output rate code |

Sectors per track is fixed at 33 (ACB-4000 SuperForm 2:1 interleave). Total capacity = `cylinders * heads * 33 * 256` bytes. Maximum 512 MB per LUN (ADFS 21-bit LBA limit).

If no DSC file exists, geometry is auto-generated from the DAT file size.

#### Noteworthy Implementation Details

- **Auto-start behaviour**: Unlike the SCSI spec, ADFS never sends START/STOP UNIT commands. BeebSCSI auto-starts LUN 0 on mount and auto-starts any LUN on first READ/WRITE. A software emulator should do the same.
- **Jukebox switching**: Multiple sets of disc images can be swapped at runtime via a vendor-specific SCSI command (0x11), without rebooting the host. This is analogous to mounting different images via a gRPC service call.
- **8 LUNs per directory**: More than B-Em (4) or BeebEm (4). ADFS itself supports drives 0-7.
- **LaserDisc mode**: The same firmware also emulates a Philips VP415 LaserVision player for the BBC Domesday project, with vendor-specific F-Code commands. This demonstrates the versatility of the SCSI command framework.
- **Source quality**: Well-documented C code with clear phase state machine (`scsi.c`, ~2400 lines) and filesystem abstraction (`filesystem.c`, ~2000 lines). The SCSI state machine is the best reference for understanding the Acorn host adapter protocol.

### Pi1MHz

[Pi1MHz](https://github.com/dp111/Pi1MHz) uses a Raspberry Pi to simultaneously emulate multiple BBC Micro 1 MHz bus peripherals. Its SCSI emulation is derived from BeebSCSI and shares the same image formats.

#### Multi-Peripheral Emulation

Pi1MHz emulates many 1 MHz bus devices concurrently:

| Peripheral | Address | Notes |
|-----------|---------|-------|
| Hard disc (SCSI) | 0xFC40-0xFC44 | BeebSCSI-derived |
| Expansion RAM (JIM) | 0xFCFD-0xFCFF | Up to 992 MB on Pi 3B+ |
| Expansion RAM (byte) | 0xFC00-0xFC03 | 16 MB byte-mode |
| Music 5000/3000 | 0xFC4x | Audio synthesis |
| SD card/FAT access | 0xFCD6 | File I/O buffer |
| Framebuffer | 0xFCA0 | HDMI video output |

This is relevant to Beebium's architecture: a pluggable FRED/JIM peripheral bus should support multiple devices at different address ranges simultaneously, just as Pi1MHz does.

#### SCSI Implementation

Pi1MHz's SCSI code is a port of BeebSCSI's AVR firmware to Linux/bare-metal Pi. The key differences:

- **More SCSI commands**: Adds INQUIRY (0x12), READ CAPACITY (0x25), REASSIGN BLOCKS (0x07), SEND DIAGNOSTIC (0x1D), and READ DEFECT DATA (0x37) beyond the BeebSCSI set.
- **Stateless polling**: Uses a non-blocking state machine registered via `Pi1MHz_Register_Poll()`. Each call advances the SCSI state by one step and returns. This is analogous to a `tick()` method in Beebium's architecture.
- **16 KB sector buffer**: Reduces SD card I/O overhead by buffering multiple sectors.
- **FatFS fast-seek**: Pre-builds cluster lookup tables for disc images to avoid worst-case FAT traversal during random access.
- **16 concurrent LUNs**: 0-7 for ADFS, 8-15 for VFS (LaserDisc). Each has independent file handles and geometry.

#### Image Format

Identical to BeebSCSI: DAT+DSC on a FAT filesystem, with optional CFG files for extended configuration. Same 512 MB per-LUN limit (ADFS constraint).

Same jukebox directory structure (`/BeebSCSI0/` through `/BeebSCSI7/`) with runtime switching via vendor-specific SCSI command.

## Implementation Complexity Comparison (Updated)

| Aspect | B-Em | BeebEm | BeebSCSI | Pi1MHz |
|--------|------|--------|----------|--------|
| SCSI source | ~900 lines | ~900 lines | ~2400 lines | ~2400 lines (ported) |
| IDE source | ~340 lines | ~260 lines | - | - |
| SCSI commands | 10 | 10 (+ 6 SASI) | 14 + vendor | 18 + vendor |
| Max LUNs | 4 | 4 | 8 | 16 |
| Image format | DAT+DSC | DAT+DSC | DAT+DSC+CFG | DAT+DSC+CFG |
| Heritage | BeebEm port | Original | Original | BeebSCSI port |

The software emulators (B-Em, BeebEm) implement the minimum needed to make ADFS work. The hardware projects (BeebSCSI, Pi1MHz) implement substantially more, driven by the need to work with a wider range of ADFS versions and third-party utilities that probe for additional SCSI capabilities.

For Beebium, the BeebSCSI SCSI state machine is the most authoritative reference. Its clear separation of bus interface, command processing, and filesystem access maps directly onto Beebium's component architecture.

## iSCSI as a Storage Backend

An interesting possibility for Beebium is supporting iSCSI (SCSI commands over TCP) as a storage backend, alongside conventional disc image files. This would allow a Beebium emulator instance to connect to real or virtual storage over the network -- a capability no other BBC Micro emulator offers.

### Why iSCSI Fits Beebium's Architecture

Beebium's multi-process, gRPC-based architecture already separates the emulation core from frontends via network protocols. Extending this philosophy to storage is a natural step:

```
6502 ADFS driver
  |
emulated Acorn SCSI host adapter (0xFC40-0xFC43)
  |
virtual SCSI target interface
  |
  +-- Image backend (DAT file)        <-- default, safe, portable
  +-- iSCSI backend (TCP to target)   <-- optional, for real/networked storage
  +-- Debug backend (logs commands)    <-- for development
```

The critical design principle: **the emulator speaks "abstract SCSI target", not "real storage"**. The Acorn SCSI host adapter emulation and bus phase state machine are always inside Beebium. Only the storage backend varies.

### What iSCSI Is (and Isn't)

iSCSI is not raw SCSI hardware passthrough. It encapsulates SCSI commands (CDBs) inside TCP packets. This means:

- No special hardware needed -- it works over standard networking
- No privileged access required (unlike raw `/dev/sdX` access)
- The emulator never touches physical SCSI buses
- Authentication, retries, and transport are handled by the iSCSI layer

This is fundamentally different from trying to pass through raw SCSI commands to physical devices, which would require platform-specific privileged APIs and is not practically viable.

### Host OS iSCSI Support

| Platform | iSCSI Initiator | Status |
|----------|----------------|--------|
| Linux | `open-iscsi` (kernel) | Excellent; block devices appear automatically |
| Windows | Built-in initiator | Mature, reliable |
| macOS | No native initiator | Third-party only (ATTO, globalSAN); workable but not seamless |

Two implementation approaches:

1. **OS-level initiator**: The host OS connects to the iSCSI target and presents a block device. Beebium opens the block device as a raw file. Simple but requires OS-level setup and may need elevated privileges.

2. **User-space iSCSI in Beebium**: Beebium implements a minimal iSCSI initiator directly, translating the emulated SCSI commands into iSCSI PDUs over TCP. More self-contained but more implementation work. Since the Acorn SCSI command set is tiny (READ, WRITE, REQUEST SENSE, and a handful of others), the iSCSI framing needed is minimal.

### Use Cases

- **Shared storage between emulator instances**: Multiple Beebium servers could mount the same iSCSI target, emulating networked BBC Micros sharing a fileserver's hard disc
- **Large storage pools**: iSCSI targets can be backed by anything -- ZFS volumes, cloud block storage, NAS appliances -- far exceeding what local disc images offer
- **Classroom/museum setups**: A single iSCSI target server could serve pre-configured hard disc images to many Beebium instances
- **Real hardware integration**: Users with actual SCSI drives could expose them via an iSCSI target, keeping the "danger" outside Beebium

### Architectural Recommendation

iSCSI support should be an optional backend behind the same virtual SCSI target interface used by disc images. The interface boundary is the SCSI CDB: the host adapter emulation produces CDBs, and the backend consumes them. Whether the backend reads from a DAT file or forwards over TCP is invisible to the emulated BBC Micro.

This could be exposed via gRPC as a target type when mounting a hard disc:

```
// Mount a disc image (default)
MountHardDisc(drive: 0, url: "file:///path/to/scsi0.dat")

// Mount an iSCSI target
MountHardDisc(drive: 0, url: "iscsi://target-host/iqn.2026-03.org.beebium:disc0")
```

### Priority

iSCSI is not needed for an initial hard disc implementation. The recommended approach is:

1. Implement the virtual SCSI target interface with a disc image backend (DAT+DSC)
2. Ensure the interface boundary is clean (CDB in, data out)
3. Add iSCSI as a second backend later, once the SCSI emulation is proven

The important thing is to design the target interface with this future in mind, so that adding iSCSI doesn't require restructuring the SCSI emulation layer.

## BBC Master AIV and LaserDisc Support

The BBC Master AIV (Advanced Interactive Video) system used SCSI to control a Philips VP415 LaserDisc player for the BBC Domesday Project. This is relevant to Beebium's SCSI design because the VP415 appears as a SCSI target on the same bus as hard discs, using the same host adapter protocol but with additional vendor-specific commands.

### AIV Host Adapter

The AIV SCSI Host Adapter is an **internal** version of the standard Acorn SCSI Host Adapter, connected to the Master 128's internal 1 MHz bus (PL12 connector) rather than the external 1 MHz bus. It uses the same register addresses (0xFC40-0xFC43) and the same bus phase protocol, but has several hardware differences:

| Aspect | Standard (External) Adapter | AIV (Internal) Adapter |
|--------|---------------------------|----------------------|
| Bus levels | TTL (external 1 MHz bus) | CMOS (internal 1 MHz bus) |
| Address decoder | 74LS138 / 74HCT138 | 74HC138 |
| Data bus inversion | Hardware (IC13 74LS240 inverts outbound data) | Software (VFS performs inversion) |
| IRQ flag polarity | Non-inverted | Inverted (missing inverter on board) |
| Status bit 2 | Grounded (unused) | Connected to SCSI RESET pin |
| Address bus width | 8-bit | 4-bit |

The cards are not electrically interchangeable (CMOS vs TTL), but the SCSI protocol is identical. The software differences (data bus inversion, IRQ polarity) are handled by the VFS ROM rather than by hardware.

### Design Implication for Beebium

The AIV adapter differences mean the host adapter emulation should not hard-code data inversion or IRQ polarity. These should be configurable (or handled entirely by the ROM software, as on real hardware). Since both adapters present the same register interface at the same address, a single SCSI host adapter implementation should suffice -- the AIV quirks are compensated for in the VFS ROM, not in the adapter hardware.

### VFS (Video Filing System) ROM

VFS version 1.70 is essentially a read-only variant of ADFS with extensions for VP415 control. It provides:
- Standard BBC filing system interface for reading data from the LV-ROM disc
- F-code commands transmitted to the VP415 via SCSI Group 6 vendor-specific commands
- Software data bus inversion (compensating for the AIV hardware difference)
- Software IRQ polarity inversion

### VP415 on the SCSI Bus

The VP415 appears as a SCSI target alongside any hard discs. The bus topology uses no arbitration (the simple Acorn host adapter does not support it):

- **Host (initiator) ID**: 1 (0x02 on bus)
- **ADFS hard discs**: LUN 0-3
- **VFS LaserDisc**: LUN 0-7

The VP415 is addressed via standard SCSI READ (0x08) for data retrieval and Group 6 vendor-specific commands (0xC8/0xCA) for F-code control of the player.

### VP415 SCSI Commands

Beyond the standard SCSI commands used for hard discs, the VP415 requires two additional Group 6 (vendor-specific) commands:

| Opcode | Command | Direction | Purpose |
|--------|---------|-----------|---------|
| 0xC8 | Read F-Code | DATA IN | Read buffered response from VP415 |
| 0xCA | Write F-Code | DATA OUT | Send F-code command to VP415 |

F-code data is transferred in 256-byte buffers. Commands are ASCII strings terminated by CR (0x0D), null-padded. The VP415 F-code set provides comprehensive playback control:

| Category | Example F-Codes |
|----------|----------------|
| Playback | `N` (play), `O` (reverse), `*` (halt), `/` (pause) |
| Speed | `W` (fast forward), `Z` (fast reverse), `U`/`V` (slow motion) |
| Navigation | `FnnnnN` (go to frame, play), `FnnnnR` (go to frame, halt) |
| Audio | `A0`/`A1` (channel 1), `B0`/`B1` (channel 2) |
| Video | `E0`/`E1` (video off/on), `VP1`-`VP5` (overlay modes) |
| Status queries | `?F` (frame number), `?D` (disc status), `?P` (player status) |
| System | `:` (reset), `'` (eject), `,0`/`,1` (standby/load) |

BeebSCSI (designed by Simon Inns, who also leads the Domesday86 recovery project) implements the full VP415 F-code set in its LV-DOS emulation mode, making it the authoritative reference for this protocol.

### Implications for Beebium's SCSI Architecture

The VP415 support reinforces several design decisions:

1. **The SCSI target interface must be generic**, not hard-disc-specific. A VP415 target handles Group 6 commands and F-codes rather than sector read/write. The CDB dispatch should be extensible.

2. **Multiple target types on one bus**: The SCSI bus must support mixed target types (hard disc + LaserDisc player) simultaneously, each with their own command handling.

3. **Group 6 vendor-specific commands must not be rejected**: The command dispatcher should pass unrecognised opcodes to the target implementation rather than returning CHECK CONDITION. Different targets handle different command groups.

4. **The host adapter is the stable part**: Both the standard and AIV adapters present the same register interface. The adapter emulation should be a thin layer; all intelligence lives in the targets.

5. **BeebSCSI is the single best reference**: Simon Inns' implementation handles both ADFS hard discs and VP415 LaserDisc through the same SCSI bus abstraction, which is exactly the architecture Beebium should follow.

### References

- [Domesday86: Acorn BBC Master AIV](https://www.domesday86.com/?page_id=67)
- [Domesday86: Philips VP415](https://www.domesday86.com/?page_id=316)
- [Domesday86: VFS ROM](https://www.domesday86.com/?page_id=70)
- [Domesday86: Acorn AIV SCSI Adapter](https://www.domesday86.com/?page_id=64)
- [BeebSCSI source (scsi.c, fcode.c)](https://github.com/simoninns/BeebSCSI)
- [Acorn AIV SCSI Adapter Card KiCad reproduction](https://github.com/simoninns/Acorn-AIV-SCSI-Adapter-Card)
- [VP415 Emulator](https://github.com/simoninns/VP415Emu)

## Pre-formatted Hard Disc Images

[Jon Ripley's BBC Micro Hard Drives page](https://jonripley.com/8bit/HardDrives/) provides 22 blank, pre-formatted ADFS hard disc images ranging from 2 MB to 512 MB. These are raw ADFS old-map format (256-byte sectors, "Hugo" root directory marker) distributed as `.adl` files inside zip archives. They contain no software -- just an empty root directory and free space map.

These images are not directly compatible with existing emulators due to naming conventions (emulators expect `scsiN.dat` + `scsiN.dsc` pairs), but the raw disc data is identical in format. Renaming `2MbHDD.adl` to `scsi0.dat` and allowing the emulator to auto-generate a DSC geometry file is sufficient.

The images are exact power-of-two megabyte sizes, which are not evenly divisible by the standard SCSI geometry stride (33 sectors/track * heads * 256 bytes). This wastes a few KB at the end of the image but is otherwise harmless, since the ADFS free space map records the actual disc size in sectors independently of the CHS geometry.

These images are useful for testing a Beebium SCSI implementation without needing to format discs from within the emulator. They cover the full range of ADFS-supported sizes up to the 512 MB limit.

## Beebium's Current State

Beebium has the foundation for 1 MHz bus peripherals but no hard disc support:

- **FRED/JIM region** (0xFC00-0xFDFF) is mapped in all hardware variants (`ModelBHardware`, `ModelBPlusHardware`, `ModelBRomRamBoardHardware`)
- Currently returns 0xFF on reads (open bus -- 74LS245 transceiver behaviour)
- A `FredJimRegion` class exists with a TODO comment: "Expand to a socket pattern supporting pluggable devices (speech synthesizers, music co-processors, etc.)"
- **Bus stretching** is implemented (`BusStretching.hpp`) -- 1 MHz timing for the I/O region is already handled
- The **disc controller socket pattern** (`DiscControllerSocket`/`DiscControllerInterface`) demonstrates how pluggable peripherals can be added at runtime

## Recommendations for Beebium

### Architecture

1. **Extend FredJimRegion into a pluggable peripheral bus**. The existing socket pattern used for disc controllers (`DiscControllerSocket`) provides a proven model. Create a `FredPeripheralSocket` that allows devices to claim address ranges within the FRED page and register read/write handlers. Pi1MHz demonstrates that multiple peripherals at different FRED addresses should coexist cleanly.

2. **Start with SCSI, not IDE**. SCSI was the standard Acorn hard disc interface and is what ADFS expects. The protocol is well-documented and all four reference implementations agree on the register interface and bus protocol.

3. **Use the DAT+DSC format** for compatibility with existing hard disc images from B-Em, BeebEm, BeebSCSI, and Pi1MHz. The format is trivially simple (raw sectors + a small geometry file) and is the de facto standard across the entire ecosystem.

4. **Use BeebSCSI's scsi.c as the primary reference**. It is the most complete and best-documented SCSI implementation, with clean separation between bus interface, command processing, and storage. Its CPLD/AVR split maps naturally onto Beebium's I/O handler / controller class separation.

### Minimum Viable Implementation

A functional SCSI hard disc requires:
- A SCSI host adapter device registered at 0xFC40-0xFC43
- Bus phase state machine (BUS FREE → SELECTION → COMMAND → DATA → STATUS → MESSAGE)
- Six SCSI commands: TEST UNIT READY (0x00), REQUEST SENSE (0x03), FORMAT (0x04), READ (0x08), WRITE (0x0A), MODE SENSE (0x1A)
- DAT file loading with sector-level read/write
- Auto-start LUN 0 on mount (ADFS never sends START/STOP UNIT)
- An ADFS ROM in a sideways ROM slot

### Estimated Scope

Based on the reference implementations, a SCSI controller is approximately 600-900 lines of C++ for a B-Em/BeebEm-level implementation, or up to 1500 lines for BeebSCSI-level completeness. The peripheral bus infrastructure (making FRED/JIM pluggable) is the larger architectural task but benefits all future 1 MHz bus peripherals (speech chip, Music 5000, Econet clock, etc.).

### Disc Geometry

All implementations agree on the standard geometry:
- 256-byte sectors (fixed, Acorn SCSI standard)
- 33 sectors per track (ACB-4000 SuperForm 2:1 interleave)
- Variable heads and cylinders from DSC descriptor
- Maximum 512 MB per LUN (ADFS 21-bit LBA limit: 2,097,151 sectors)

### gRPC Service Architecture

The current `DiscService` manages the WD1770 floppy controller. Adding SCSI hard discs, LaserDisc players, and potentially other 1 MHz bus peripherals raises the question of how to structure the gRPC service layer.

#### The Naming Problem

Neither domain-driven naming (FloppyDiscService, HardDiscService) nor capability-driven naming (FixedDiscService, RemovableDiscService) scales well:

- A VP415 LaserDisc player is a SCSI target but not a disc drive at all
- An Iomega Jaz drive would be a removable SCSI disc, breaking the fixed/removable split
- "Hard disc" conflates the media type with the controller type (SCSI)
- The floppy controller (WD1770 on SHEILA) and SCSI adapter (on FRED) are fundamentally different hardware on different buses

#### Follow the Hardware Topology

The gRPC services should mirror the actual hardware tree rather than classifying by media type:

```
Machine
  ├─ FloppyControllerService          (WD1770 at 0xFE80/0xFE84)
  │    Operations: insert/eject disc image, query drive status
  │
  ├─ OneHMzBusService                 (FRED/JIM, 0xFC00-0xFDFF)
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

#### Service Lifecycle and Hardware Presence

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

#### Future: Dynamically Loaded Peripheral Plugins

To avoid baking all possible 1 MHz bus peripherals into the Beebium server executables, a plugin architecture could allow peripherals (e.g. Music 5000, speech chip) to be loaded at runtime. Plugins would use a C API (not C++) to minimise ABI compatibility issues and ease implementation in other languages.

A key question is whether a plugin could define a gRPC service served by the existing Beebium gRPC server. gRPC C++ does **not** support adding services to a running server -- `ServerBuilder::RegisterService()` must be called before `BuildAndStart()`, and there is no public API to register services afterward. Three approaches are viable:

**Approach A: Generic service with dynamic dispatch.** A `CallbackGenericService` is registered at server startup as a catch-all for RPCs that don't match any built-in service. It receives the full method name (e.g. `/beebium.Music5000Service/SetWaveform`) and raw serialized bytes. Plugins register handlers into a mutable dispatch table keyed by method name. The plugin handles its own protobuf serialization via the C API (receiving/returning `uint8_t*` + length), which avoids C++ ABI coupling entirely. Limitations: gRPC server reflection will not automatically list plugin services; all RPCs are modelled as bidirectional streams at the framework level (unary RPCs are emulated as single-message streams).

**Approach B: Server restart on plugin load.** When a plugin is loaded, the gRPC server is shut down, rebuilt with all existing services plus the plugin's service, and restarted on the same port. This provides full typed service support and working reflection, but severs all active streams during the restart. Acceptable if plugins are loaded only at machine configuration time (not mid-emulation). Beebium clients already handle server reconnection.

**Approach C: Plugin runs its own gRPC server.** Each plugin starts a separate gRPC server on its own port. Beebium already uses this pattern: `ParasiteServer` runs an independent gRPC server for Tube co-processor debugging. Service discovery would need extending to advertise plugin server ports alongside the main server.

For the C plugin API, Approach A is the most natural fit: the plugin exports C functions that receive and return serialized protobuf bytes, completely sidestepping C++ ABI and gRPC framework dependencies. The main server's generic service handles gRPC transport; the plugin handles message content. The plugin would ship its own `.proto` file for client-side code generation, but the server side is just raw bytes through the C API.

**Recommended approach: unified extension registry with pre-start registration.** All 1 MHz bus peripherals -- whether compiled into the server or loaded from shared libraries -- register through the same extension registry before the gRPC server starts. The registry collects extension descriptors; each descriptor provides C API function pointers (init, read, write, tick, etc.), FRED/JIM address claims, and an optional `grpc::Service*` for gRPC registration.

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

This reflects real hardware practice: you don't hot-plug cards into a 1 MHz bus on a running BBC Micro. If you want to change attached hardware, restart the machine. This is the same constraint that applies to Tube co-processors -- the parasite server starts alongside the main server, not later. Changing the hardware configuration means restarting the Beebium server process, which is the emulator equivalent of power-cycling the machine.

The startup sequence:

```
1. Beebium server starts, parses command-line options
2. Built-in extensions register their descriptors with the extension registry
3. Plugin directory scanned; each .so/.dll/.dylib is dlopen'd
   and beebium_extension_describe() called to obtain its descriptor
4. All descriptors validated (no address conflicts, compatible API version)
5. Each extension's init() function called
6. ServerBuilder registers all built-in gRPC services
   + grpc::Service* from any extension descriptors that provide one
7. BuildAndStart() -- gRPC server begins accepting connections
8. Emulation begins
```

Approaches A (generic dispatch) and C (separate server per plugin) remain available as future options if true hot-loading during emulation becomes desirable, but the pre-start unified registry should be the default.

#### SCSI Target Extensibility

The extension architecture has a second level: the SCSI host adapter itself needs to support pluggable targets. A hard disc target and a VP415 LaserDisc target both sit on the same SCSI bus but have entirely different command handling. The host adapter must not own device-specific logic -- it should handle the bus protocol (selection, phases, REQ/ACK) and delegate CDB processing to pluggable target implementations.

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

The host adapter calls `target[N].handle_cdb()` when a CDB arrives for target N. A hard disc target processes READ/WRITE against a DAT file. A VP415 target processes Group 6 F-code commands. The adapter is identical in both cases -- it just moves bytes between the 6502 bus and the target.

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

The cross-plugin case (SCSI adapter is a plugin, VP415 is also a plugin) requires the VP415 plugin to find and register with the adapter. This is handled by a **two-phase init**:

- **Phase 1**: Init 1 MHz bus extensions. The SCSI host adapter creates its target registry.
- **Phase 2**: Init sub-bus devices. Each extension whose descriptor declares `attaches_to = "scsi"` is passed a handle to the SCSI adapter's target registry and registers its target descriptor.

This avoids general-purpose dependency resolution. We're handling exactly one level of nesting (bus → device), which is all that's needed for the BBC Micro's peripheral topology. If a third level ever appeared (unlikely), the pattern could be generalised then.

The full startup sequence with two-phase init:

```
1. Beebium server starts, parses command-line options
2. Built-in extensions register their descriptors with the extension registry
3. Plugin directory scanned; .so/.dll/.dylib files loaded,
   beebium_extension_describe() called for each
4. All descriptors validated (no address conflicts, compatible API version)
5. Phase 1 init: 1 MHz bus extensions
   - SCSI host adapter init() called; creates its target registry
   - Music 5000 init() called; etc.
6. Phase 2 init: sub-bus devices
   - Each descriptor with attaches_to = "scsi" is passed to the
     SCSI adapter's target registry
   - Hard disc target init() called with DAT file path
   - VP415 target init() called (if present)
7. Collect grpc::Service* from all bus extensions AND all SCSI targets
8. ServerBuilder registers all built-in gRPC services + extension services
9. BuildAndStart() -- gRPC server begins accepting connections
10. Emulation begins
```

**Key design principle**: the SCSI host adapter must be designed from day one with the target registry as its core abstraction, even if the only initial target is a hard disc. Adding the `handle_cdb` dispatch through a target interface (rather than handling READ/WRITE directly in the adapter) costs almost nothing upfront but prevents a major restructuring when VP415 or other SCSI device support is added later.

#### Generalised Extension Point Architecture

The two-phase init described above is a simplification. Real BBC Micro hardware has multi-port devices (e.g. a joystick interface that connects to both the User Port and the Analogue Port simultaneously) and arbitrary nesting depth. A more general model uses **named extension points** and **dependency graph resolution**.

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

#### Extension Points as Hardware Specification

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

This scales in both directions. A fully-loaded Master AIV with SCSI adapter, VP415, hard disc, Music 5000, and joystick interface works because all the required extension points are present and the dependency graph resolves cleanly. A bare Model A with just a printer works because only `printer-port` is registered and no plugins attempt to attach to absent extension points.

The hardware policy classes already define what hardware is present (RAM size, ROM slots, I/O address map). Extension points extend this pattern to external ports, making the machine variant definition the single source of truth for what can be attached.

#### Built-in vs Plugin: Following the Internal/External Boundary

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
