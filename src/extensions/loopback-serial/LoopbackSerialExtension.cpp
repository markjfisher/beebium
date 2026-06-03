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

#include "SerialLoopbackExtension.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>

#include <iostream>
#include <memory>

namespace beebium {

std::span<const std::string_view> SerialLoopbackExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"serial-port"};
    return deps;
}

std::span<const std::string_view> SerialLoopbackExtension::provides() const {
    return {};
}

void SerialLoopbackExtension::init(ExtensionContext& ctx) {
    endpoint_ = std::make_unique<LoopbackSerialEndpoint>();
    ctx.get<SerialPort>().attach(*endpoint_);
    std::cout << "serial-loopback: echoing transmitted bytes back to the receiver\n";
}

void SerialLoopbackExtension::shutdown() {
    endpoint_.reset();
}

}  // namespace beebium
