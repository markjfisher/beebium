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

#include "LoopbackSerialExtension.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>

#include <iostream>

namespace beebium {

std::span<const std::string_view> LoopbackSerialExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"serial-port"};
    return deps;
}

std::span<const std::string_view> LoopbackSerialExtension::provides() const {
    return {};
}

void LoopbackSerialExtension::init(ExtensionContext& ctx) {
    ctx.get<SerialPort>().attach(*this);
    std::cout << "loopback-serial: echoing transmitted bytes back to the receiver\n";
}

void LoopbackSerialExtension::shutdown() {
    queue_.clear();
}

}  // namespace beebium
