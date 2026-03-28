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

#ifndef BEEBIUM_EXTENSION_PLUGIN_LOADER_HPP
#define BEEBIUM_EXTENSION_PLUGIN_LOADER_HPP

#include "ExtensionManifest.hpp"
#include "ExtensionRegistry.hpp"
#include "PeripheralExtension.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace beebium {

// Scans an extension directory for manifest.json files, and loads
// requested extensions by name via dlopen/LoadLibrary.
//
// Plugin loading is opt-in: scan_manifests() enumerates available
// extensions without loading any code; load_extension() loads a
// specific extension by manifest.
class PluginLoader {
public:
    ~PluginLoader();

    // Non-copyable, non-movable (owns dlopen handles)
    PluginLoader() = default;
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    // Scan directory for manifest.json files. No code is loaded.
    // Each subdirectory or direct manifest.json in the directory is checked.
    std::vector<ExtensionManifest> scan_manifests(const std::filesystem::path& extension_dirpath);

    // Load a specific extension from its manifest. The shared library
    // named in the manifest is loaded and the entry point called.
    // The resulting extension is registered with the given registry.
    void load_extension(const ExtensionManifest& manifest, ExtensionRegistry& registry);

    // Find a manifest by name from a previously scanned list.
    static const ExtensionManifest* find_manifest(
        const std::vector<ExtensionManifest>& manifests,
        std::string_view name);

private:
    struct LoadedPlugin {
        void* handle = nullptr;
        std::string name;
    };
    std::vector<LoadedPlugin> loaded_plugins_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_PLUGIN_LOADER_HPP
