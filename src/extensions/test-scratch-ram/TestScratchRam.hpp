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

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/OneMHzBusDevice.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

class ScratchRamServiceImpl;

// 8 bytes of scratch RAM at FRED offsets 0x80-0x87 (absolute 0xFC80-0xFC87).
// This is within the "Test Hardware" range (FC80-FC8F) defined in the
// Acorn 1 MHz Bus Application Note 003.
//
// This extension exists to validate the PeripheralExtension framework
// end-to-end: extension registration, dependency resolution, address claiming,
// read/write dispatch through the MemoryMap, gRPC service provision, and
// 1 MHz bus ticking. It is a permanent test fixture, not a real BBC Micro
// peripheral.
class TestScratchRam : public PeripheralExtension,
                       public OneMHzBusDevice {
public:
    static constexpr uint16_t kBaseOffset = 0x80;
    static constexpr uint16_t kEndOffset = 0x87;
    static constexpr uint16_t kSize = kEndOffset - kBaseOffset + 1;  // 8 bytes

    TestScratchRam();
    ~TestScratchRam() override;

    static std::unique_ptr<TestScratchRam> create();

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"1mhz-bus"};
        return deps;
    }

    std::span<const std::string_view> provides() const override { return {}; }

    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    std::vector<grpc::Service*> grpc_services() override;

    // OneMHzBusDevice interface

    uint8_t read(uint16_t offset) override;
    void write(uint16_t offset, uint8_t value) override;

    // Direct access for testing and service implementation
    uint8_t peek(uint16_t index) const { return ram_[index]; }
    void poke(uint16_t index, uint8_t value) { ram_[index] = value; }

private:
    std::array<uint8_t, kSize> ram_{};
    std::unique_ptr<ScratchRamServiceImpl> service_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_TEST_SCRATCH_RAM_HPP
