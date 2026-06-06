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

#ifndef BEEBIUM_SERVER_BUILTIN_EXTENSIONS_HPP
#define BEEBIUM_SERVER_BUILTIN_EXTENSIONS_HPP

// Built-in extensions: extensions compiled directly into the server
// rather than loaded from an on-disk plugin.
//
// Anything that *could* be a plugin should be. Entries here describe
// extensions that the server has link-time knowledge of -- typically
// because ServerMain reaches into their type via dynamic_cast or
// because they PUBLIC-link libraries (like beebium_service) that
// cannot safely coexist with plugin copies. A cleaner plugin ABI (for
// example, a virtual-interface adapter for cross-processor debugger
// wiring) would unblock converting the remaining built-ins to plugins;
// see the acorn-65c02-coprocessor notes below.

#include "AunEconetTransportExtension.hpp"
#include "HostSerialExtension.hpp"
#include "Ip232SerialExtension.hpp"
#include "RpcSerialExtension.hpp"
#include "SecondProcessor65C02Extension.hpp"
#include "beebium/extension/Extension.hpp"
#include "beebium/extension/ExtensionManifest.hpp"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace beebium::builtin_extensions {

struct Entry {
    ExtensionManifest manifest;
    std::function<std::unique_ptr<Extension>()> factory;
};

namespace detail {

inline std::vector<Entry> make_entries() {
    std::vector<Entry> result;

    // Acorn 65C02 3 MHz second processor (Tube co-processor).
    //
    // Built-in rather than a plugin because ServerMain does
    // dynamic_cast<SecondProcessor65C02Extension*> on the loaded
    // extension to wire cross-processor debugger callbacks, and the
    // extension's inline non-virtual integration methods
    // (running(), parasite_pause_callback(), wire_counterpart_stop())
    // reach into private members that are not exposed through a
    // cross-DLL virtual interface.
    {
        ExtensionManifest m;
        m.name = "acorn-65c02-coprocessor";
        m.display_name = "Acorn 65C02 Co-processor";
        m.description = "Acorn 65C02 3 MHz second processor";
        m.cli_name = "tube-65c02";
        m.extension_kind = "peripheral";
        m.parameters.push_back(
            {"rom", "filepath", "Path to 2KB Tube client ROM image",
             -1, false, false, ""});
        result.push_back({std::move(m),
                          [] { return std::unique_ptr<Extension>(
                              new SecondProcessor65C02Extension()); }});
    }

    // AUN UDP econet transport.
    //
    // Built-in rather than a plugin primarily for CLI-test ergonomics:
    // many parse_start_arguments tests use a synthetic argv
    // (argv[0] = "beebium") which cannot auto-discover plugins via
    // <exe-dir>/extensions/ lookup. Making AUN a plugin would need
    // those tests to pre-populate an extension directory, which is a
    // non-trivial refactor. A future test-side cleanup could unblock
    // conversion -- the runtime auto-discovery is in place and would
    // work identically for an AUN plugin.
    {
        ExtensionManifest m;
        m.name = "aun";
        m.display_name = "AUN Transport";
        m.description = "AUN (Acorn Universal Networking) UDP econet transport";
        m.cli_name = "aun";
        m.extension_kind = "econet-transport";
        m.parameters.push_back(
            {"port", "string",
             "UDP port to bind (decimal, or 'none' to disable)",
             -1, false, false, "32768"});
        m.parameters.push_back(
            {"net", "string",
             "Local Econet net number this station belongs to (0..127)",
             -1, false, false, "0"});
        m.parameters.push_back(
            {"map", "string",
             "Peer entry 'net.stn@ip@port' (repeatable)",
             -1, false, /*is_list=*/true, ""});
        result.push_back({std::move(m),
                          [] { return std::unique_ptr<Extension>(
                              new AunEconetTransportExtension()); }});
    }

    // Host serial bridge.
    //
    // Built-in for the same CLI-test ergonomics reason as AUN: --host-serial
    // must be recognised without a plugin directory. Attaches a HostSerial-
    // Endpoint to the serial port; when it claims the port, SerialService sees
    // is_occupied() and yields (reports status only).
    {
        ExtensionManifest m;
        m.name = "host-serial";
        m.display_name = "Host Serial";
        m.description =
            "Connect the BBC serial port (RS423) to a host PTY or serial device";
        m.cli_name = "host-serial";
        m.extension_kind = "peripheral";
        m.parameters.push_back(
            {"mode", "string",
             "'pty' (create a pseudo-terminal) or 'device' (open an existing "
             "serial/pty device path)",
             -1, false, false, "pty"});
        m.parameters.push_back(
            {"path", "string",
             "pty: optional stable symlink to the pty slave; "
             "device: the serial device path to open",
             -1, false, false, ""});
        m.parameters.push_back(
            {"baud", "integer", "device line speed (ignored for pty)",
             -1, false, false, "19200"});
        m.parameters.push_back(
            {"tx_buffer", "integer",
             "transmit buffer size in bytes; the BBC's /CTS asserts at/above it",
             -1, false, false, "4096"});
        result.push_back({std::move(m),
                          [] { return std::unique_ptr<Extension>(
                              new HostSerialExtension()); }});
    }

    // rpc-serial: client-driven serial peer (the RPC client is the device).
    // Built-in for the same CLI-test ergonomics reason as the other serial
    // extensions. Drive it via the RpcSerial gRPC service.
    {
        ExtensionManifest m;
        m.name = "rpc-serial";
        m.display_name = "RPC Serial";
        m.description =
            "Client-driven serial peer: inject/collect bytes via the RpcSerial service";
        m.cli_name = "rpc-serial";
        m.extension_kind = "peripheral";
        m.parameters.push_back(
            {"tx_buffer", "integer",
             "transmit buffer size in bytes; the BBC's /CTS asserts at/above it",
             -1, false, false, "4096"});
        result.push_back({std::move(m),
                          [] { return std::unique_ptr<Extension>(
                              new RpcSerialExtension()); }});
    }

    // ip232-serial: connect the BBC serial port to a tcpser-style IP232 server
    // over TCP. Built-in for the same CLI-test ergonomics reason as the other
    // serial extensions. The network sibling of host-serial.
    //
    // host and port are separate parameters (not a host:port URL) because the
    // extension arg parser splits values on ':'.
    {
        ExtensionManifest m;
        m.name = "ip232-serial";
        m.display_name = "IP232 Serial";
        m.description =
            "Connect the BBC serial port (RS423) to a tcpser-style IP232 server "
            "over TCP";
        m.cli_name = "ip232-serial";
        m.extension_kind = "peripheral";
        m.parameters.push_back(
            {"host", "string", "IP232 server hostname or address",
             -1, false, false, "localhost"});
        m.parameters.push_back(
            {"port", "integer", "IP232 server TCP port",
             -1, false, false, "25232"});
        m.parameters.push_back(
            {"mode", "string",
             "'ip232' (0xFF-escaped, persistent connection) or 'raw' (pure byte "
             "pipe, connect/disconnect on RTS)",
             -1, false, false, "ip232"});
        m.parameters.push_back(
            {"handshake", "boolean",
             "convey the BBC's RTS line to the server via the 0xFF escape "
             "(ip232 mode only)",
             -1, false, false, "true"});
        m.parameters.push_back(
            {"tx_buffer", "integer",
             "transmit buffer size in bytes; the BBC's /CTS asserts at/above it",
             -1, false, false, "4096"});
        result.push_back({std::move(m),
                          [] { return std::unique_ptr<Extension>(
                              new Ip232SerialExtension()); }});
    }

    return result;
}

}  // namespace detail

// Returns the table of built-in extensions. The list is constructed on
// first call and cached for the program's lifetime.
inline const std::vector<Entry>& entries() {
    static const std::vector<Entry> table = detail::make_entries();
    return table;
}

// Look up an entry by canonical extension name (manifest.name).
// Returns nullptr if no built-in by that name exists.
inline const Entry* find(std::string_view name) {
    for (const auto& e : entries()) {
        if (e.manifest.name == name) return &e;
    }
    return nullptr;
}

}  // namespace beebium::builtin_extensions

#endif  // BEEBIUM_SERVER_BUILTIN_EXTENSIONS_HPP
