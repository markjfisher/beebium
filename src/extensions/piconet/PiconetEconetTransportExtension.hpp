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

#ifndef BEEBIUM_EXTENSIONS_PICONET_PICONET_ECONET_TRANSPORT_EXTENSION_HPP
#define BEEBIUM_EXTENSIONS_PICONET_PICONET_ECONET_TRANSPORT_EXTENSION_HPP

// Plugin EconetTransportExtension for the Piconet USB-CDC bridge to a
// real Econet wire. POSIX-only (PosixSerialPort is the only SerialPort
// implementation today). Constructed by plugin_entry.cpp via the
// generic beebium_create_extension ABI; the framework hands us our
// config map (parsed from --piconet device_path=/dev/ttyX or the
// equivalent preset entry), and create_backend() opens the device
// and constructs a PiconetBackend wrapping it.

#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/extension/EconetTransportExtension.hpp"

#include <cstdint>
#include <memory>

namespace beebium {

class PiconetEconetTransportExtension : public EconetTransportExtension {
public:
    PiconetEconetTransportExtension() = default;

    // Construct a PiconetBackend from the current config. Returns
    // nullptr if device_path is missing or the serial open fails;
    // ServerMain treats that as "transport configured but unavailable"
    // and installs a disconnected stub instead.
    std::unique_ptr<NetworkBackend> create_backend(std::uint8_t station) override;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_PICONET_PICONET_ECONET_TRANSPORT_EXTENSION_HPP
