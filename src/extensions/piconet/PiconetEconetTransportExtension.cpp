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

#include "PiconetEconetTransportExtension.hpp"

#include "beebium/econet/piconet/PiconetConfig.hpp"
#include "beebium/econet/piconet/PosixSerialPort.hpp"

#include <iostream>
#include <string>

namespace beebium {

std::unique_ptr<NetworkBackend>
PiconetEconetTransportExtension::create_backend(std::uint8_t station) {
    auto device_path = config_value("device_path");
    if (!device_path || device_path->empty()) {
        std::cerr << "Piconet extension: missing required parameter 'device_path'\n";
        return nullptr;
    }

    std::string path(*device_path);
    auto serial = std::make_unique<piconet::PosixSerialPort>(path);
    if (!serial->is_open()) {
        std::cerr << "Piconet extension: failed to open device " << path << "\n";
        return nullptr;
    }

    return std::make_unique<PiconetBackend>(
        piconet::PiconetConfig{path, station},
        std::move(serial));
}

}  // namespace beebium
