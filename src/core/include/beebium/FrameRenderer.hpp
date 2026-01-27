// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#ifndef BEEBIUM_FRAME_RENDERER_HPP
#define BEEBIUM_FRAME_RENDERER_HPP

#include "PixelBatch.hpp"
#include "OutputQueue.hpp"
#include "FrameBuffer.hpp"
#include <cstdint>

namespace beebium {

// CRTC timing parameters for calculating display offsets.
// These are derived from CRTC registers and Video ULA settings.
struct DisplayTiming {
    uint8_t h_total = 127;           // R0: Horizontal Total (chars - 1)
    uint8_t h_displayed = 80;        // R1: Horizontal Displayed (chars)
    uint8_t h_sync_pos = 98;         // R2: Horizontal Sync Position
    uint8_t sync_width = 0x28;       // R3: H sync (low 4 bits), V sync (high 4 bits)
    uint8_t v_total = 38;            // R4: Vertical Total (rows - 1)
    uint8_t v_total_adjust = 0;      // R5: Vertical Total Adjust (scanlines)
    uint8_t v_displayed = 32;        // R6: Vertical Displayed (rows)
    uint8_t v_sync_pos = 34;         // R7: Vertical Sync Position
    uint8_t interlace_mode = 0;      // R8: Interlace and Skew
    uint8_t max_scanline = 7;        // R9: Max scanline address
    uint8_t pixels_per_char = 8;     // From Video ULA (8 for high freq, 16 for low)
};

// Converts PixelBatch stream to pixel framebuffer.
//
// The FrameRenderer consumes PixelBatches from an OutputQueue,
// converts them to BGRA32 pixels, and writes them to a FrameBuffer.
// It handles sync signals (HSYNC/VSYNC) to track raster position.
//
// Display positioning uses the CRTC display enable signal: Y resets
// to 0 when display enable goes high (first visible scanline), and
// X resets to 0 at the start of each visible line. This provides
// correct positioning based on actual CRTC timing, not hardcoded offsets.
//
// This is an optional convenience component. Clients that want
// raw PixelBatch access (e.g., for CRT shaders) can consume
// the queue directly without using FrameRenderer.
//
class FrameRenderer {
public:
    explicit FrameRenderer(FrameBuffer* frame_buffer)
        : frame_buffer_(frame_buffer)
        , x_(0)
        , y_(0)
        , in_vsync_(false)
        , in_hsync_(false)
        , horizontal_offset_(0)
        , vertical_offset_(0)
        , odd_field_(false)
        , field_offset_(0)
    {
        // Calculate initial offsets from default timing
        update_timing(DisplayTiming{});
    }

    // Update display timing from CRTC registers.
    // Call this when CRTC registers change, or at least once per frame.
    // TODO: Wire up actual CRTC registers for dynamic offset calculation.
    // For now, use empirical fixed offsets that match B2/BeebEm positioning.
    void update_timing(const DisplayTiming& timing) {
        timing_ = timing;

        // Small negative offset to compensate for blanking period before display
        // TODO: Calculate proper offset from CRTC H-sync position register
        horizontal_offset_ = 0;
        vertical_offset_ = 0;
    }

    // Process a batch of PixelBatches from the queue.
    // Returns number of batches consumed.
    // Should be called periodically (e.g., in render thread).
    size_t process(OutputQueue<PixelBatch>& queue, size_t max_units = 1000) {
        auto buffers = queue.get_consumer_buffer();
        if (buffers.empty()) {
            return 0;
        }

        size_t consumed = 0;
        size_t to_consume = std::min(max_units, buffers.total());

        // Process buffer A
        for (size_t i = 0; i < std::min(to_consume, buffers.a.size()); ++i) {
            process_unit(buffers.a[i]);
            ++consumed;
        }

        // Process buffer B (wrap-around portion)
        if (consumed < to_consume && !buffers.b.empty()) {
            for (size_t i = 0; i < std::min(to_consume - consumed, buffers.b.size()); ++i) {
                process_unit(buffers.b[i]);
                ++consumed;
            }
        }

        queue.consume(consumed);
        return consumed;
    }

    // Process a single PixelBatch
    void process_unit(const PixelBatch& batch) {
        uint8_t flags = batch.flags();
        bool vsync = (flags & VIDEO_FLAG_VSYNC) != 0;
        bool hsync = (flags & VIDEO_FLAG_HSYNC) != 0;
        bool display = (flags & VIDEO_FLAG_DISPLAY) != 0;
        bool interlace = (flags & VIDEO_FLAG_INTERLACE) != 0;

        // Track interlace mode from VIDEO_FLAG_INTERLACE
        in_interlace_mode_ = interlace;

        // Handle VSYNC rising edge - finalize frame and swap buffers
        if (vsync && !in_vsync_) {
            // Capture frame scanline count before reset
            max_frame_scanlines_ = std::max(max_frame_scanlines_, frame_scanline_count_);
            frame_scanline_count_ = 0;

            if (in_interlace_mode_) {
                // Latch CRTC's odd_field at VSYNC. The CRTC toggles odd_field at "end of
                // vertical displayed" (mid-frame), so by VSYNC time it reflects the NEXT
                // field that's about to start.
                latched_odd_field_ = (batch.flags() & VIDEO_FLAG_ODD_FIELD) != 0;

                // In interlace mode, a complete frame consists of two fields:
                // - Field 1 (odd_field=true): renders to framebuffer rows 0,2,4...
                // - Field 2 (odd_field=false): renders to framebuffer rows 1,3,5...
                //
                // We must swap buffers AFTER both fields complete, which is at the VSYNC
                // that starts the NEXT frame's field 1. If we swap at field 2's VSYNC,
                // we'd clear field 1's content before field 2 renders.
                //
                // Timing:
                // - VSYNC with odd_field=true: Field 1 about to start, previous Field 2 just ended
                //   → Finish and swap the completed frame, then start new frame
                // - VSYNC with odd_field=false: Field 2 about to start, Field 1 just ended
                //   → Don't swap yet, continue building current frame
                if (latched_odd_field_) {
                    finish_frame();
                }
            } else {
                // Non-interlace mode - finish frame every VSYNC
                finish_frame();
            }
            y_ = 0;  // Reset vertical position for new field
            was_displaying_ = false;  // New field starting
        }
        in_vsync_ = vsync;

        // Handle HSYNC rising edge - new scanline
        if (hsync && !in_hsync_) {
            // Capture line width before reset
            max_line_pixels_ = std::max(max_line_pixels_, line_pixel_count_);
            line_pixel_count_ = 0;
            blanking_count_ = 0;
            ++frame_scanline_count_;  // Count all scanlines including blanking

            ++y_;
            x_ = 0;  // Reset horizontal position for new scanline
            was_displaying_line_ = false;  // New line starting
            // Use capacity (physical allocation) for wrap check, not logical height
            if (y_ >= frame_buffer_->capacity_height()) {
                y_ = 0;  // Wrap around if we exceed buffer
            }
        }
        in_hsync_ = hsync;

        // Count ALL batches for total line width (before early return)
        line_pixel_count_ += 8;

        // Reset Y when first displayed scanline is reached (display enable rising edge)
        // This positions content correctly regardless of CRTC VSYNC timing variations.
        // Capture the pre-reset y_ as the top border (scanlines before display).
        if (display && !was_displaying_) {
            top_border_ = y_;  // Scanlines from VSYNC to first display line
            y_ = 0;  // First visible scanline in this field
            was_displaying_ = true;
        }

        // Count blanking batches and return early (don't write pixels during blanking)
        if (!display) {
            ++blanking_count_;
            return;
        }

        // Capture left border when display first goes high on a line
        if (display && !was_displaying_line_) {
            left_border_ = blanking_count_ * 8;  // Convert batches to pixels
            x_ = 0;
            was_displaying_line_ = true;
        }

        // Calculate write position (no offsets needed with display-enable reset)
        int write_x = static_cast<int>(x_);
        int write_y;
        if (in_interlace_mode_) {
            // Interlace: interleave fields on alternating framebuffer lines
            // Use the batch's odd_field directly to determine framebuffer row offset:
            // - odd_field=true (raster 0,2,4...) → offset=0 → write to rows 0,2,4...
            // - odd_field=false (raster 1,3,5...) → offset=1 → write to rows 1,3,5...
            // This directly maps raster values to framebuffer rows regardless of boot timing.
            // Note: odd_field toggles at "end of vertical displayed" (mid-frame), but all
            // visible pixels (display=true) have the correct value for their field.
            bool batch_odd_field = (batch.flags() & VIDEO_FLAG_ODD_FIELD) != 0;
            int field_offset = batch_odd_field ? 0 : 1;
            write_y = static_cast<int>(y_) * 2 + field_offset;
        } else {
            write_y = static_cast<int>(y_);
        }

        // Convert PixelBatch pixels to BGRA32 and write to framebuffer
        // Use pixel_count() for variable-width batches (mode-dependent)
        uint8_t pixel_count = batch.pixel_count();

        // Use capacity for bounds checking (physical allocation size)
        if (write_x >= 0 && write_x + pixel_count <= static_cast<int>(frame_buffer_->capacity_width()) &&
            write_y >= 0 && write_y < static_cast<int>(frame_buffer_->capacity_height())) {
            uint32_t* dest = frame_buffer_->write_ptr(static_cast<size_t>(write_x),
                                                       static_cast<size_t>(write_y));
            for (int i = 0; i < pixel_count; ++i) {
                dest[i] = pixel_to_bgra32(batch.pixels.pixels[i]);
            }

            // Track frame bounds for logical dimensions
            max_x_written_ = std::max(max_x_written_, static_cast<size_t>(write_x + pixel_count));
            max_y_written_ = std::max(max_y_written_, static_cast<size_t>(write_y + 1));
        }

        x_ += pixel_count;  // Advance by actual pixel count
    }

    // Get current raster position (for debugging)
    size_t x() const { return x_; }
    size_t y() const { return y_; }

    // Reset renderer state
    void reset() {
        x_ = 0;
        y_ = 0;
        in_vsync_ = false;
        in_hsync_ = false;
        odd_field_ = false;
        field_offset_ = 0;
        in_interlace_mode_ = false;
        latched_odd_field_ = true;   // Match CRTC reset odd_field=true
        was_displaying_ = false;
        was_displaying_line_ = false;
        max_x_written_ = 0;
        max_y_written_ = 0;
        top_border_ = 0;
        left_border_ = 0;
        line_pixel_count_ = 0;
        max_line_pixels_ = 0;
        frame_scanline_count_ = 0;
        max_frame_scanlines_ = 0;
        blanking_count_ = 0;
    }

    // Get tracked frame dimensions (for debugging/testing)
    size_t max_x_written() const { return max_x_written_; }
    size_t max_y_written() const { return max_y_written_; }

private:
    // Finalize frame at swap: set logical dimensions and metadata
    void finish_frame() {
        // Capture final line width
        max_line_pixels_ = std::max(max_line_pixels_, line_pixel_count_);

        // Use tracked dimensions, but default to capacity if nothing was written
        // (can happen on first frame or if display was never enabled)
        size_t frame_width = max_x_written_ > 0 ? max_x_written_ : frame_buffer_->capacity_width();
        size_t frame_height = max_y_written_ > 0 ? max_y_written_ : frame_buffer_->capacity_height();

        // Set logical dimensions to match actual content
        frame_buffer_->set_dimensions(frame_width, frame_height);

        // Build and store metadata
        FrameMetadata meta;
        meta.width = static_cast<uint32_t>(frame_width);
        meta.height = static_cast<uint32_t>(frame_height);
        meta.frame_number = frame_buffer_->version() + 1;
        meta.interlaced = in_interlace_mode_;

        // BBC Micro always displays at 640 pixels wide (physical CRT width)
        // regardless of logical resolution. Client scales width→display_width.
        // Scale factors: MODE 0=1x, MODE 1=2x, MODE 2=4x, MODE 4=2x, MODE 5=4x
        meta.display_width = 640;
        meta.display_height = static_cast<uint32_t>(frame_height);

        // Calculate borders from tracked values
        meta.left_border = static_cast<uint32_t>(left_border_);
        meta.top_border = static_cast<uint32_t>(top_border_);

        // right_border = total_line_width - left_border - displayed_width
        if (max_line_pixels_ > left_border_ + frame_width) {
            meta.right_border = static_cast<uint32_t>(max_line_pixels_ - left_border_ - frame_width);
        }

        // bottom_border = total_scanlines - top_border - displayed_height
        // In interlace mode, max_frame_scanlines_ is per-field, frame_height is composited
        size_t displayed_scanlines = in_interlace_mode_ ? frame_height / 2 : frame_height;
        if (max_frame_scanlines_ > top_border_ + displayed_scanlines) {
            meta.bottom_border = static_cast<uint32_t>(max_frame_scanlines_ - top_border_ - displayed_scanlines);
        }

        frame_buffer_->set_metadata(meta);

        // Swap buffers (no reallocation - just pointer swap)
        frame_buffer_->swap();

        // Clear new write buffer to black for next frame
        // (Gap scanlines in MODE 3/6 will remain black)
        frame_buffer_->clear(0x00000000);

        // Reset tracking for next frame
        max_x_written_ = 0;
        max_y_written_ = 0;
        max_line_pixels_ = 0;
        max_frame_scanlines_ = 0;
    }


    // Convert a 4-bit-per-channel VideoDataPixel to BGRA32
    static uint32_t pixel_to_bgra32(VideoDataPixel pixel) {
        // VideoDataPixel: bits 0-3 blue, 4-7 green, 8-11 red
        // BGRA32: bits 0-7 blue, 8-15 green, 16-23 red, 24-31 alpha
        auto b = static_cast<uint8_t>((pixel.bits.b << 4) | pixel.bits.b);  // 4-bit to 8-bit
        auto g = static_cast<uint8_t>((pixel.bits.g << 4) | pixel.bits.g);
        auto r = static_cast<uint8_t>((pixel.bits.r << 4) | pixel.bits.r);
        return (0xFF << 24) | (r << 16) | (g << 8) | b;
    }

    FrameBuffer* frame_buffer_;
    size_t x_;  // Current horizontal pixel position
    size_t y_;  // Current scanline
    bool in_vsync_;
    bool in_hsync_;
    DisplayTiming timing_;
    int horizontal_offset_;  // Pixels from HSYNC end to display start
    int vertical_offset_;    // Scanlines from VSYNC end to display start
    bool odd_field_;         // For interlace: alternates each frame
    int field_offset_;       // 0 or 1 for interlace field positioning
    bool in_interlace_mode_ = false;   // True when interlace mode detected
    bool latched_odd_field_ = true;    // Latched at VSYNC; init matches CRTC reset odd_field=true
    bool was_displaying_ = false;      // Frame-level: have we seen display=true this frame?
    bool was_displaying_line_ = false; // Line-level: have we seen display=true this line?

    // Track frame dimensions from actual pixel writes
    size_t max_x_written_ = 0;
    size_t max_y_written_ = 0;

    // Track border dimensions (blanking before active area)
    size_t top_border_ = 0;     // Scanlines from VSYNC to first display
    size_t left_border_ = 0;    // Pixels from HSYNC to first display

    // Track total line/frame dimensions (including blanking)
    size_t line_pixel_count_ = 0;      // All batches this line (reset at HSYNC)
    size_t max_line_pixels_ = 0;       // Maximum line width seen this frame
    size_t frame_scanline_count_ = 0;  // All scanlines this frame (reset at VSYNC)
    size_t max_frame_scanlines_ = 0;   // Maximum frame height seen

    // Blanking tracking for left border calculation
    size_t blanking_count_ = 0;        // Blanking batches since HSYNC
};

} // namespace beebium

#endif // BEEBIUM_FRAME_RENDERER_HPP
