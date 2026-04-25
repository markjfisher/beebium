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

#include <iostream>
#include <string>

namespace beebium {

namespace {

// Single platform-selection helper used both for the initial open in
// create_backend and for the SerialFactory the backend uses to build
// a replacement during process_pending_reopen. Keeping the #ifdef in
// one place ensures the two paths can never drift to different
// SerialPort implementations.
std::unique_ptr<piconet::SerialPort> make_platform_serial(
    const std::string& path)
{
#ifdef _WIN32
    return std::make_unique<piconet::Win32SerialPort>(path);
#else
    return std::make_unique<piconet::PosixSerialPort>(path);
#endif
}

// Static thunk wired to PiconetBackend::on_async_state_change_. The
// userdata is the owning PiconetEconetTransportExtension*. Used as a
// plain function pointer rather than a capturing lambda boxed in a
// std::function to avoid the Windows AV described in
// docs/discussion/test-grpc-piconet-ui-windows-av.md -- on Windows a
// std::function dispatch from this hook (or from PiconetBackend's
// SerialFactory) crashes with the call landing in unmapped memory at a
// canonical-high-only address. A free function pointer lowers to a
// direct register-indirect CALL with no SBO, no vtable, and no
// _Mybase indirection, which sidesteps the bug.
void mark_ui_dirty_thunk(void* userdata) noexcept {
    static_cast<PiconetEconetTransportExtension*>(userdata)->ui()->mark_dirty();
}

}  // namespace

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
    auto serial = make_platform_serial(path);
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

    // Wire the backend's async-state-change hook to the UI's
    // mark_dirty() via a static thunk; `this` is passed as userdata so
    // the thunk can recover the PiconetEconetTransportExtension*.
    // Without this the panel View stays frozen across hot-unplug events
    // -- the reader thread closes the serial port but the framework's
    // poll loop only sees a revision change when mark_dirty() is
    // called. The extension owns both ui_ and the backend (via the
    // unique_ptr handed to EconetSocket), so the userdata's lifetime is
    // bounded by both. See mark_ui_dirty_thunk above for why this is a
    // free function rather than a capturing lambda.
    auto backend = std::make_unique<PiconetBackend>(
        piconet::PiconetConfig{path, station},
        std::move(serial),
        &make_platform_serial,
        &mark_ui_dirty_thunk,
        this);
    backend_ = backend.get();  // non-owning; ownership goes to EconetSocket
    open_error_message_.clear();
    return backend;
}

}  // namespace beebium
