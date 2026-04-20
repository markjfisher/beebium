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

#include "PiconetUi.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/extension/EconetTransportExtension.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace grpc { class Service; }

namespace beebium {

class PiconetServiceImpl;  // forward; defined when service is built

class PiconetEconetTransportExtension : public EconetTransportExtension {
public:
    PiconetEconetTransportExtension();
    ~PiconetEconetTransportExtension() override;

    // Construct a PiconetBackend from the current config. Returns
    // nullptr if device_path is missing or the serial open fails;
    // ServerMain treats that as "transport configured but unavailable"
    // and installs a disconnected stub instead.
    //
    // The constructed backend is also stashed as a non-owning pointer
    // so PiconetService can read its state. The owning unique_ptr is
    // handed off to EconetSocket; the raw pointer mirrors the AUN
    // extension's pattern and shares its lifetime caveat (becomes
    // dangling at machine shutdown, which only happens at process
    // exit).
    std::unique_ptr<NetworkBackend> create_backend(std::uint8_t station) override;

    // Returns the PiconetService bound to this extension's backend.
    // The server collects this and registers it with gRPC.
    std::vector<grpc::Service*> grpc_services() override;

    // Non-owning accessor used by PiconetService and PiconetUi.
    PiconetBackend* backend() { return backend_; }
    const PiconetBackend* backend() const { return backend_; }

    // Empty unless create_backend has been called and failed at the
    // device-open step. PiconetUi reads this to surface a human-readable
    // OS-level diagnosis ("No such file or directory", "Permission
    // denied", etc) on the Indicator's text field, rather than just
    // showing the generic "Adapter offline".
    const std::string& open_error_message() const { return open_error_message_; }

    // ExtensionUi hook: returns a stable pointer to the per-extension
    // PiconetUi. The framework reads from the View tree it produces and
    // dispatches validated events into its handle_event.
    ExtensionUi* ui() override { return &ui_; }

private:
    PiconetBackend* backend_ = nullptr;  // non-owning; lives in EconetSocket
    std::string open_error_message_;
    std::unique_ptr<PiconetServiceImpl> service_;
    PiconetUi ui_{*this};
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_PICONET_PICONET_ECONET_TRANSPORT_EXTENSION_HPP
