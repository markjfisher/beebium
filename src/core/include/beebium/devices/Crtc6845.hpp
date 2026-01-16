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

#pragma once

#include "../ClockTypes.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace beebium {

// MC6845 CRTC (Cathode Ray Tube Controller)
//
// The 6845 generates video timing signals and provides the memory address
// for reading character/pixel data from screen memory.
//
// Address mapping (offset & 1):
//   0: Address register (write selects register 0-17)
//   1: Data register (read/write selected register)
//
// Memory-mapped at 0xFE00-0xFE07 with Mirror<0x07>
//
class Crtc6845 {
public:
    // Clock subscription: falling edge only, dynamic rate (1MHz/2MHz)
    static constexpr ClockEdge clock_edges = ClockEdge::Falling;

    // Dynamic clock rate - depends on video mode
    ClockRate clock_rate() const {
        return fast_clock_ ? ClockRate::Rate_2MHz : ClockRate::Rate_1MHz;
    }

    void set_fast_clock(bool fast) { fast_clock_ = fast; }
    bool fast_clock() const { return fast_clock_; }

    // Clock subscriber interface
    void tick_falling() { tick(); }

    // Register indices
    static constexpr uint8_t R0_HTOTAL          = 0;   // Horizontal total (chars - 1)
    static constexpr uint8_t R1_HDISPLAYED      = 1;   // Horizontal displayed (chars)
    static constexpr uint8_t R2_HSYNC_POS       = 2;   // Horizontal sync position
    static constexpr uint8_t R3_SYNC_WIDTH      = 3;   // Sync widths (H low nibble, V high)
    static constexpr uint8_t R4_VTOTAL          = 4;   // Vertical total (rows - 1)
    static constexpr uint8_t R5_VTOTAL_ADJ      = 5;   // Vertical total adjust (scanlines)
    static constexpr uint8_t R6_VDISPLAYED      = 6;   // Vertical displayed (rows)
    static constexpr uint8_t R7_VSYNC_POS       = 7;   // Vertical sync position
    static constexpr uint8_t R8_INTERLACE       = 8;   // Interlace and skew
    static constexpr uint8_t R9_MAX_SCANLINE    = 9;   // Max scanline address (rows - 1)
    static constexpr uint8_t R10_CURSOR_START   = 10;  // Cursor start scanline
    static constexpr uint8_t R11_CURSOR_END     = 11;  // Cursor end scanline
    static constexpr uint8_t R12_START_ADDR_HI  = 12;  // Start address high (6 bits)
    static constexpr uint8_t R13_START_ADDR_LO  = 13;  // Start address low
    static constexpr uint8_t R14_CURSOR_HI      = 14;  // Cursor position high
    static constexpr uint8_t R15_CURSOR_LO      = 15;  // Cursor position low
    static constexpr uint8_t R16_LIGHTPEN_HI    = 16;  // Light pen high (read-only)
    static constexpr uint8_t R17_LIGHTPEN_LO    = 17;  // Light pen low (read-only)

    // Output signals from the CRTC - updated by tick()
    struct Output {
        uint16_t address : 14;   // Memory address (MA0-MA13)
        uint16_t raster : 5;     // Current scanline within character row (RA0-RA4)
        uint16_t hsync : 1;      // Horizontal sync
        uint16_t vsync : 1;      // Vertical sync
        uint16_t display : 1;    // Display enable (active during visible area)
        uint16_t cursor : 1;     // Cursor display at current position
        uint16_t interlace : 1;  // Interlace sync and video mode active
        uint16_t odd_field : 1;  // Odd field (0) or even field (1)
    };

    // Debug callback for cursor detection analysis
    // Parameters: char_addr, cursor_pos, screen_start, row, raster, cursor_detected
    using CursorDebugCallback = std::function<void(
        uint16_t char_addr,
        uint16_t cursor_pos,
        uint16_t screen_start,
        uint8_t row,
        uint8_t raster,
        bool cursor_detected
    )>;

    void set_cursor_debug_callback(CursorDebugCallback cb) {
        cursor_debug_callback_ = std::move(cb);
    }

    void clear_cursor_debug_callback() {
        cursor_debug_callback_.reset();
    }

    // Read from CRTC register
    uint8_t read(uint16_t offset) const {
        if (offset & 1) {
            // Data register read - only some registers are readable
            if (address_register_ >= 12 && address_register_ <= 17) {
                return registers_[address_register_];
            }
        }
        return 0x00;
    }

    // Write to CRTC register
    void write(uint16_t offset, uint8_t value) {
        if (offset & 1) {
            // Data register write
            if (address_register_ < 18) {
                registers_[address_register_] = value & register_masks_[address_register_];
            }
        } else {
            // Address register write
            address_register_ = value & 0x1F;
        }
    }

    // Advance CRTC state by one character clock
    // Returns the current output signals
    // lightpen: state of light pen input (active high)
    Output tick(bool lightpen = false) {
        // Capture light pen position on rising edge
        if (lightpen && !prev_lightpen_) {
            registers_[R16_LIGHTPEN_HI] = static_cast<uint8_t>(char_addr_ >> 8) & 0x3F;
            registers_[R17_LIGHTPEN_LO] = static_cast<uint8_t>(char_addr_ & 0xFF);
        }
        prev_lightpen_ = lightpen;

        // Handle horizontal sync
        if (hsync_counter_ >= 0) {
            ++hsync_counter_;
            if (hsync_counter_ >= hsync_width()) {
                hsync_counter_ = -1;
            }
        }

        // Check horizontal displayed
        if (column_ == registers_[R1_HDISPLAYED]) {
            // Latch next line address at end of displayed area (14-bit)
            next_line_addr_ = char_addr_ & 0x3FFF;
            h_display_ = false;
        }

        // End of horizontal total never displays
        if (column_ == registers_[R0_HTOTAL]) {
            h_display_ = false;
        }

        // Start horizontal sync
        if (column_ == registers_[R2_HSYNC_POS] && hsync_counter_ < 0) {
            hsync_counter_ = 0;
        }

        // Handle vertical sync END first, then START (order matters!)
        // If vsync ends, vsync_counter_ becomes -1, allowing a new vsync to start on the same tick.
        // This matches jsbeeb's order where inVSync=false is set before the start check.

        // Vsync END (only at valid interlace point)
        // Vsync counter is incremented per scanline in end_of_scanline()
        if (vsync_counter_ >= 0 && vsync_counter_ >= vsync_width() && is_vsync_point()) {
            vsync_counter_ = -1;
        }

        // Vsync START (only at valid interlace point)
        // In interlace mode, vsync start is delayed to half-scanline on even frames
        if (row_ == registers_[R7_VSYNC_POS] && vsync_counter_ < 0 && !had_vsync_this_row_ && is_vsync_point()) {
            vsync_counter_ = 0;
            had_vsync_this_row_ = true;
        }

        // Build output
        Output output;
        output.address = char_addr_ & 0x3FFF;
        output.raster = raster_ & 0x1F;
        output.hsync = hsync_counter_ >= 0 ? 1 : 0;
        output.vsync = vsync_counter_ >= 0 ? 1 : 0;
        output.display = (h_display_ && v_display_) ? 1 : 0;
        output.interlace = interlace_sync_and_video() ? 1 : 0;
        output.odd_field = odd_field_ ? 1 : 0;

        // Cursor display
        output.cursor = 0;
        if (output.display) {
            uint16_t cursor_pos = cursor_position();
            uint16_t masked_char_addr = char_addr_ & 0x3FFF;
            // Both addresses must be compared in 14-bit space (MA0-MA13)
            bool address_match = (masked_char_addr == cursor_pos);

            // Debug callback for cursor analysis
            if (cursor_debug_callback_) {
                (*cursor_debug_callback_)(
                    masked_char_addr,
                    cursor_pos,
                    screen_start(),
                    row_,
                    raster_,
                    address_match
                );
            }

            if (address_match) {
                uint8_t cursor_start = registers_[R10_CURSOR_START] & 0x1F;
                uint8_t cursor_end = registers_[R11_CURSOR_END] & 0x1F;
                uint8_t cursor_mode = (registers_[R10_CURSOR_START] >> 5) & 0x03;

                if (raster_ >= cursor_start && raster_ <= cursor_end) {
                    switch (cursor_mode) {
                        case 0: // Steady cursor
                            output.cursor = 1;
                            break;
                        case 1: // No cursor
                            break;
                        case 2: // Blink at 1/16 field rate
                            output.cursor = (field_count_ & 0x08) ? 1 : 0;
                            break;
                        case 3: // Blink at 1/32 field rate
                            output.cursor = (field_count_ & 0x10) ? 1 : 0;
                            break;
                    }
                }
            }
        }

        // Advance character address (wrap at 14-bit boundary)
        char_addr_ = (char_addr_ + 1) & 0x3FFF;

        // Handle end of horizontal line
        if (column_ == registers_[R0_HTOTAL]) {
            end_of_scanline();
            column_ = 0;
            h_display_ = true;
        } else {
            ++column_;
        }

        return output;
    }

    // Accessors for commonly needed values
    uint16_t screen_start() const {
        return (static_cast<uint16_t>(registers_[R12_START_ADDR_HI] & 0x3F) << 8) |
               registers_[R13_START_ADDR_LO];
    }

    uint16_t cursor_position() const {
        return (static_cast<uint16_t>(registers_[R14_CURSOR_HI] & 0x3F) << 8) |
               registers_[R15_CURSOR_LO];
    }

    uint8_t max_scanline() const { return registers_[R9_MAX_SCANLINE] & 0x1F; }
    uint8_t hsync_width() const { return registers_[R3_SYNC_WIDTH] & 0x0F; }
    // VSYNC width: 0 means 16 scanlines (per Hitachi 6845 datasheet)
    uint8_t vsync_width() const {
        uint8_t w = (registers_[R3_SYNC_WIDTH] >> 4) & 0x0F;
        return (w == 0) ? 16 : w;
    }

    // Interlace mode detection
    // R8 bits 0-1: 00=normal, 01=interlace sync, 10=normal, 11=interlace sync and video
    // Mode 7 uses interlace sync and video (0x03)
    bool interlace_sync_and_video() const {
        return (registers_[R8_INTERLACE] & 0x03) == 0x03;
    }

    // Check if interlace sync mode is enabled (R8 bit 0)
    // This includes both "interlace sync" (R8=1) and "interlace sync and video" (R8=3)
    // Used for vsync timing - see Motorola 6845 datasheet figure 13
    bool is_interlace_sync() const {
        return (registers_[R8_INTERLACE] & 1) != 0;
    }

    // Current position (for debugging)
    uint8_t column() const { return column_; }
    uint8_t row() const { return row_; }
    uint8_t raster() const { return raster_; }
    uint16_t address() const { return char_addr_; }

    // Direct register access for testing/debugging
    uint8_t reg(uint8_t index) const {
        return (index < 18) ? registers_[index] : 0;
    }

    // Address register (currently selected register for read/write)
    uint8_t address_register() const { return address_register_; }

    // Sync and display state for debugger inspection
    bool in_hsync() const { return hsync_counter_ >= 0; }
    bool in_vsync() const { return vsync_counter_ >= 0; }
    bool display_enabled() const { return h_display_ && v_display_; }

    void reset() {
        address_register_ = 0;
        registers_.fill(0);
        column_ = 0;
        row_ = 0;
        raster_ = 0;
        char_addr_ = 0;
        line_addr_ = 0;
        next_line_addr_ = 0;
        hsync_counter_ = -1;
        vsync_counter_ = -1;
        vadj_counter_ = 0;
        h_display_ = true;
        v_display_ = true;
        in_vadj_ = false;
        had_vsync_this_row_ = false;
        field_count_ = 0;
        odd_field_ = true;
        frame_count_ = 0;
        do_even_frame_logic_ = false;  // Matches jsbeeb initial state
        prev_lightpen_ = false;
        fast_clock_ = false;
    }

private:
    // Check if current position is valid for vsync start/end in interlace mode
    // In interlace sync mode, vsync transitions only occur at specific points:
    // - Non-interlace mode: any character position is valid
    // - Interlace odd frame: any character position is valid
    // - Interlace even frame: only valid at half-scanline (column == R0/2)
    // This implements 312.5 scanline frames per Motorola 6845 datasheet figure 13.
    // See jsbeeb video.js:603-650 for reference implementation.
    //
    // Matches jsbeeb's isVsyncPoint calculation (video.js:616):
    //   const isVsyncPoint = !isInterlace || !this.doEvenFrameLogic || halfR0Hit;
    // When doEvenFrameLogic is true, vsync can only start/end at half-scanline.
    bool is_vsync_point() const {
        if (!is_interlace_sync()) {
            return true;  // Non-interlace: any position is valid
        }

        if (!do_even_frame_logic_) {
            return true;  // !doEvenFrameLogic: any position is valid
        }

        // Even frame logic active: only valid at half-scanline
        return column_ == (registers_[R0_HTOTAL] / 2);
    }

    void end_of_scanline() {
        // Increment vsync counter per scanline (end check happens in tick() for interlace timing)
        if (vsync_counter_ >= 0) {
            ++vsync_counter_;
        }

        // Check for end of character row
        bool at_max_raster;
        if (interlace_sync_and_video()) {
            // In interlace mode, compare halved values (per B2)
            at_max_raster = (raster_ >> 1) == ((registers_[R9_MAX_SCANLINE] & 0x1F) >> 1);
        } else {
            at_max_raster = (raster_ == (registers_[R9_MAX_SCANLINE] & 0x1F));
        }

        if (at_max_raster) {
            // Latch line address for next row
            line_addr_ = next_line_addr_;
        }

        // Increment raster
        if (interlace_sync_and_video()) {
            raster_ += 2;
        } else {
            ++raster_;
        }
        raster_ &= 0x1F;

        if (at_max_raster && !in_vadj_) {
            end_of_row();
        }

        // Handle end of vertical displayed (R6 check)
        // This must happen BEFORE the v_adjust/end_of_frame logic so that
        // end_of_frame() can reset v_display_ to true for the new frame.
        // With R6=0, this allows the first scanline to display before v_display
        // is cleared at the end of that scanline.
        const bool r6_hit = (row_ == registers_[R6_VDISPLAYED]);
        if (r6_hit && v_display_) {
            v_display_ = false;
            // Increment by 2 in non-interlace mode to normalize to 50Hz field rate.
            // In interlace mode, this triggers twice per frame (once per field).
            // In non-interlace mode, this triggers once per frame, so we double it.
            field_count_ += interlace_sync_and_video() ? 1 : 2;
            // Toggle field for interlace mode
            odd_field_ = !odd_field_;
            // Increment frame count (matches jsbeeb's frameCount++)
            ++frame_count_;
        }

        // Update doEvenFrameLogic at R6 or R7 hit (matches jsbeeb video.js:771-772)
        // This quirk means even frame interlace logic activates when either R6 or R7 is seen.
        const bool r7_hit = (row_ == registers_[R7_VSYNC_POS]);
        if (r6_hit || r7_hit) {
            do_even_frame_logic_ = (frame_count_ & 1) != 0;
        }

        // Check for vertical adjust period
        if (row_ == registers_[R4_VTOTAL] + 1 && !in_vadj_) {
            if (registers_[R5_VTOTAL_ADJ] > 0) {
                in_vadj_ = true;
                vadj_counter_ = 0;
            } else {
                end_of_frame();
            }
        }

        if (in_vadj_) {
            ++vadj_counter_;
            // Use > not >= because vadj_counter is incremented on the same
            // end_of_scanline call that enters v_adjust, so we need one extra count
            if (vadj_counter_ > registers_[R5_VTOTAL_ADJ]) {
                in_vadj_ = false;
                end_of_frame();
            }
        }

        // Reset character address to start of line
        char_addr_ = line_addr_;
    }

    void end_of_row() {
        ++row_;
        // In interlace mode, maintain field parity
        if (interlace_sync_and_video()) {
            raster_ = odd_field_ ? 0 : 1;
        } else {
            raster_ = 0;
        }
        had_vsync_this_row_ = false;
    }

    void end_of_frame() {
        row_ = 0;
        // In interlace mode, start at raster 0 (odd field) or 1 (even field)
        if (interlace_sync_and_video()) {
            raster_ = odd_field_ ? 0 : 1;
        } else {
            raster_ = 0;
        }

        // Reload start address
        line_addr_ = screen_start();
        next_line_addr_ = line_addr_;
        char_addr_ = line_addr_;

        v_display_ = true;
        in_vadj_ = false;
        had_vsync_this_row_ = false;
        // Note: do_even_frame_logic_ is NOT updated here - it's updated at R6/R7 hit
        // to match jsbeeb's behavior where doEvenFrameLogic changes at those points.
    }

    // Register masks (write masks for each register)
    static constexpr std::array<uint8_t, 18> register_masks_ = {
        0xFF, // R0  - Horizontal total
        0xFF, // R1  - Horizontal displayed
        0xFF, // R2  - Horizontal sync position
        0xFF, // R3  - Sync widths
        0x7F, // R4  - Vertical total (7 bits)
        0x1F, // R5  - Vertical total adjust (5 bits)
        0x7F, // R6  - Vertical displayed (7 bits)
        0x7F, // R7  - Vertical sync position (7 bits)
        0xF3, // R8  - Interlace mode (bits 0,1,4,5,6,7)
        0x1F, // R9  - Max scanline (5 bits)
        0x7F, // R10 - Cursor start (7 bits)
        0x1F, // R11 - Cursor end (5 bits)
        0x3F, // R12 - Start address high (6 bits)
        0xFF, // R13 - Start address low
        0x3F, // R14 - Cursor high (6 bits)
        0xFF, // R15 - Cursor low
        0x3F, // R16 - Light pen high (read-only, 6 bits)
        0xFF, // R17 - Light pen low (read-only)
    };

    // Registers
    uint8_t address_register_ = 0;
    std::array<uint8_t, 18> registers_{};

    // Timing state
    uint8_t column_ = 0;           // Horizontal character counter (0 to R0)
    uint8_t row_ = 0;              // Vertical character counter (0 to R4)
    uint8_t raster_ = 0;           // Scanline within character row (0 to R9)
    uint16_t char_addr_ = 0;       // Current character address (MA0-MA13)
    uint16_t line_addr_ = 0;       // Address at start of current scanline
    uint16_t next_line_addr_ = 0;  // Address for start of next character row

    // Sync state
    int8_t hsync_counter_ = -1;    // -1 = not in hsync, 0+ = counting
    int8_t vsync_counter_ = -1;    // -1 = not in vsync, 0+ = counting
    uint8_t vadj_counter_ = 0;     // Vertical adjust scanline counter

    // Display enable
    bool h_display_ = true;
    bool v_display_ = true;
    bool in_vadj_ = false;
    bool had_vsync_this_row_ = false;

    // Field counter for cursor blink timing (always increments at 50Hz field rate)
    uint8_t field_count_ = 0;

    // Field tracking for interlace mode
    bool odd_field_ = true;  // Odd field (first) or even field (second)

    // Frame counter for interlace timing (matches jsbeeb's frameCount)
    // Incremented at R6 hit (when v_display_ goes false)
    uint32_t frame_count_ = 0;

    // Even frame logic flag for vsync timing (matches jsbeeb's doEvenFrameLogic)
    // When true, vsync can only start/end at half-scanline position (column == R0/2)
    // Updated at R6 and R7 hits based on frame_count_ parity
    bool do_even_frame_logic_ = false;

    // Light pen
    bool prev_lightpen_ = false;

    // Clock rate (set by video ULA)
    bool fast_clock_ = false;

    // Debug callback for cursor analysis
    std::optional<CursorDebugCallback> cursor_debug_callback_;
};

} // namespace beebium
