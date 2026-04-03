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

#ifndef BEEBIUM_EXTENSION_ONE_MHZ_BUS_PORT_HPP
#define BEEBIUM_EXTENSION_ONE_MHZ_BUS_PORT_HPP

#include "OneMHzBusDevice.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace beebium {

// Port handle for the 1 MHz bus (FRED/JIM region, 0xFC00-0xFDFF).
//
// Satisfies the MemoryMappedDevice concept (read/write with uint16_t offset).
// Dispatches reads and writes to devices that have claimed address ranges.
// Unclaimed addresses return 0xFF on read (74LS245 transceiver behaviour)
// and silently ignore writes.
//
// Replaces the former FredJimRegion stub in hardware policy classes.
class OneMHzBusPort {
public:
    static constexpr uint16_t kSize = 512;  // 0xFC00-0xFDFF

    OneMHzBusPort() {
        device_map_.fill(nullptr);
    }

    // MemoryMappedDevice interface (called by MemoryMap region binding).
    // Offset is relative to 0xFC00.

    uint8_t read(uint16_t offset) {
        if (auto* dev = device_map_[offset]) {
            return dev->read(offset);
        }
        return 0xFF;
    }

    void write(uint16_t offset, uint8_t value) {
        if (auto* dev = device_map_[offset]) {
            dev->write(offset, value);
        }
    }

    // Extension framework interface.

    // Register a device for a contiguous range of offsets [base, end] inclusive.
    // Offsets are relative to 0xFC00.
    // Throws std::runtime_error if any address in the range is already claimed.
    void claim_addresses(uint16_t base, uint16_t end, OneMHzBusDevice& device) {
        if (base > end || end >= kSize) {
            throw std::runtime_error(
                "OneMHzBusPort: invalid address range 0x"
                + to_hex(base) + "-0x" + to_hex(end));
        }

        for (uint16_t offset = base; offset <= end; ++offset) {
            if (device_map_[offset] != nullptr) {
                throw std::runtime_error(
                    "OneMHzBusPort: address 0x" + to_hex(offset)
                    + " already claimed");
            }
        }

        for (uint16_t offset = base; offset <= end; ++offset) {
            device_map_[offset] = &device;
        }

        if (std::find(tickable_devices_.begin(), tickable_devices_.end(), &device)
                == tickable_devices_.end()) {
            tickable_devices_.push_back(&device);
        }
    }

    // Tick all registered devices (called at 1 MHz from poll_nmi).
    // Invalidates the IRQ cache since device state may have changed.
    void tick() {
        for (auto* dev : tickable_devices_) {
            dev->tick();
        }
        irq_dirty_ = true;
    }

    // Query whether any device has claimed the given offset.
    bool is_claimed(uint16_t offset) const {
        return offset < kSize && device_map_[offset] != nullptr;
    }

    // Poll IRQ status from all attached devices.
    // Returns true if any device on the 1 MHz bus is asserting IRQ.
    // Used by the IrqAggregator to include 1 MHz bus IRQs.
    // Caches the result between tick() calls to avoid redundant virtual
    // dispatch at 2 MHz when tick() only runs at 1 MHz.
    bool irq_pending() const {
        if (irq_dirty_) {
            cached_irq_ = false;
            for (auto* dev : tickable_devices_) {
                if (dev->irq_pending()) {
                    cached_irq_ = true;
                    break;
                }
            }
            irq_dirty_ = false;
        }
        return cached_irq_;
    }

private:
    static std::string to_hex(uint16_t value) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%04X", value);
        return buf;
    }

    std::array<OneMHzBusDevice*, kSize> device_map_;
    std::vector<OneMHzBusDevice*> tickable_devices_;
    mutable bool cached_irq_ = false;
    mutable bool irq_dirty_ = true;
};

// Concept to detect hardware that has a 1 MHz bus port.
template<typename T>
concept HasOneMHzBus = requires(T& hw) {
    { hw.one_mhz_bus() } -> std::convertible_to<OneMHzBusPort&>;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ONE_MHZ_BUS_PORT_HPP
