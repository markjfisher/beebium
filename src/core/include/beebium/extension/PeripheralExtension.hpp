// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXTENSION_PERIPHERAL_EXTENSION_HPP
#define BEEBIUM_EXTENSION_PERIPHERAL_EXTENSION_HPP

#include "Export.hpp"
#include "Extension.hpp"
#include "ExtensionManifest.hpp"

#include <span>
#include <string_view>
#include <vector>

// Cross-platform symbol export for plugin entry points
#ifdef _WIN32
#define BEEBIUM_PLUGIN_EXPORT __declspec(dllexport)
#else
#define BEEBIUM_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace grpc { class Service; }

namespace beebium {

class ExtensionContext;

// Base class for peripheral extensions: extensions that plug into a host
// machine's hardware port (1MHz bus, User VIA, Tube, etc.). Identity
// (manifest, config, name, id, label) is inherited from Extension; this
// class adds the port-attaching lifecycle. I/O methods are NOT on this
// class -- each extension point type defines its own device callback
// interface (OneMHzBusDevice, UserPortDevice, etc.) which extensions
// implement via composition rather than inheritance.
class BEEBIUM_EXT_API PeripheralExtension : public Extension {
public:
    // Extension points this extension requires (e.g. {"1mhz-bus"}).
    virtual std::span<const std::string_view> attaches_to() const = 0;

    // Extension points this extension creates (e.g. {"scsi"}).
    virtual std::span<const std::string_view> provides() const = 0;

    // Called after dependency resolution, in topological order.
    // The ExtensionContext provides access to the port handles declared
    // in attaches_to().
    virtual void init(ExtensionContext& ctx) = 0;

    // Called during shutdown in reverse init order.
    virtual void shutdown() = 0;

    // Zero or more gRPC services for client interaction.
    // Collected after init() and registered with the gRPC ServerBuilder.
    virtual std::vector<grpc::Service*> grpc_services() { return {}; }
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_PERIPHERAL_EXTENSION_HPP
