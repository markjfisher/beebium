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

#include "ExtensionManifest.hpp"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace grpc { class Service; }

namespace beebium {

class ExtensionContext;

// Base class for all peripheral extensions. Handles identity, dependencies,
// lifecycle, and gRPC service registration. I/O methods are NOT on this class
// -- each extension point type defines its own device callback interface
// (OneMHzBusDevice, UserPortDevice, etc.) which extensions implement via
// composition rather than inheritance.
//
// The manifest is the single source of truth for extension metadata.
// For dynamically loaded extensions it is read from manifest.json;
// for built-in extensions it is constructed programmatically.
class PeripheralExtension {
public:
    virtual ~PeripheralExtension() = default;

    // Set the manifest (called by the framework before init).
    void set_manifest(ExtensionManifest manifest) { manifest_ = std::move(manifest); }

    // Access the manifest.
    const ExtensionManifest& manifest() const { return manifest_; }

    // Set instance configuration (called by the framework before init).
    // Config is parsed from CLI arguments or preset files.
    void set_config(std::map<std::string, std::string> config) { config_ = std::move(config); }

    // Access a single config value by key.
    std::optional<std::string_view> config_value(std::string_view key) const {
        auto it = config_.find(std::string(key));
        if (it != config_.end()) {
            return std::string_view(it->second);
        }
        return std::nullopt;
    }

    // Access all config values.
    const std::map<std::string, std::string>& config() const { return config_; }

    // Instance ID (from config["id"] or empty if not yet assigned).
    std::string_view id() const {
        auto it = config_.find("id");
        return (it != config_.end()) ? std::string_view(it->second) : std::string_view{};
    }

    // Display label (from config["label"], falls back to id).
    std::string_view label() const {
        auto it = config_.find("label");
        if (it != config_.end() && !it->second.empty()) {
            return std::string_view(it->second);
        }
        return id();
    }

    // Extension name (default reads from manifest; can be overridden).
    virtual std::string_view name() const { return manifest_.name; }

    // Human-readable description (from manifest).
    std::string_view description() const { return manifest_.description; }

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

protected:
    ExtensionManifest manifest_;
    std::map<std::string, std::string> config_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_PERIPHERAL_EXTENSION_HPP
