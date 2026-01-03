# Disc Controller Implementation References

This document tracks third-party disc controller implementations that may be useful
for refining Beebium's WD1770 emulation.

## MAME WD FDC Implementation

**Location:** `/Users/rjs/Code/mame/src/devices/machine/wd_fdc.h` and `wd_fdc.cpp`

MAME's implementation covers the entire WD FDC family (WD1770, WD1772, WD1773, etc.)
with detailed timing and state machine emulation. Notable features to consider:
- Detailed command state machines
- Accurate timing for step rates, head load, motor spin-up
- Index pulse handling
- CRC verification
- Format track support

## MAMEHub WD17xx Implementation

**URL:** https://github.com/MisterTea/MAMEHub/blob/master/Sources/Emulator/src/emu/machine/wd17xx.c

Older MAME codebase with WD17xx family support. May have simpler structure
for understanding the core state machine logic.

## B2 Emulator (Primary Reference)

**Location:** `/Users/rjs/Code/b2/`

B2's disc implementation was the primary architectural reference for Beebium.
Uses handler interface pattern for disc image abstraction.

## BeebEm Implementation

**Location:** `/Users/rjs/Code/beebem-mac/`

BeebEm has both Intel 8271 and WD1770 implementations. The 8271 is particularly
relevant for Model B compatibility (Phase 10+ of implementation plan).

## B-Em Implementation

**Location:** `/Users/rjs/Code/b-em/`

B-Em has both controllers with function pointer dispatch pattern.
Good reference for Tube co-processor disc access patterns.

## WD1770 Technical Documentation

**URL:** https://www.cloud9.co.uk/james/BBCMicro/Documentation/wd1770.html

Comprehensive WD1770 documentation with timing diagrams and signal descriptions.
Useful reference for understanding INTRQ, DRQ, and command timing.

---

## Implementation Notes

### Current Status (Phase 9 Complete)
- DiscGeometry for SSD/DSD format detection
- DiscImage abstraction with FileDiscImage and MemoryDiscImage implementations
- DiscDrive physical drive emulation (head positioning, motor control)
- WD1770 Type I commands (Restore, Seek, Step, Step-In, Step-Out)
- WD1770 Type II commands (Read Sector, Write Sector)
- DRQ/INTRQ signals with proper status register behavior
- NMI infrastructure in Machine class
- Model B+ integration with disc control register at 0xFE80:
  - Drive select, side select, density, motor control
  - Reset and NMI enable/disable gating
- Full integration tests for sector read/write

### Areas for Future Refinement (from MAME reference)
- [ ] More accurate motor spin-up timing
- [ ] Index pulse generation and detection
- [ ] CRC error detection
- [ ] Head load timing
- [ ] Lost data detection with proper timing
- [ ] Type III commands (Read Address, Read/Write Track)
- [ ] Detailed format track support
- [ ] Intel 8271 controller for Model B

---

## WD1770/WD1772 Test Suite Design

No dedicated formal test suite exists for WD1770/WD1772 emulation. This section compiles
research from multiple sources to design a comprehensive test suite for Beebium's disc
controller emulation.

### Additional Technical References

#### Primary Sources

| Source | URL | Content |
|--------|-----|---------|
| WD1770 Datasheet | https://cdn.hackaday.io/files/256641098008576/WD177x-00.pdf | Official timing specs |
| WD1772 Annotated Datasheet | http://info-coach.fr/atari/documents/_mydoc/WD1772-JLG.pdf | Corrected diagrams/tables |
| Atari-Forum WD1772 Thread | https://www.atari-forum.com/viewtopic.php?t=27448 | Undocumented behaviors |
| Stardot NMI Timing Thread | https://stardot.org.uk/forums/viewtopic.php?t=16114 | Real hardware timing tests |

#### Additional Reference Implementations

| Emulator | Source Location | Notes |
|----------|-----------------|-------|
| MAME (GitHub) | https://github.com/mamedev/mame/blob/master/src/devices/machine/wd_fdc.h | Decap-based fixes |
| B-Em (GitHub) | https://github.com/stardot/b-em/blob/master/src/wd1770.c | Timing constants |
| Hatari | https://github.com/hatari/hatari | Atari ST WD1772 |
| fdc1772-verilator | https://github.com/harbaum/fdc1772-verilator | FPGA testbench |

### Test Categories

#### Category 1: Register Access Tests
**Status: Already implemented in Beebium**

- [x] Initial status register = 0x00
- [x] Initial sector register = 0x01
- [x] Track/sector/data register read/write
- [x] Reading status clears INTRQ
- [x] Reading data clears DRQ
- [x] Register offset masking (2 bits)
- [x] Registers protected when BUSY

#### Category 2: Type I Command Tests (Positioning)
**Status: Partially implemented**

| Test | Expected Behavior | Source |
|------|-------------------|--------|
| Restore seeks to track 0 | Step out until TRACK0 signal | Datasheet |
| Restore updates track register | Track = 0 on completion | Datasheet |
| Restore sets TRACK0 status bit | Bit 2 = 1 when at track 0 | Datasheet |
| Seek moves to data register value | Step in/out until track = data | Datasheet |
| Seek updates track register | Track register updated each step | Datasheet |
| Step uses last direction | Direction preserved from previous command | Datasheet |
| Step-In/Out set direction | Direction flag updated | Datasheet |
| Update Track flag (U bit) | Track register only updated if bit 4 set | Datasheet |
| **Step rate timing** | 6/12/20/30ms per step (WD1770) | Datasheet |

#### Category 3: Type II Command Tests (Sector I/O)
**Status: Partially implemented**

| Test | Expected Behavior | Source |
|------|-------------------|--------|
| Read Sector loads data | Data register contains sector bytes | Datasheet |
| Read Sector DRQ timing | DRQ asserted for each byte | Datasheet |
| Write Sector stores data | Sector written to disc | Datasheet |
| Write protection check | STATUS_WRITE_PROT set if protected | Datasheet |
| Record Not Found | STATUS_RNF set if sector invalid | Datasheet |
| Multi-sector flag | Continue to next sector if bit 4 set | Datasheet |
| Lost Data detection | LOST_DATA set if DRQ not cleared in time | Datasheet |
| Side compare flag | Compare side number in ID field if S flag set | Datasheet |

#### Category 4: Type IV Command Tests (Force Interrupt)
**Status: Basic test exists**

| Test | Expected Behavior | Source |
|------|-------------------|--------|
| Force Interrupt clears BUSY | Status bit 0 cleared | Datasheet |
| Force Interrupt sets INTRQ | INTRQ asserted | Datasheet |
| **INTRQ timing** | ~101µs after command write | Stardot thread |
| Index pulse trigger | INTRQ on next index if bit 2 set | Datasheet |
| Immediate trigger | INTRQ immediately if bit 3 set | Datasheet |

#### Category 5: Timing Tests
**Status: Not implemented**

| Test | Expected Behavior | Source |
|------|-------------------|--------|
| Step rates WD1770 | 6ms, 12ms, 20ms, 30ms | Datasheet |
| Step rates WD1772 | 2ms, 3ms, 6ms, 12ms | Datasheet |
| Settle delay | 30ms (15ms for WD1772) | Datasheet |
| Motor spinup | 6 revolutions (1s at 300rpm) | Datasheet |
| Motor spindown | 10 revolutions idle | Datasheet |
| Byte transfer | ~64µs per byte (MFM, 300rpm) | B2 source |
| Command start delay | 16µs before execution starts | B-Em source |

#### Category 6: Status Register Tests
**Status: Partially implemented**

| Bit | Type I Meaning | Type II/III Meaning | Test |
|-----|----------------|---------------------|------|
| 0 | BUSY | BUSY | Write command sets, completion clears |
| 1 | INDEX | DRQ | Index pulse / data request |
| 2 | TRACK0 | LOST_DATA | At track 0 / data overrun |
| 3 | CRC_ERROR | CRC_ERROR | CRC mismatch |
| 4 | SEEK_ERROR | RNF | Verify fail / sector not found |
| 5 | SPIN_UP | RECORD_TYPE | Motor ready / deleted data mark |
| 6 | WRITE_PROT | WRITE_PROT | Disc write protected |
| 7 | MOTOR_ON | MOTOR_ON | Motor running |

#### Category 7: Undocumented Behaviors (from Atari-Forum)
**Status: Not implemented - Advanced**

| Behavior | Detail | Test Priority |
|----------|--------|---------------|
| CRC preset to $CDB4 | On each $F5 during Write Track | Low |
| $F5 writes $A1 sync | MFM sync mark $4489 | Low |
| $F6 writes $C2 sync | MFM sync mark $5224 | Low |
| $F7 escaping | Second $F7 written unchanged | Low |
| Index pulse overread | Sync at index causes long reads | Low |
| $29 $F5 exploit | Generates impossible sync sequences | Low |

These are primarily relevant for copy protection and can be deferred.

#### Category 8: Edge Cases & Error Conditions
**Status: Not implemented**

| Test | Expected Behavior | Source |
|------|-------------------|--------|
| No disc inserted | RNF error | Datasheet |
| Reset during command | BUSY may stay set (chip variant) | Cloud9 docs |
| Double command write | Second command ignored if BUSY | Datasheet |
| Invalid sector number | RNF error | Datasheet |
| Read past end of track | Sector wrap or RNF | Implementation-specific |
| Step beyond track 0 | TRACK0 status, no step | Datasheet |
| Step beyond track 79 | No limit check (wraps or stops) | Implementation-specific |

### Test Implementation Plan

#### Phase 1: Timing Infrastructure
Add test helpers for timing verification:

```cpp
// Helper to count ticks until condition
template<typename Predicate>
int ticks_until(WD1770& controller, Predicate pred, int max_ticks = 1000000) {
    for (int i = 0; i < max_ticks; ++i) {
        controller.tick();
        if (pred()) return i;
    }
    return -1;  // Timeout
}

// Example: measure INTRQ timing
int intrq_delay = ticks_until(controller, [&]() { return controller.intrq(); });
```

#### Phase 2: Step Rate Tests
Verify all four step rates for both WD1770 and WD1772.

#### Phase 3: Status Register Interpretation
Test that status bits have correct meaning per command type.

#### Phase 4: NMI Timing Verification
Port the Stardot test program logic (see appendix).

#### Phase 5: Cross-Emulator Validation
Create tests that verify Beebium behavior matches B2/MAME:

| Behavior | B2 | MAME | jsbeeb | Beebium Target |
|----------|-----|------|--------|----------------|
| NMI timing (X reg) | 217 | 217 | 216 | 216-217 |
| Step rate 0 | 6000 | 6000 | 6000 | 6000 |

### Test Priority Order

1. **High**: Step rate timing tests (fundamental accuracy)
2. **High**: Status register interpretation tests
3. **Medium**: NMI/INTRQ timing tests
4. **Medium**: Error condition tests (RNF, write protect)
5. **Low**: Type III commands (Read/Write Track)
6. **Low**: Undocumented behaviors (copy protection)

### Appendix: Real Hardware Test Data

#### NMI Timing Results (from Stardot)

| Emulator | X Register | NMI Count |
|----------|------------|-----------|
| Real BBC B | 91-147 (varies) | 1+ |
| jsbeeb | 216 | 1 |
| b-em | 216 | 1 |
| beebem | 216 | 1 |
| MAME | 217 | 1 |
| b2 | 217 | 1 |

#### Step Rate Constants (from B2 source)

```cpp
// WD1770 step rates in milliseconds
const int STEP_RATES_MS_1770[] = {6, 12, 20, 30};

// WD1772 step rates (faster)
const int STEP_RATES_MS_1772[] = {2, 3, 6, 12};

// Settle time
static const int SETTLE_uS_1770 = 30000;

// Byte transfer time (MFM at 300rpm)
static const int uS_PER_BYTE = 64;
```

#### Access Delays (from Cloud9 docs)

| Operation | Next Operation | Single Density | Double Density |
|-----------|----------------|----------------|----------------|
| Write command | Read busy bit | 48µs | 24µs |
| Write command | Read status 1-7 | 64µs | 32µs |
| Write any reg | Read same reg | 32µs | 16µs |

#### Stardot NMI Timing Test Program

From https://stardot.org.uk/forums/viewtopic.php?t=16114

```assembly
; NMI handler at &D00
PHA
TXA
PHA
INC &D40        ; Increment NMI counter
LDX &D40
LDA &FE64       ; Read VIA T1C-L for timing
STA &D40,X      ; Store timing value
LDA #&D0        ; Force Interrupt command
STA &FE84       ; Clear interrupt state
LDA &FE84       ; Read status (clears INTRQ)
PLA
TAX
PLA
RTI

; Main test code at &D20
SEI
LDA #&FF
STA &FE64       ; Set VIA timer high
STA &FE65
LDX #&D8        ; Force Interrupt + immediate (bit 3)
STX &FE84       ; Write to command register
CLI
RTS
```

**Expected results from real hardware:**
- X register value: 91-147 (varies with disc state)
- Time to NMI: ~101µs after command write
