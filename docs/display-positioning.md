# Display Positioning

This document describes the display positioning implementation in Beebium and potential future improvements.

## Current Implementation

### Problem Statement

When comparing Beebium's display output to B2 and BeebEm, the "BBC Computer 32K" startup text appeared:
- Approximately 2-3 MODE 7 characters too far left
- Approximately 1 MODE 7 text row too low

### Solution

Added offset infrastructure to `FrameRenderer` that adjusts where pixels are written in the framebuffer:

**Files modified:**
- `src/core/include/beebium/FrameRenderer.hpp`

**Key additions:**
1. `DisplayTiming` struct - holds CRTC register values for future dynamic offset calculation
2. `update_timing()` method - calculates horizontal and vertical offsets
3. Offset application in `process_unit()` - applies offsets when writing pixels
4. Interlace field handling - alternates field offset for interlaced modes

**Current empirical offsets:**
```cpp
horizontal_offset_ = 28;   // Shift right ~3 MODE 7 characters
vertical_offset_ = -20;    // Shift up ~1 MODE 7 row
```

These offsets produce positioning that closely matches B2 with "Correct aspect ratio" enabled.

## Comparative Analysis

### B2's Approach: TV Timing State Machine
B2 models actual CRT TV timing with explicit constants:
```cpp
static const int HORIZONTAL_RETRACE_CYCLES = 8;   // 4 µs
static const int BACK_PORCH_CYCLES = 16;          // 8 µs
static const int SCAN_OUT_CYCLES = 104;           // 52 µs
```

The state machine transitions through scanout, retrace, and back porch phases. Display position emerges from accurate timing emulation.

### BeebEm's Approach: CRTC Register Calculation
BeebEm calculates position from CRTC registers:
```cpp
int InitialOffset = 0 - (((CRTC_HorizontalTotal + 1) / 2) -
                        (HSyncModifier == 8 ? 40 : 20));

int HStart = InitialOffset +
             (CRTC_HorizontalTotal + 1 -
              (CRTC_HorizontalSyncPos + (CRTC_SyncWidth & 0x0f))) +
             ((HSyncModifier == 8) ? 2 : 1);
```

For standard Mode 7 timing, this produces HStart ≈ 0.

### B-Em's Approach: Fixed Offset with Sync Adjustment
B-Em uses a fixed 128-pixel left margin, adjusted by sync width:
```c
scrx = 128 - ((crtc[3] & 15) * 4);  // High frequency mode
```

### Beebium's Approach: Empirical Fixed Offsets
Currently uses simple fixed offsets tuned to match B2. This works for standard modes but won't adapt to custom CRTC timings.

## Remaining Differences

Even with corrected positioning, some differences remain:

1. **Vertical content density**: Beebium displays 25 MODE 7 rows in approximately the same vertical space where B2 displays 24. This results in:
   - Slightly compressed vertical appearance
   - Larger bottom margin in Beebium
   - B2 may be applying slight overscan/cropping

2. **Margin handling**: BeebEm has no visible top/bottom margins, suggesting different framebuffer sizing or overscan behavior.

These differences are cosmetic and don't affect functionality.

## Future Directions

### 1. Wire CRTC to FrameRenderer
Connect the actual CRTC register values to `update_timing()` so offsets adapt when:
- Screen mode changes (MODE 0-7)
- Games use custom CRTC timings (Elite, Exile, etc.)

**Implementation approach:**
- Add observer pattern or callback from CRTC to FrameRenderer
- Call `update_timing()` when relevant CRTC registers change

### 2. CRTC-Derived Offset Calculation
Replace empirical offsets with calculated values based on CRTC timing:
```cpp
// Back porch approach (needs refinement)
int h_back_porch = (h_total + 1) - (h_sync_pos + h_sync_width);
horizontal_offset_ = h_back_porch * pixels_per_char;
```

**Challenge:** The relationship between CRTC timing and framebuffer positioning is not straightforward. BeebEm's formula includes centering adjustments that cancel out the back porch for standard modes.

### 3. Overscan/Border Configuration
Add user-configurable overscan settings similar to B2:
- Control how much border/blanking area is visible
- Allow cropping edges for full-screen display
- Match different emulator behaviors

### 4. TV Timing State Machine
For highest accuracy, implement B2-style explicit TV timing:
- Model horizontal/vertical retrace periods
- Apply back porch delays before scanout
- Handle interlace field timing precisely

This would be a larger refactor but would produce the most accurate positioning.

## References

- B2: `/Users/rjs/Code/b2/src/beeb/src/TVOutput.cpp`
- BeebEm: `/Users/rjs/Code/beebem-mac/Src/Video.cpp` (AdjustVideo function)
- B-Em: `/Users/rjs/Code/b-em/src/video.c`
