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
