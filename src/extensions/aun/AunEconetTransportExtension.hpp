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

#ifndef BEEBIUM_ECONET_AUN_ECONET_TRANSPORT_EXTENSION_HPP
#define BEEBIUM_ECONET_AUN_ECONET_TRANSPORT_EXTENSION_HPP

// Built-in EconetTransportExtension that wraps AunBackend (the existing
// UDP/AUN transport). Reads its configuration from the framework-managed
// config map populated from --aun port=...:map=... (CLI) or
// econet.transport.parameters (preset). Constructs and returns an
// AunBackend on demand; owns no machine state of its own.
//
// Manifest is constructed by builtin_extensions::entries() and includes
// two parameters:
//   port (string, default "32768"): UDP port to bind. Special value
//                                   "none" disables the network (no
//                                   socket bound; create_backend returns
//                                   nullptr).
//   map  (string, is_list):         Repeated peer entries of the form
//                                   "net;ip;port" (semicolon-separated
//                                   so the extension arg parser doesn't
//                                   split on the inner ':' chars).
//                                   The accumulated value is a
//                                   comma-separated list.

#include "beebium/econet/AunBackend.hpp"
#include "beebium/extension/EconetTransportExtension.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace grpc { class Service; }

namespace beebium {

class AunServiceImpl;  // forward; defined when service is built

class AunEconetTransportExtension : public EconetTransportExtension {
public:
    // Parameter parsing for an individual map= entry.
    struct PeerSpec {
        std::uint8_t net;
        std::uint8_t stn;
        std::uint32_t ip_addr_net_byte_order;
        std::uint16_t port;
    };

    AunEconetTransportExtension();
    ~AunEconetTransportExtension() override;

    // Construct an AunBackend from the current config. Returns nullptr if
    // port=none (network disabled) or if the underlying socket bind
    // fails. The caller (ServerMain) treats nullptr as "AUN configured
    // but transport unavailable" -- the EconetSocket is still constructed
    // in disconnected state.
    //
    // The constructed backend is also stashed as a non-owning pointer so
    // AunService can manipulate it. The owning unique_ptr is handed off
    // to EconetSocket; the extension does not extend the backend's
    // lifetime and the raw pointer becomes dangling once the machine
    // shuts down (which only happens at process exit).
    std::unique_ptr<NetworkBackend> create_backend(std::uint8_t station) override;

    // Returns the AunService bound to this extension's backend. The
    // server collects this and registers it with gRPC.
    std::vector<grpc::Service*> grpc_services() override;

    // Non-owning pointer to the AunBackend we constructed. Returns
    // nullptr before create_backend has run or if construction failed
    // (port=none, bind error). AunService consults this to find its
    // backend on each RPC.
    AunBackend* backend() { return backend_; }

    // Helpers exposed for unit testing -- these are pure functions of the
    // config map and don't touch sockets.

    // Parse the "port" config value. "none" -> std::nullopt. Empty or
    // missing -> AUN_DEFAULT_PORT. Otherwise parsed as decimal uint16.
    static std::optional<std::uint16_t> parse_port(const std::string& value);

    // Parse the "map" config value (comma-separated list of
    // "net;ip;port" entries). Returns the parsed list; entries that
    // can't be parsed are dropped with a warning to stderr.
    static std::vector<PeerSpec> parse_map(const std::string& value);

private:
    AunBackend* backend_ = nullptr;  // non-owning; lives in EconetSocket
    std::unique_ptr<AunServiceImpl> service_;  // lazily constructed
};

}  // namespace beebium

#endif  // BEEBIUM_ECONET_AUN_ECONET_TRANSPORT_EXTENSION_HPP
