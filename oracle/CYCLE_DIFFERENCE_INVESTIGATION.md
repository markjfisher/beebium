# Investigation: Origin of Cycle Differences Between Beebium and jsbeeb

## Summary

This document records the findings from investigating why beebium and jsbeeb have cycle count differences during emulation.

## Key Findings

### 1. Reset Sequence Handling (7 cycles initial offset)

**jsbeeb** (`6502.js:418-423`):
```javascript
reset(hard) {
    this.pc = this.readmem(0xfffc) | (this.readmem(0xfffd) << 8);
    this.p.i = true;
    if (hard) { this.currentCycles = 0; }
}
```
- Instantaneous reset - reads vector directly, sets PC immediately
- No cycles counted for reset sequence

**beebium** (`Machine.hpp:145-153`, `DebuggerService.hpp:316-337`):
- Full 7-cycle 6502 reset sequence implemented in CPU
- After `reset()`, the gRPC service calls `machine_.run(7)` to complete the reset sequence
- This puts both emulators at the same PC ($D9CD) but with different cycle counts

**Impact**: After reset, beebium shows 7 cycles, jsbeeb shows 0. The gRPC Reset handler compensates for this.

### 2. 1MHz Bus Stretching (1+ cycle differences)

At step 7 from reset, the instruction `LDA $FE4E` (System VIA IER read) shows:
- jsbeeb: 5 cycles
- beebium: 6 cycles

Accesses to the SHEILA range ($FE00-$FEFF) trigger 1MHz bus stretching. The exact timing depends on the phase relationship between the 2MHz and 1MHz clocks.

**Impact**: Each slow bus access can differ by 1 cycle. Over many accesses, this accumulates.

### 3. VSync Width Interpretation with R3=0 (Major difference during initialization)

**jsbeeb** (`video.js:91`):
```javascript
this.vpulseWidth = (val & 0xf0) >>> 4;  // Raw value: 0 stays 0
```

**beebium** (`Crtc6845.hpp:260-263`):
```cpp
uint8_t vsync_width() const {
    uint8_t w = (registers_[R3_SYNC_WIDTH] >> 4) & 0x0F;
    return (w == 0) ? 16 : w;  // Per 6845 datasheet: 0 means 16 scanlines
}
```

At reset, all CRTC registers are 0:
- **jsbeeb**: vpulseWidth=0, causing degenerate vsync behavior
- **beebium**: vsync_width=16, following the Hitachi 6845 datasheet

**Impact**: During the initialization period before MOS programs the CRTC, vsync timing differs significantly. This affects when the CA1 interrupt fires, which can affect IRQ timing and cycle counts.

### 4. Degenerate CRTC Behavior at Reset

With all CRTC registers at 0:
- R0 (horizontal total) = 0 → line length = 1 character
- R4 (vertical total) = 0 → frame height = 1 row
- R7 (vsync position) = 0 → vsync triggers at row 0
- R9 (max scanline) = 0 → row height = 1 scanline

Both emulators have undefined behavior in this state. jsbeeb and beebium implement different interpretations, leading to cycle differences during the initialization period.

## Test Observations

### Cycle Difference Pattern

Running convergence tests showed:
- After 100 instructions: beebium ahead by 2 cycles
- Fluctuates between small positive and negative values
- Eventually diverges significantly (2M+ cycles) around 600K instructions

This indicates the differences are NOT a simple constant offset but accumulate based on execution patterns.

### First VSync Detection

Test checking for CA1 interrupt (vsync end):
- jsbeeb: First CA1 interrupt at cycle 19 (instruction 6)
- beebium: No CA1 interrupt detected in 25000 instructions

This major difference is due to the vsync width interpretation (#3 above).

## Root Causes

1. **Slow bus timing**: Different implementations of 1MHz bus cycle stretching
2. **VSync width special case**: Different handling of R3=0 (jsbeeb: 0, beebium: 16)
3. **Degenerate CRTC state**: Undefined behavior when all registers are 0

## Recommendations

### For Oracle Testing

1. Accept small cycle differences (1-10 cycles per instruction) as normal
2. Focus on CPU state and memory comparison rather than cycle-exact matching
3. Test after MOS has initialized the CRTC (e.g., at BASIC entry point)

### For Beebium

1. **Document**: The vsync_width=0→16 mapping follows the 6845 datasheet
2. **Consider**: Matching jsbeeb's raw interpretation for compatibility testing
3. **Test**: Cycle accuracy against real hardware, not just jsbeeb

## Files Referenced

- `/Users/rjs/Code/jsbeeb/src/6502.js` - CPU reset
- `/Users/rjs/Code/jsbeeb/src/video.js` - Video timing, vsync
- `/Users/rjs/Code/jsbeeb/src/via.js` - VIA interrupt handling
- `/Users/rjs/Code/beebium/src/core/include/beebium/Machine.hpp` - Machine step/reset
- `/Users/rjs/Code/beebium/src/core/include/beebium/devices/Crtc6845.hpp` - CRTC implementation
- `/Users/rjs/Code/beebium/src/core/src/Via6522.cpp` - VIA edge detection
- `/Users/rjs/Code/beebium/src/core/include/beebium/VideoBinding.hpp` - Video/VIA connection
