# Empirical Data Bus Behaviour on BBC Micro

This document summarizes findings from hardware testing and oscilloscope measurements
of BBC Micro data bus behaviour when reading from unmapped, empty, or write-only addresses.

Primary sources:
- https://stardot.org.uk/forums/viewtopic.php?t=17509 (scarybeasts, 2019)
- https://stardot.org.uk/forums/viewtopic.php?p=330703#p330703 (gfoot, 2021)

## Summary

The BBC Micro has **6.8KΩ pull-down resistors** on its data bus (contrary to the common
assumption of pull-ups). However, the value read from unmapped addresses depends on
**bus cycle timing**:

| Access Type | Typical Value | Reason |
|-------------|---------------|--------|
| Slow 1MHz (e.g., VIA, ADC) | 0x00 | Pull-downs have time to discharge bus |
| Fast 2MHz (e.g., CRTC, ULA, ROM) | 0xFE or previous value | Capacitance holds previous bus state |
| FRED/JIM (FC00/FD00) | 0xFF | 74LS245 transceiver actively drives bus |

## Hardware Architecture

### Pull-Down Resistors

The BBC Micro includes a 6.8KΩ resistor pack on the data bus, located near the speech
processor socket. Steve Furber has noted in interviews that the BBC has "far too many
things on its data bus" and the resistors were added to improve stability.

These are weak pull-downs that slowly discharge the bus capacitance toward 0V when
no device is actively driving the bus.

### The 74LS245 Transceiver (IC72)

FRED (FC00-FCFF) and JIM (FD00-FDFF) accesses activate a 74LS245 transceiver that
connects to the 1MHz bus connector. When enabled during a read:
- The transceiver amplifies signals from the 1MHz bus
- With nothing connected, floating LS inputs read as high
- Result: 0xFF is actively driven onto the CPU data bus

This is consistent and reliable - FRED/JIM always return 0xFF with no devices attached.

## Oscilloscope Measurements (gfoot, 2021)

Direct voltage measurements on the data bus during reads from unmapped addresses:

### Fast 2MHz Access (FE80 - 8271 disc controller range, chip removed)
- Bus voltage at CPU sample time: ~4V
- CPU reads this as logic high
- Result: Previous bus value retained (typically 0xFE)

### Slow 1MHz Access (FE60 - User VIA range, chip removed)
- Bus voltage at CPU sample time: ~1V
- CPU reads this as logic low
- Result: 0x00

The 1MHz bus stretches the clock cycle, giving the weak pull-down resistors sufficient
time to discharge the bus capacitance below the logic threshold.

## The "Last Bus Value" Effect

### Why 0xFE Appears So Often

When executing `LDA &FE21` (or similar), the instruction sequence puts values on the
data bus:
1. Opcode fetch: 0xAD (LDA absolute)
2. Low address byte: 0x21
3. High address byte: 0xFE  ← **This is left on the bus**
4. Data read from &FE21: Bus not driven, capacitance holds 0xFE

### Proof: Page-Crossing Indexed Addressing

gfoot's test demonstrates the effect definitively:

```assembly
LDY #&21
LDA &FDFF,Y    ; Crosses page boundary
```

This instruction sequence:
1. Reads from &FD20 first (JIM range - transceiver drives 0xFF)
2. Then reads from &FE20 (unmapped - bus holds previous value)

Result: **0xFF** instead of 0xFE, because the JIM read set up the bus.

### OS ROM Bleed-Through

gfoot also discovered that during SHIELA (FE00-FEFF) reads, the OS ROM briefly drives
the bus. The ROM's active-low CS signal (directly from address decoding) changes
faster than its active-low OE signal (which goes through additional gates to mask
SHIELA accesses). This creates a brief window where ROM data appears on the bus.

## Practical Test Results

Tests from multiple BBC Model B machines (scarybeasts thread, 2019):

| Address | Description | Typical Result | Notes |
|---------|-------------|----------------|-------|
| &FC00 | FRED (1MHz bus) | 255 | Transceiver drives high |
| &FD00 | JIM (1MHz bus) | 255 | Transceiver drives high |
| &FEA0 | Unmapped I/O | 254 (varies) | Fast cycle, previous bus value |
| &FE21 | Video ULA (write-only) | 254 | Fast cycle, address byte echo |
| &FE00 after write | CRTC address register | 0 | CRTC may drive bus |
| &FE6C | User VIA (if present) | Written value | Normal R/W register |

### Cold Boot Anomalies

BigEd observed different results (190 instead of 254) on a "very cold" BBC that had
been off for an extended period. This is attributed to capacitance effects in chips
that hadn't reached normal operating temperature. Results stabilized after warm-up.

## CRTC Register Readability

Contrary to some documentation, CRTC registers R12 and R13 (screen start address)
**are readable** on BBC hardware. The BBC uses an S-type 6845 (Hitachi HD6845S or
compatible) which supports:
- Programmable vsync width (needed for interlace)
- Display skew compensation (needed for Mode 7 SAA5050 delay)
- Readable R12/R13

Test: `?&FE00=12:P.?&FE01` returns 40 in MODE 7 (the screen start address high byte).

## Software That Relies on This Behaviour

Despite being technically unreliable, Acorn firmware and some software depends on
these effects:

### Tube Detection (MOS ROM)
```assembly
LDA #$81
STA $FEE0        ; Write to Tube register
LDA $FEE0        ; Read back
ROR A            ; Check bit 0
```
If no Tube present, capacitance briefly holds the written value. The odd value (0x81)
ensures bit 0 is set, which wouldn't happen if the bus had time to discharge to 0.

### DFS/ADFS Startup
Reads from potentially empty disc controller addresses, expecting specific capacitive
behaviour to detect hardware presence.

### User VIA Detection (MOS ROM)
```assembly
LDA #$0E
STA $FE6C        ; Write to VIA timer latch
CMP $FE6C        ; Read back and compare
BEQ via_present
```

### Arcadians
Reads random sections of MOS ROM for the "ship exploded" sound effect. Sometimes
this crosses into I/O space, causing reads from unmapped addresses to affect the
audio sample data (though not game logic).

### Protected Loaders
At least one game's copy protection reads from FRED/JIM expecting 0xFF.

## Implications for Emulators

### Simple Approach (Most Emulators)
Return a fixed value:
- 0xFF for unmapped addresses (matches FRED/JIM, erased EPROM)
- 0x00 for unmapped addresses (matches discharged bus)

Neither is fully accurate but 0xFF is closer for fast (2MHz) accesses.

### Accurate Approach
Track the last value driven onto the data bus and return it for unmapped reads.
This matches observed hardware behaviour for fast cycles.

### Most Accurate Approach
Distinguish between fast and slow bus cycles:
- Fast cycles: Return previous bus value
- Slow cycles: Return 0x00
- FRED/JIM: Return 0xFF (actively driven)

### Empty ROM Sockets

Sideways ROM reads are fast 2MHz accesses. An empty socket would return the previous
bus value, which is typically:
- Part of the instruction opcode/operand
- Data from the previous memory access

For practical purposes, 0xFF is a reasonable approximation because:
1. It's what an unprogrammed/erased EPROM would return
2. It's closer to fast-cycle behaviour than 0x00
3. ROM enumeration code expects 0xFF for "no ROM present"
4. FRED/JIM (which have similar address decoding) return 0xFF

Returning 0x00 (as jsbeeb currently does) is incorrect - this is slow 1MHz behaviour
that doesn't apply to ROM reads.

## References

- Stardot forums: Reads of write-only CRTC/ULA registers
  https://stardot.org.uk/forums/viewtopic.php?t=17509

- Stardot forums: Open bus behaviour and measurements
  https://stardot.org.uk/forums/viewtopic.php?p=330703#p330703

- Hitachi HD6845S datasheet (S-type CRTC)
  http://www.cpcwiki.eu/imgs/c/c0/Hd6845.hitachi.pdf

- 6502.org: Building the BBC Micro [pulldowns considered harmful]
  http://forum.6502.org/viewtopic.php?f=1&t=5240

- BBC Micro Service Manual and Circuit Diagrams
