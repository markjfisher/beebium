// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include <beebium/extension/OneMHzBusPort.hpp>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace beebium {

namespace {
std::string to_hex(uint16_t value) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", value);
    return buf;
}
}  // anonymous namespace

void OneMHzBusPort::claim_addresses(uint16_t base, uint16_t end, OneMHzBusDevice& device) {
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

void OneMHzBusPort::tick() {
    for (auto* dev : tickable_devices_) {
        dev->tick();
    }
    irq_dirty_ = true;
}

bool OneMHzBusPort::irq_pending() const {
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

}  // namespace beebium
