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
// Display positioning is calculated from CRTC timing registers to
// properly center content within the framebuffer, matching the
// behavior of other emulators (B2, BeebEm, B-Em).
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
        bool interlace_odd = (flags & VIDEO_FLAG_INTERLACE) != 0;

        // Track interlace mode from VIDEO_FLAG_INTERLACE
        if (interlace_odd) {
            in_interlace_mode_ = true;
        }

        // Handle VSYNC
        if (vsync && !in_vsync_) {
            // Rising edge of VSYNC - end of field
            if (in_interlace_mode_) {
                // In interlace mode, swap every other VSYNC
                interlace_field_count_++;
                if ((interlace_field_count_ & 1) == 0) {
                    // Even field count (0, 2, 4...) - swap after completing a pair
                    frame_buffer_->swap();
                }
            } else {
                // Non-interlace mode - swap every VSYNC
                frame_buffer_->swap();
            }
            y_ = 0;
        }
        in_vsync_ = vsync;

        // Handle HSYNC
        if (hsync && !in_hsync_) {
            // Rising edge of HSYNC - end of scanline
            x_ = 0;
            ++y_;
            if (y_ >= frame_buffer_->height()) {
                y_ = 0;  // Wrap around if we exceed buffer
            }
        }
        in_hsync_ = hsync;

        // Only write pixels during display enable
        if (!display) {
            return;
        }

        // Apply offsets for proper display positioning
        int write_x = static_cast<int>(x_) + horizontal_offset_;
        int write_y;
        if (in_interlace_mode_) {
            // Interlace: interleave fields on alternating framebuffer lines
            // First field (odd rasters 0,2,4...) → even lines (0, 2, 4...)
            // Second field (even rasters 1,3,5...) → odd lines (1, 3, 5...)
            // field_count is odd during first field, even during second
            int field_offset = (interlace_field_count_ & 1) ? 0 : 1;
            write_y = static_cast<int>(y_) * 2 + field_offset;
            write_y += vertical_offset_ * 2;  // Scale offset for interlace
        } else {
            write_y = static_cast<int>(y_) + vertical_offset_;
        }

        // Convert PixelBatch pixels to BGRA32 and write to framebuffer
        // Check bounds including negative offset possibility
        if (write_x >= 0 && write_x + 8 <= static_cast<int>(frame_buffer_->width()) &&
            write_y >= 0 && write_y < static_cast<int>(frame_buffer_->height())) {
            uint32_t* dest = frame_buffer_->write_ptr(static_cast<size_t>(write_x),
                                                       static_cast<size_t>(write_y));
            for (int i = 0; i < 8; ++i) {
                dest[i] = pixel_to_bgra32(batch.pixels.pixels[i]);
            }
        }

        x_ += 8;  // Each batch is 8 pixels
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
        interlace_field_count_ = 0;
    }

private:
    // Convert a 4-bit-per-channel VideoDataPixel to BGRA32
    static uint32_t pixel_to_bgra32(VideoDataPixel pixel) {
        // VideoDataPixel: bits 0-3 blue, 4-7 green, 8-11 red
        // BGRA32: bits 0-7 blue, 8-15 green, 16-23 red, 24-31 alpha
        uint8_t b = (pixel.bits.b << 4) | pixel.bits.b;  // 4-bit to 8-bit
        uint8_t g = (pixel.bits.g << 4) | pixel.bits.g;
        uint8_t r = (pixel.bits.r << 4) | pixel.bits.r;
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
    uint32_t interlace_field_count_ = 0; // Counts fields in interlace mode
};

} // namespace beebium

#endif // BEEBIUM_FRAME_RENDERER_HPP
