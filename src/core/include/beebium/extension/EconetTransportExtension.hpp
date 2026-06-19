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

#ifndef BEEBIUM_EXTENSION_ECONET_TRANSPORT_EXTENSION_HPP
#define BEEBIUM_EXTENSION_ECONET_TRANSPORT_EXTENSION_HPP

#include "Export.hpp"
#include "Extension.hpp"
#include "../econet/NetworkBackend.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace grpc { class Service; }

namespace beebium {

class EconetStatusResponse;  // forward decl; defined in econet.proto

// Base class for Econet transport extensions: extensions that produce a
// NetworkBackend implementation for EconetSocket. Identity (manifest,
// config, name, id, label) is inherited from Extension; this class adds
// the transport-construction lifecycle.
//
// Each transport extension provides exactly one NetworkBackend per
// machine instance (e.g. one AUN UDP socket, one Piconet USB device).
// The future Acorn-Econet-Bridge machine type would have two transport
// extensions, one per ADLC -- nothing here precludes that.
class BEEBIUM_EXT_API EconetTransportExtension : public Extension {
public:
    // Out-of-line destructor anchors the vtable in beebium_extension_api
    // (required by MSVC for dllimport-decorated polymorphic classes).
    ~EconetTransportExtension() override;

    // Construct the backend. Called once per machine after extension
    // config is set, before EconetSocket::enable(). Returns nullptr on
    // construction failure (e.g. socket bind failed, device unavailable);
    // the caller decides what to do (typically print error and exit).
    virtual std::unique_ptr<NetworkBackend> create_backend(uint8_t station) = 0;

    // Forward station-ID changes (e.g. from gRPC SetStationId) so the
    // transport can update any of its own bookkeeping. Default no-op;
    // transports that need to push the change to a downstream device
    // (Piconet's SET_STATION command) override. Defined out-of-line in
    // EconetTransportExtension.cpp.
    virtual void on_station_id_changed(uint8_t new_station_id);

    // Whether this transport requires the emulation to run at real time (1x).
    // Default false: transports with no shared real-world clock (e.g. AUN over
    // UDP) work at any speed. A transport bridging to a real Econet line with
    // real stations (Piconet) overrides to true; the server then gates that
    // transport off while the speed is not 1x, since its protocol timing cannot
    // interleave with real peers in wall time at any other rate. See
    // docs/networking.md ("Emulation speed and real-time peers"). Defined
    // out-of-line in EconetTransportExtension.cpp.
    virtual bool requires_real_time_pacing() const;

    // A transport's client-facing API (e.g. AUN peer-list RPCs) is served
    // through the core's ExtensionRpc channel via rpc_dispatchers() (declared
    // on Extension), not by hosting a gRPC service here.
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ECONET_TRANSPORT_EXTENSION_HPP
