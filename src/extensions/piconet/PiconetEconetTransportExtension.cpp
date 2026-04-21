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

#ifdef _WIN32
#include "beebium/econet/piconet/Win32SerialPort.hpp"
#else
#include "beebium/econet/piconet/PosixSerialPort.hpp"
#endif

#ifdef BEEBIUM_BUILD_SERVICE
#include "PiconetService.hpp"
#endif

#include <iostream>
#include <string>

namespace beebium {

PiconetEconetTransportExtension::PiconetEconetTransportExtension() = default;
PiconetEconetTransportExtension::~PiconetEconetTransportExtension() = default;

std::unique_ptr<NetworkBackend>
PiconetEconetTransportExtension::create_backend(std::uint8_t station) {
    auto device_path = config_value("device_path");
    if (!device_path || device_path->empty()) {
        std::cerr << "Piconet extension: missing required parameter 'device_path'\n";
        open_error_message_ = "missing 'device_path' parameter";
        return nullptr;
    }

    std::string path(*device_path);
#ifdef _WIN32
    auto serial = std::make_unique<piconet::Win32SerialPort>(path);
#else
    auto serial = std::make_unique<piconet::PosixSerialPort>(path);
#endif
    if (!serial->is_open()) {
        // Capture the OS-level error from the SerialPort so PiconetUi
        // can surface it on the Indicator. Falls back to a generic
        // message if the serial port didn't record one (shouldn't
        // happen in practice -- open() / CreateFile() always sets an
        // errno / GetLastError() on failure).
        auto why = serial->open_error();
        open_error_message_ = why.empty() ? std::string("unknown error")
                                          : std::string(why);
        std::cerr << "Piconet extension: failed to open device " << path
                  << ": " << open_error_message_ << "\n";
        return nullptr;
    }

    // Wire the backend's async-state-change hook to the UI's
    // mark_dirty(). Without this the panel View stays frozen across
    // hot-unplug events -- the reader thread closes the serial port
    // but the framework's poll loop only sees a revision change when
    // mark_dirty() is called. Captures `this` because the extension
    // owns both ui_ and the backend (via the unique_ptr handed to
    // EconetSocket); the callback's lifetime is bounded by both.
    auto backend = std::make_unique<PiconetBackend>(
        piconet::PiconetConfig{path, station},
        std::move(serial),
        [this]{ ui_.mark_dirty(); });
    backend_ = backend.get();  // non-owning; ownership goes to EconetSocket
    open_error_message_.clear();
    return backend;
}

std::vector<grpc::Service*> PiconetEconetTransportExtension::grpc_services() {
#ifdef BEEBIUM_BUILD_SERVICE
    if (!service_) {
        service_ = std::make_unique<PiconetServiceImpl>(*this);
    }
    return {service_.get()};
#else
    return {};
#endif
}

}  // namespace beebium
