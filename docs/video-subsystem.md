# Beebium Video Subsystem

## Overview

The video subsystem produces a stream of `PixelBatch` objects at 2MHz (one per CPU cycle). Each batch contains 8 pixels representing 0.5μs of video output. Clients can consume the raw stream or use the optional `FrameRenderer` to produce a traditional framebuffer.

## Data Flow

```
                         ┌─► VideoULA ─────┐
CRTC 6845 ──► screen ────┤                 ├──► PixelBatch ──► OutputQueue ──► FrameRenderer ──► FrameBuffer
  (timing)    memory     └─► SAA5050 ─────┘      (8 px)       (lock-free)      (optional)       (double-buffered)
                          (Mode 7 only)
```

The CRTC provides timing and addresses. Screen memory is read and passed to either the VideoULA (bitmap modes 0-6) or SAA5050 (teletext Mode 7), selected by the VideoULA's teletext mode bit.

## Components

### Crtc6845

Generates timing signals and memory addresses. Produces `Output` struct with:
- 14-bit screen address
- HSYNC/VSYNC signals
- Display enable flag
- Cursor state

### VideoUla

Converts screen memory bytes to pixels for bitmap modes 0-6. Handles:
- Mode-dependent pixel unpacking (1/2/4/8 bpp)
- Palette lookup (16 logical → 8 physical colors)
- Cursor rendering
- CRTC clock rate selection (1MHz/2MHz)
- Teletext mode detection (delegates to SAA5050)

### SAA5050 (Teletext Character Generator)

Renders Mode 7 teletext display. The SAA5050 is a dedicated chip that converts 7-bit character codes into pixel patterns using an internal character ROM.

**Architecture:**

```
Screen byte ──► byte() ──► Output Buffer ──► emit_pixels() ──► PixelBatch
                 │              (8 slots)          │
                 ▼                                 ▼
           Process control                  Font lookup
           codes & store                    & pixel gen
```

**4-Slot Output Delay Buffer:**

The SAA5050 models a 2μs propagation delay from character input (LOSE signal) to pixel output. This is implemented as an 8-slot circular buffer with read/write indices offset by 4 positions:

- `byte()` writes character data at `write_index`, then advances by 1
- `emit_pixels()` reads from `read_index`, then advances by 1
- Initial state: `write_index=4`, `read_index=0`
- Effect: 4 character delay between input and output

**Character Sets:**

| Charset | Range | Description |
|---------|-------|-------------|
| Alpha | 0x20-0x7F | Standard alphanumeric characters from ROM |
| ContiguousGraphics | 0x20-0x3F | 2×3 sixel blocks, adjacent |
| SeparatedGraphics | 0x20-0x3F | 2×3 sixel blocks with gaps |

**Control Codes (0x00-0x1F after masking to 7-bit):**

Control codes change rendering state but display as spaces:
- 0x01-0x07: Alpha colors (red through white)
- 0x11-0x17: Graphics colors
- 0x08: Flash, 0x09: Steady
- 0x0C: Normal height, 0x0D: Double height
- 0x18: Conceal
- 0x19: Contiguous graphics, 0x1A: Separated graphics
- 0x1C: Black background, 0x1D: New background
- 0x1E: Hold graphics, 0x1F: Release graphics

**Line/Frame Management:**

- `start_of_line()`: Reset per-line state (colors, charset, hold)
- `end_of_line()`: Advance raster counter by 2 (10 font rows → 20 scanlines)
- `vsync()`: Reset raster to 0, increment frame counter

**Font Data:**

96 characters × 10 rows × 6 bits per row, stored in `teletext_font.inl`. Each character is 6 pixels wide, rendered into 8-pixel batches (6 character + 2 spacing).

### PixelBatch

16-byte packet containing:
- 8 pixels (4-bit RGB each)
- Type (Bitmap/Teletext/Nothing)
- Flags (HSYNC/VSYNC/Display)

### OutputQueue

Lock-free SPSC circular buffer. Decouples core from consumers. Default capacity ~256K batches (~1 frame).

### FrameRenderer (optional)

Consumes queue, tracks raster position, writes BGRA32 pixels to framebuffer. Swaps buffers on VSYNC.

**Key features:**

- **Display-enable positioning**: Resets Y to 0 when display enable first goes high (start of visible area), resets X at line start. This positions content correctly regardless of CRTC sync timing variations.

- **Border tracking**: Counts all pixel batches (including blanking) to calculate four border dimensions:
  - `left_border`: Blanking pixels before display enable on each line
  - `right_border`: Total line width minus left border minus displayed width
  - `top_border`: Scanlines from VSYNC to first display enable
  - `bottom_border`: Total frame height minus top border minus displayed height

- **Interlace support**: Detects interlace mode via `VIDEO_FLAG_INTERLACE`, composites both fields into a single framebuffer (even field → even lines, odd field → odd lines), swaps buffers every other VSYNC.

- **Dynamic dimensions**: Tracks maximum X/Y written to determine actual frame dimensions. Sets logical width/height in `FrameMetadata` at swap time.

### FrameBuffer

Double-buffered with mutex-protected swap. Core writes to front buffer; clients read immutable back buffer. Version counter for change detection.

## Integration

Video output is optional. Call `ModelBHardware::enable_video_output()` to activate. The `tick_peripherals()` method clocks video hardware when enabled, pushing batches to the queue.

### ModelBHardware::tick_video()

The video pipeline is driven by `tick_video()`, called from `tick_peripherals()` at either 1MHz or 2MHz depending on the VideoULA's clock rate setting:

```cpp
void tick_video() {
    // 1. Tick CRTC to get timing and address
    auto crtc_output = crtc.tick();

    // 2. Translate CRTC address to BBC memory address
    uint16_t screen_addr = translate_screen_address(crtc_output.address);
    uint8_t screen_byte = crtc_output.display ? main_ram.read(screen_addr) : 0;

    // 3. Generate pixels (mode-dependent)
    PixelBatch batch;
    if (video_ula.teletext_mode()) {
        // Mode 7: SAA5050 teletext
        handle_teletext_timing(crtc_output);
        saa5050.byte(screen_byte, crtc_output.display ? 1 : 0);
        saa5050.emit_pixels(batch, bbc_colors::PALETTE);
    } else {
        // Modes 0-6: VideoULA bitmap
        video_ula.byte(screen_byte, crtc_output.cursor != 0);
        video_ula.emit_pixels(batch);  // or emit_blank()
    }

    // 4. Set sync flags and push to queue
    batch.set_flags(...);
    video_output->push(batch);
}
```

### Screen Address Translation

The CRTC outputs a 14-bit address. Translation depends on mode:

- **Mode 7**: Screen at 0x7C00-0x7FFF (1KB). Address = `0x7C00 | (crtc_addr & 0x3FF)`
- **Bitmap modes**: Screen base from addressable latch (IC32) bits 4-5:
  - 00: 0x3000, 01: 0x4000, 10: 0x5800, 11: 0x6000

### SAA5050 Timing Integration

The SAA5050 requires specific timing signals derived from CRTC output:

| CRTC Event | SAA5050 Call | Effect |
|------------|--------------|--------|
| VSYNC rising edge | `vsync()` | Reset raster, increment frame counter |
| HSYNC rising edge | `end_of_line()` | Advance raster by 2 |
| Display area start | `start_of_line()` | Reset per-line state |
| Each character | `byte()` + `emit_pixels()` | Feed char, get pixels |

## Current Status

**Implemented:**
- CRTC 6845 timing and address generation (including interlace mode)
- VideoULA mode detection and palette
- SAA5050 teletext character generator (complete):
  - All 96 printable characters with pre-computed antialiased font
  - Color control codes (foreground/background)
  - Graphics characters (contiguous and separated sixels)
  - Flash/steady animation
  - Double-height characters
  - Hold graphics mode
  - Conceal display
  - Gamma-corrected 6→8 pixel blending (B2-quality rendering)
- FrameBuffer double-buffering with metadata
- FrameRenderer with display-enable positioning and border tracking
- Dynamic frame dimensions (adapts to mode changes)
- Full border calculation (left, right, top, bottom)
- Interlace field compositing for Mode 7
- Logical pixel output with client-side scaling metadata

## Logical Pixel Output and Client Scaling

### Overview

The BBC Micro displays all screen modes at the same physical CRT size, but different modes have different logical resolutions:

| Mode | Logical Width | Display Width | Horizontal Scale |
|------|--------------|---------------|------------------|
| MODE 0 | 640 | 640 | 1× |
| MODE 1 | 320 | 640 | 2× |
| MODE 2 | 160 | 640 | 4× |
| MODE 3 | 640 | 640 | 1× |
| MODE 4 | 320 | 640 | 2× |
| MODE 5 | 160 | 640 | 4× |
| MODE 6 | 640 | 640 | 1× |
| MODE 7 | 480* | 480 | 1× |

*Mode 7 uses the SAA5050 teletext generator with 6→8 pixel expansion, producing 480 output pixels.

### Design Philosophy

Rather than pre-scaling pixels in the core (which would require interpolation decisions), Beebium outputs **logical pixels** and provides **display dimension metadata**. This approach:

1. **Preserves pixel fidelity**: Golden master tests can compare logical pixels directly
2. **Enables client choice**: Clients can use nearest-neighbor, bilinear, or CRT shader scaling
3. **Simplifies the core**: No scaling logic needed in VideoULA or FrameRenderer
4. **Supports flexible output**: Same frame data works for tests, framebuffers, and video streams

### PixelBatch Variable Width

The `PixelBatch` struct supports variable pixel counts per batch:

```cpp
struct PixelBatch {
    PixelData pixels;           // 8 pixel slots
    PixelBatchType type;        // Bitmap, Teletext, or Nothing
    uint8_t flags;              // HSYNC, VSYNC, Display

    // Variable pixel count (1-8), stored in pixels[2].bits.x
    void set_pixel_count(uint8_t count);
    uint8_t pixel_count() const;
};
```

The VideoULA emits different pixel counts based on mode:

| Bits per Pixel | Pixels per Batch | Modes |
|----------------|------------------|-------|
| 8 bpp | 8 pixels | MODE 0, 3, 6 |
| 4 bpp | 4 pixels | MODE 1, 4 |
| 2 bpp | 2 pixels | MODE 2, 5 |

### FrameMetadata Display Dimensions

The `FrameMetadata` struct includes target display dimensions:

```cpp
struct FrameMetadata {
    // ... existing fields ...

    // Target display resolution for client scaling
    // BBC displays all modes at the same physical CRT size
    uint32_t display_width = 640;   // Target width (typically 640)
    uint32_t display_height = 256;  // Target height (scanlines)
};
```

The `FrameRenderer` sets these in `finish_frame()`:

```cpp
void finish_frame() {
    meta.display_width = 640;  // All modes display at same width
    meta.display_height = static_cast<uint32_t>(frame_height);
    // ... swap buffers ...
}
```

### gRPC Frame Message

The `video.proto` Frame message includes display dimensions:

```protobuf
message Frame {
    uint64 frame_number = 1;
    uint32 width = 3;           // Logical width (varies by mode)
    uint32 height = 4;          // Logical height (scanlines)
    bytes pixels = 5;           // BGRA32 at logical resolution

    // Border dimensions
    uint32 left_border = 7;
    uint32 right_border = 8;
    uint32 top_border = 9;
    uint32 bottom_border = 10;

    // Target display dimensions for scaling
    uint32 display_width = 11;  // Target width (typically 640)
    uint32 display_height = 12; // Target height (typically 256)
}
```

### Client-Side Scaling

Clients receive frames at logical resolution and scale to display dimensions:

```
Core Output          gRPC Transport       Client Rendering
───────────          ──────────────       ────────────────
MODE 1: 320×256  ──► Frame {              Scale 320→640 (2×)
                     width: 320           using nearest-neighbor
                     height: 256          or shader
                     display_width: 640
                     display_height: 256
                    }
```

#### Metal Shader Example (macOS Client)

The macOS client uses a Metal shader with separate texture and display sizes:

```metal
struct Uniforms {
    float2 textureSize;    // Logical texture dimensions (e.g., 320×256)
    float2 displaySize;    // Target display dimensions (e.g., 640×256)
    float2 totalSize;      // Display size + borders
    float2 borderOffset;   // Left and top border widths
    // ...
};

fragment float4 fragmentShader(...) {
    // Calculate position in total area (including borders)
    float2 pixelCoord = in.texCoord * uniforms.totalSize;

    // Content area boundaries use displaySize
    float rightEdge = uniforms.borderOffset.x + uniforms.displaySize.x;
    float bottomEdge = uniforms.borderOffset.y + uniforms.displaySize.y;

    // Sample texture - UV automatically scales logical→display
    float2 contentCoord = pixelCoord - uniforms.borderOffset;
    float2 texUV = contentCoord / uniforms.displaySize;

    return texture.sample(textureSampler, texUV);
}
```

The shader uses `displaySize` for layout calculations but samples the texture using normalized UV coordinates, which automatically handles the scaling from logical to display resolution.

### Testing with Logical Pixels

Golden master tests benefit from logical pixel output:

```cpp
TEST_CASE("MODE 1 test card") {
    machine.memory().set_startup_screen_mode(1);
    machine.reset();
    // ... run and render ...

    // Frame is 320×256 - direct pixel comparison
    // No scaling artifacts to account for
    REQUIRE(frame.width() == 320);
    compare_golden_master("mode1_testcard.ppm", frame);
}
```

Test images are stored at logical resolution, making visual inspection and comparison straightforward
