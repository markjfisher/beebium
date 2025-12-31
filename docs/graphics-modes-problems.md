# Graphics Mode Problems

This document tracks known issues with bitmap mode rendering (MODE 0-6).

## Fixed Issues

### Interlacing Effects When Typing New Characters (FIXED)

**Original symptom:**
- When typing new characters in MODE 0, MODE 1, or MODE 2, half the character scanlines appeared first
- A fraction of a second later, the alternate scanlines were "filled in"

**Root cause:**
`in_interlace_mode_` flag in FrameRenderer was "sticky" - once set true during MODE 7 boot, it never got reset to false when switching to non-interlaced modes.

**Fix (commit ee706b9):**
Changed FrameRenderer.hpp from:
```cpp
if (interlace) {
    in_interlace_mode_ = true;  // Only ever set true, never false
}
```
To:
```cpp
in_interlace_mode_ = interlace;  // Track actual CRTC state
```

### Display Aspect Ratio Incorrect (FIXED)

**Original symptom:**
After fixing interlace stickiness, the macOS client displayed bitmap modes twice as wide as expected.

**Root cause:**
The PAR (Pixel Aspect Ratio) of 0.96 was calibrated for interlaced MODE 7 (~500 scanlines). Non-interlaced modes with 256 scanlines need line-doubling to calculate correct aspect ratio.

**Fix (commit ee706b9):**
The macOS client shader now applies line-doubling for non-interlaced modes:
```metal
float contentHeight = uniforms.totalSize.y;
if (uniforms.interlaced == 0) {
    contentHeight *= 2.0;  // Line-doubling for progressive modes
}
```

See `docs/video-subsystem.md` section "Display Geometry, Aspect Ratio, and Line-Doubling" for full explanation.

---

## Outstanding Issues

### 1. Cursor Width Incorrect in MODE 1 and MODE 2

**Symptom:**
- MODE 0: Cursor displays at correct width (8 logical pixels)
- MODE 1: Cursor displays at half the expected width (4 logical pixels)
- MODE 2: Cursor displays at one quarter the expected width (2 logical pixels)

**Required behavior:**
Cursor should span 8 logical pixels in ALL modes. After client-side scaling:
- MODE 0: 8 logical → 8 display pixels
- MODE 1: 8 logical → 16 display pixels (2:1 scaling)
- MODE 2: 8 logical → 32 display pixels (4:1 scaling)

**Root cause:**
Beebium calls `emit_pixels()` exactly once per `byte()` in all modes. The `cursor_pattern_` was being reset on every `byte()` call, preventing the cursor from spanning multiple batches.

- MODE 0: 1 batch = 8 pixels (correct)
- MODE 1: 1 batch = 4 pixels (need 2 batches for 8 pixels)
- MODE 2: 1 batch = 2 pixels (need 4 batches for 8 pixels)

**Status:** In progress. Fix requires detecting cursor_active rising edge and persisting cursor_pattern across multiple byte() calls.

**Location:** `src/core/include/beebium/devices/VideoUla.hpp`

### ~~2. Cursor Blink Rate Half Speed in Bitmap Modes~~ (FIXED)

**Original symptom:**
- MODE 0, MODE 1, MODE 2: Cursor blinks at half the expected rate
- MODE 7: Cursor blinks at correct rate

**Root cause:**
The CRTC's `frame_count_` (now renamed `field_count_`) incremented at different rates depending on interlace mode:
- In interlaced MODE 7: incremented twice per complete frame (once per field) = 100/sec
- In non-interlaced MODE 0-6: incremented once per frame = 50/sec

The blink masks (0x08, 0x10) assumed 50Hz field-rate counting, but this only held true in interlace mode.

**Fix:**
Renamed `frame_count_` to `field_count_` and normalized the increment rate:
- In interlace mode: increment by 1 (twice per frame = 100/sec, but mask comparison still correct)
- In non-interlace mode: increment by 2 per frame to simulate field rate

```cpp
field_count_ += interlace_sync_and_video() ? 1 : 2;
```

**Location:** `src/core/include/beebium/devices/Crtc6845.hpp` line 242

---

## Golden Master Status

| Mode | Dimensions | Cursor | Notes |
|------|------------|--------|-------|
| MODE 0 | 640x256 (correct) | Missing | Capture timing issue |
| MODE 1 | 320x256 (correct) | Half width | Cursor bug visible |
| MODE 2 | 160x256 (correct) | Quarter width | Cursor bug visible |
| MODE 7 | 640x500 (correct) | Correct | Working correctly |

---

## Technical Background

### Cursor Width in VideoUla

The VideoUla control register bits 5-7 specify cursor width:
- 0: No cursor
- 1: 1 byte wide
- 2: 2 bytes wide
- 3: 4 bytes wide
- 4-7: Full width

The cursor is rendered by XORing with white (0x0FFF) when the cursor pattern bit is set.

### Pixels Per Batch by Mode

| Mode | Pixels/Byte | Pixels/Batch | Logical Width |
|------|-------------|--------------|---------------|
| MODE 0 | 8 | 8 | 640 |
| MODE 1 | 4 | 4 | 320 |
| MODE 2 | 2 | 2 | 160 |

### Interlace Handling

MODE 7 uses CRTC register R8 set to 0x03 (interlace sync and video). In interlace mode:
- Two fields are rendered per frame
- Fields are interleaved on alternate scanlines
- Frame swap occurs every 2 VSYNCs

Bitmap modes (0-6) use R8 set to 0x00 (no interlace) by default:
- Single field per frame
- Sequential scanlines
- Frame swap occurs every VSYNC

### Programmatic Interlace Control

Interlace mode can be controlled independently of screen mode via the `*TV` command and VDU sequences. The FrameRenderer correctly tracks the actual CRTC R8 register state via `VIDEO_FLAG_INTERLACE` in PixelBatch flags.

---

## Files Involved

- `src/core/include/beebium/devices/VideoUla.hpp` - Cursor XOR application (width fixed)
- `src/core/include/beebium/FrameRenderer.hpp` - Interlace mode tracking (fixed)
- `src/core/include/beebium/devices/Crtc6845.hpp` - Cursor timing and interlace detection (blink rate fixed)
- `clients/macos/Beebium/Beebium/Shaders.metal` - Line-doubling (fixed)
- `clients/macos/Beebium/Beebium/MetalRenderer.swift` - Interlace flag passing (fixed)
