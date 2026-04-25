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
        // Log the failure but still construct a PiconetBackend in a
        // closed state. The ModalEditor on the Piconet panel needs a
        // live backend to call request_reopen() against; without this,
        // a wrong-path-at-startup is unrecoverable from the UI. The
        // backend's own open_error_message() carries the OS-level
        // diagnosis that PiconetUi surfaces on the Indicator; the
        // extension's open_error_message_ (used for the "no
        // device_path configured" case below) stays empty.
        std::cerr << "Piconet extension: failed to open device " << path
                  << ": " << serial->open_error() << "\n";
    }

    // Factory used by PiconetBackend::process_pending_reopen() to build
    // a replacement SerialPort when the user re-points the adapter via
    // the Extension UI. Mirrors the platform selection used for the
    // initial open above so tests / production agree on which
    // SerialPort class drives reopens.
    auto serial_factory = [](const std::string& new_path)
        -> std::unique_ptr<piconet::SerialPort> {
#ifdef _WIN32
        return std::make_unique<piconet::Win32SerialPort>(new_path);
#else
        return std::make_unique<piconet::PosixSerialPort>(new_path);
#endif
    };

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
        std::move(serial_factory),
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
