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

*All known bitmap mode rendering issues have been fixed.*

### ~~1. Cursor Width Incorrect in MODE 1 and MODE 2~~ (FIXED)

**Original symptom:**
- MODE 0: Cursor displays at correct width (8 logical pixels)
- MODE 1: Cursor displays at half the expected width (4 logical pixels)
- MODE 2: Cursor displays at one quarter the expected width (2 logical pixels)

**Root cause:**
Beebium calls `emit_pixels()` exactly once per `byte()` in all modes. The `cursor_pattern_` was being reset on every `byte()` call, preventing the cursor from spanning multiple batches.

**Fix:**
1. Added `cursor_was_active_` for rising-edge detection of cursor signal
2. Load cursor pattern only once per cursor position, persist across batches
3. Simplified `cursor_width_pattern()` to always produce 8 logical pixels
4. Changed cursor shift from `fast_clock() ? 2 : 1` to fixed 1-bit per batch

Result: Cursor spans 8 logical pixels in all bitmap modes:
- MODE 0: 1 batch × 8 pixels = 8 logical pixels
- MODE 1: 2 batches × 4 pixels = 8 logical pixels
- MODE 2: 4 batches × 2 pixels = 8 logical pixels

**Discrepancy with BBC hardware:**
The MOS writes to the VideoULA control register (0xFE20) with cursor width bits 5-7 set to values 4-7 for bitmap modes. According to the VideoULA encoding (`1xx` = 4 bytes), this specifies a 4-character-wide cursor, which would produce:
- MODE 0: 4 chars × 8 pixels = 32 logical pixels
- MODE 1: 4 chars × 4 pixels = 16 logical pixels
- MODE 2: 4 chars × 2 pixels = 8 logical pixels

After client-side scaling, this would give constant physical width (32 display pixels in all modes). However, Beebium intentionally ignores the `cursor_width_bits` value (except for "no cursor" = 0) and always produces 8 logical pixels. This gives consistent logical width but varying physical width after scaling.

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

### ~~3. Gap Scanlines Show Repeated Character Data~~ (FIXED)

**Original symptom:**
- MODE 3: The two blank rows at the bottom of each 10-scanline character cell showed a repeat of the top two rows of character data instead of being blank
- MODE 6: Similar issue with gap scanlines

**Root cause:**
In `VideoRenderer::render_bitmap()`, pixel emission was based only on `crtc_output.display`. When CRTC raster values exceeded 7 (the last row of character data), the address calculation masked raster with 0x07, causing raster 8→0 and raster 9→1. This fetched character data for what should be blank gap scanlines.

**Fix:**
1. Added `has_character_data(raster)` predicate in `VideoRenderer.hpp` that returns `true` only for raster < 8
2. Modified `render_bitmap()` to call `emit_blank()` instead of `emit_pixels()` when `!has_character_data(crtc_output.raster)`
3. Updated `emit_blank()` in `VideoUla.hpp` to apply cursor XOR and shift the cursor pattern, ensuring the cursor remains visible on gap scanlines

This matches B2 emulator's approach in `BBCMicro_Update.inl`:
```cpp
if (m_state.crtc_last_output.display && m_state.crtc_last_output.raster < 8) {
    m_state.video_ula.EmitPixels(&video_unit->pixels);
} else {
    m_state.video_ula.EmitBlank(&video_unit->pixels);
}
```

**Location:** `src/core/include/beebium/VideoRenderer.hpp`, `src/core/include/beebium/devices/VideoUla.hpp`

---

## Golden Master Status

| Mode | Dimensions | Cursor | Notes |
|------|------------|--------|-------|
| MODE 0 | 640x256 (correct) | 8 pixels (correct) | Updated with steady cursor |
| MODE 1 | 320x256 (correct) | 8 pixels (correct) | Updated with steady cursor |
| MODE 2 | 160x256 (correct) | 8 pixels (correct) | Updated with steady cursor |
| MODE 3 | 640x250 (correct) | 8 pixels (correct) | 10-line character cells with gap scanlines |
| MODE 7 | 640x500 (correct) | Correct | Working correctly |

Golden master tests use `force_steady_cursor()` to set CRTC R10 cursor mode to steady (non-blinking) for deterministic capture. This ensures the cursor is always visible regardless of blink timing.

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

### Gap Scanlines in MODE 3 and MODE 6

Some bitmap modes use character cells taller than 8 scanlines:
- **MODE 3**: 10 scanlines per character (R9 = 9)
- **MODE 6**: 10 scanlines per character (R9 = 9)

Screen memory only stores 8 bytes per character (one per scanline 0-7). Scanlines 8-9 are "gap scanlines" that contain no character data and must render as blank (black).

The `has_character_data(raster)` predicate in `VideoRenderer.hpp` checks if raster < 8:
```cpp
static constexpr bool has_character_data(uint8_t raster) {
    return raster < 8;
}
```

For gap scanlines, `emit_blank()` is called instead of `emit_pixels()`. However, the cursor must still be visible on gap scanlines. Therefore `emit_blank()` applies the cursor XOR and shifts the cursor pattern, matching the behavior of `emit_pixels()`:
```cpp
void emit_blank(PixelBatch& batch) {
    batch.set_type(PixelBatchType::Nothing);
    batch.clear();
    batch.set_pixel_count(pixels_per_batch());

    // Apply cursor XOR even on blank scanlines
    if (cursor_pattern_ & 1) {
        uint8_t count = pixels_per_batch();
        for (int i = 0; i < count; ++i) {
            batch.pixels.pixels[i].value ^= 0x0FFF;
        }
    }
    cursor_pattern_ >>= 1;
}
```

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

- `src/core/include/beebium/VideoRenderer.hpp` - Gap scanline handling with `has_character_data()` predicate
- `src/core/include/beebium/devices/VideoUla.hpp` - Cursor XOR in `emit_blank()` for gap scanlines
- `src/core/include/beebium/FrameRenderer.hpp` - Interlace mode tracking (fixed)
- `src/core/include/beebium/devices/Crtc6845.hpp` - Cursor timing and interlace detection (blink rate fixed)
- `clients/macos/Beebium/Beebium/Shaders.metal` - Line-doubling (fixed)
- `clients/macos/Beebium/Beebium/MetalRenderer.swift` - Interlace flag passing (fixed)
- `tests/test_bitmap_modes.cpp` - Golden master tests with `force_steady_cursor()` helper
