// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXTENSION_TEST_SCRATCH_RAM_HPP
#define BEEBIUM_EXTENSION_TEST_SCRATCH_RAM_HPP

#include "ExtensionContext.hpp"
#include "OneMHzBusDevice.hpp"
#include "OneMHzBusPort.hpp"
#include "PeripheralExtension.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace beebium {

// 8 bytes of scratch RAM at FRED offsets 0x50-0x57 (absolute 0xFC50-0xFC57).
//
// This extension exists to validate the PeripheralExtension framework
// end-to-end: extension registration, dependency resolution, address claiming,
// read/write dispatch through the MemoryMap, and 1 MHz bus ticking.
// It is a permanent test fixture, not a real BBC Micro peripheral.
class TestScratchRam : public PeripheralExtension,
                       public OneMHzBusDevice {
public:
    static constexpr uint16_t kBaseOffset = 0x50;
    static constexpr uint16_t kEndOffset = 0x57;
    static constexpr uint16_t kSize = kEndOffset - kBaseOffset + 1;  // 8 bytes

    std::string_view name() const override { return "test-scratch-ram"; }

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"1mhz-bus"};
        return deps;
    }

    std::span<const std::string_view> provides() const override { return {}; }

    void init(ExtensionContext& ctx) override {
        ctx.get<OneMHzBusPort>().claim_addresses(kBaseOffset, kEndOffset, *this);
    }

    void shutdown() override {}

    // OneMHzBusDevice interface

    uint8_t read(uint16_t offset) override {
        return ram_[offset - kBaseOffset];
    }

    void write(uint16_t offset, uint8_t value) override {
        ram_[offset - kBaseOffset] = value;
    }

    // Direct access for testing
    uint8_t peek(uint16_t index) const { return ram_[index]; }
    void poke(uint16_t index, uint8_t value) { ram_[index] = value; }

private:
    std::array<uint8_t, kSize> ram_{};
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_TEST_SCRATCH_RAM_HPP
