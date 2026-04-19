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

#ifndef BEEBIUM_EXTENSION_EXTENSION_HPP
#define BEEBIUM_EXTENSION_EXTENSION_HPP

#include "Export.hpp"
#include "ExtensionManifest.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace beebium {

class ExtensionUi;  // forward decl; defined in ExtensionUi.hpp

// Common base for all extension-point types. Holds manifest, instance
// config, and identity accessors. No lifecycle methods -- those belong
// on the derived extension-point classes (PeripheralExtension,
// EconetTransportExtension, etc.) because each kind of extension has
// its own activation contract with the host machine.
//
// The manifest is the single source of truth for extension metadata
// (name, description, parameter schema, extension kind). For
// dynamically loaded extensions it is read from manifest.json; for
// built-in extensions it is constructed programmatically.
//
// Instance config (the parameters passed via CLI or preset) is stored
// as a string -> string map. Extensions parse typed values out of it
// during their own initialisation.
class BEEBIUM_EXT_API Extension {
public:
    virtual ~Extension() = default;

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

    // Optional UI hook for the Extension UI framework. Returns nullptr
    // by default (extension exposes no UI). Concrete extensions that
    // want to surface a control panel in frontends override this and
    // return a stable pointer to their ExtensionUi implementation. The
    // returned pointer must outlive the Extension instance; typically
    // the implementation is a member of the concrete extension class.
    virtual ExtensionUi* ui() { return nullptr; }

protected:
    ExtensionManifest manifest_;
    std::map<std::string, std::string> config_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_EXTENSION_HPP
