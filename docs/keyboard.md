# BBC Micro Keyboard Emulation

This document describes the keyboard matrix emulation in Beebium, based on analysis of the MOS 1.20 ROM and reference emulators (BeebEm, B2).

## Keyboard Matrix

### Hardware Overview

The BBC Micro keyboard is organized as a 10×8 matrix with 73 keys and 8 keyboard links. The System VIA (6522 at $FE40-$FE4F) interfaces with the keyboard:

- **Port A (bits 0-6)**: Key number output (column in low nibble, row in bits 4-6)
- **Port A (bit 7)**: Key state input (directly from keyboard matrix)
- **Port B (bits 0-2)**: Addressable latch address
- **Port B (bit 3)**: Addressable latch data
- **Addressable latch bit 3**: KB_WRITE (active low, enables keyboard scanning)

### Key Number Encoding

The MOS encodes key positions as a 7-bit "key number":
```
Bits 0-3: Column (0-15, though only 0-9 used for keys)
Bits 4-6: Row (0-7)
```

For example:
- SHIFT is at row 0, column 0 → key number $00
- CTRL is at row 0, column 1 → key number $01
- 'A' is at row 4, column 1 → key number $41

### Bit 7 Convention

**Critical**: The software convention used by emulators (BeebEm, B2) is:

| Bit 7 Value | Meaning |
|-------------|---------|
| 0 | Key/link NOT pressed (open circuit) |
| 1 | Key/link IS pressed (closed circuit) |

This is the logical convention, not the physical hardware level. When emulating:
- Return `output & 0x7F` (bit 7 = 0) when no key is pressed
- Return `output | 0x80` (bit 7 = 1) when the key IS pressed

### Row 0 Special Handling

Row 0 contains special keys and links that don't generate interrupts:
- Column 0: SHIFT
- Column 1: CTRL
- Columns 2-9: Keyboard links (active only during startup)

The MOS scans SHIFT and CTRL separately when needed (e.g., in the `$CAE0` handleScrollingInPagedMode routine). During regular keyboard scanning, row 0 keys don't trigger the CA2 keyboard interrupt.

## Keyboard Links (Startup Options)

### Overview

Eight keyboard links at row 0 (columns 2-9) form a startup options byte read by the MOS at reset. These are typically DIP switches on physical hardware.

### Link-to-Bit Mapping

The 8 links map to an 8-bit startup options byte:

| Link | Column | Bit | Purpose |
|------|--------|-----|---------|
| Link 1 | 2 | 7 | ROM-dependent (filing system priority) |
| Link 2 | 3 | 6 | ROM-dependent |
| Link 3 | 4 | 5 | ROM-dependent (disc timing) |
| Link 4 | 5 | 4 | ROM-dependent (disc timing) |
| Link 5 | 6 | 3 | SHIFT-BREAK action |
| Link 6 | 7 | 2 | Screen mode bit 2 (MSB) |
| Link 7 | 8 | 1 | Screen mode bit 1 |
| Link 8 | 9 | 0 | Screen mode bit 0 (LSB) |

### Active-Low Logic

Links use active-low logic:
- **Bit SET (1)** = Link BROKEN (open circuit)
- **Bit CLEAR (0)** = Link MADE (closed circuit)

Default value **0xFF** (all bits set) = all links broken.

### Stable Meanings (Bits 0-3)

Only bits 0-3 have consistent meanings across all BBC Micro configurations:

**Bits 0-2: Screen Mode**
- The MOS XORs these bits with 7 to get the screen mode
- All broken (0x07) → Mode 7 (default)
- All made (0x00) → Mode 0

**Bit 3: SHIFT-BREAK Action**
- Bit 3 = 1 (broken): Normal - BREAK auto-boots, SHIFT-BREAK doesn't
- Bit 3 = 0 (made): Reversed - BREAK doesn't auto-boot, SHIFT-BREAK does

### ROM-Dependent Meanings (Bits 4-7)

Bits 4-7 are interpreted differently by different ROMs:
- **Bits 4-5**: Disc timing (varies by DFS ROM and controller chip)
- **Bit 6**: Varies by filing system (HADFS, ENFS, etc.)
- **Bit 7**: Filing system priority (DFS vs NFS) or screen mode on Master

### Emulation Implementation

Beebium stores startup options as a single byte, separate from the key matrix:

```cpp
// In KeyboardMatrix
std::atomic<uint8_t> startup_options_{0xFF};  // Default: all links broken

// SystemViaPeripheral reads links from this byte
if (row == 0 && column >= 2 && column <= 9) {
    uint8_t bit = 9 - column;  // Column 9 = bit 0, column 2 = bit 7
    pressed = (startup_options_ & (1 << bit)) == 0;  // Active low
}
```

### CLI Configuration

```bash
beebium-model-b --screen-mode 0        # Boot in Mode 0
beebium-model-b --auto-boot            # Enable SHIFT-BREAK auto-boot
beebium-model-b --links 248            # Raw byte (0xF8 = Mode 0)
```

### gRPC API

```protobuf
rpc SetStartupOptions(SetStartupOptionsRequest) returns (SetStartupOptionsResponse);
rpc GetStartupOptions(GetStartupOptionsRequest) returns (StartupOptions);
```

## References

- BeebEm source: `Src/SysVia.cpp` - keyboard matrix and link handling
- B2 source: `src/beeb/src/BBCMicro_Update.inl` - keyboard state management
- MOS 1.20 disassembly: https://tobylobster.github.io/mos/mos/
- BBC Micro Advanced User Guide - keyboard hardware description
