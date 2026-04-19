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

#include "beebium/extension/PluginLoader.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

// Platform-specific dynamic loading
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// JSON parsing (nlohmann/json used elsewhere in the project)
#include <nlohmann/json.hpp>

namespace beebium {

namespace {

// Platform-specific shared library suffix
#ifdef __APPLE__
constexpr const char* kSharedLibSuffix = ".dylib";
#elif defined(_WIN32)
constexpr const char* kSharedLibSuffix = ".dll";
#else
constexpr const char* kSharedLibSuffix = ".so";
#endif

// Entry point function type
using CreateExtensionFn = Extension* (*)(const ExtensionManifest&);

// Load a shared library. When global=true, the library's symbols are made
// available for subsequently loaded plugins to resolve against. This is
// needed for provider plugins (e.g. acorn-scsi provides "scsi") whose
// symbols are used by child plugins (e.g. scsi-hard-disc).
void* platform_dlopen(const std::filesystem::path& filepath, bool global = false) {
#ifdef _WIN32
    (void)global;  // Windows DLLs always use explicit import/export
    return LoadLibraryW(filepath.c_str());
#else
    int flags = RTLD_NOW | (global ? RTLD_GLOBAL : RTLD_LOCAL);
    return dlopen(filepath.c_str(), flags);
#endif
}

void platform_dlclose(void* handle) {
    if (handle) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }
}

void* platform_dlsym(void* handle, const char* symbol) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbol));
#else
    return dlsym(handle, symbol);
#endif
}

std::string platform_dlerror() {
#ifdef _WIN32
    DWORD err = GetLastError();
    if (err == 0) return "";
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                   nullptr, err, 0, reinterpret_cast<char*>(&msg), 0, nullptr);
    std::string result = msg ? msg : "unknown error";
    LocalFree(msg);
    return result;
#else
    const char* msg = dlerror();
    return msg ? msg : "";
#endif
}

ExtensionManifest parse_manifest(const std::filesystem::path& manifest_filepath) {
    std::ifstream file(manifest_filepath);
    if (!file) {
        throw std::runtime_error(
            "Cannot open manifest: " + manifest_filepath.string());
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            "Invalid JSON in manifest " + manifest_filepath.string() + ": " + e.what());
    }

    ExtensionManifest manifest;
    manifest.name = j.value("name", "");
    manifest.description = j.value("description", "");
    manifest.library_stem = j.value("library", "");
    manifest.cli_name = j.value("cli", "");
    // extension_kind: defaults to "peripheral" when absent (existing manifests).
    manifest.extension_kind = j.value("extension_kind", "peripheral");
    manifest.manifest_dirpath = manifest_filepath.parent_path();

    if (manifest.name.empty()) {
        throw std::runtime_error(
            "Manifest missing 'name' field: " + manifest_filepath.string());
    }
    if (manifest.library_stem.empty()) {
        throw std::runtime_error(
            "Manifest missing 'library' field: " + manifest_filepath.string());
    }

    // Parse provides list (extension points this extension creates)
    if (j.contains("provides") && j["provides"].is_array()) {
        for (const auto& p : j["provides"]) {
            if (p.is_string()) {
                manifest.provides.push_back(p.get<std::string>());
            }
        }
    }

    // Parse parameter schema
    if (j.contains("parameters") && j["parameters"].is_array()) {
        for (const auto& p : j["parameters"]) {
            ParameterSchema param;
            param.key = p.value("key", "");
            param.type = p.value("type", "string");
            param.description = p.value("description", "");
            param.position = p.value("position", -1);
            param.required = p.value("required", false);
            param.default_value = p.value("default", "");
            if (!param.key.empty()) {
                manifest.parameters.push_back(std::move(param));
            }
        }
    }

    return manifest;
}

}  // namespace

PluginLoader::~PluginLoader() {
    for (auto& plugin : loaded_plugins_) {
        platform_dlclose(plugin.handle);
    }
}

std::vector<ExtensionManifest> PluginLoader::scan_manifests(
        const std::filesystem::path& extension_dirpath) {
    std::vector<ExtensionManifest> manifests;

    if (!std::filesystem::exists(extension_dirpath)) {
        return manifests;
    }

    for (const auto& entry : std::filesystem::directory_iterator(extension_dirpath)) {
        // Check for manifest.json directly in extension_dirpath
        if (entry.is_regular_file() && entry.path().filename() == "manifest.json") {
            try {
                manifests.push_back(parse_manifest(entry.path()));
            } catch (const std::exception& e) {
                std::cerr << "Warning: " << e.what() << "\n";
            }
        }
        // Check for subdirectories containing manifest.json
        if (entry.is_directory()) {
            auto manifest_filepath = entry.path() / "manifest.json";
            if (std::filesystem::exists(manifest_filepath)) {
                try {
                    manifests.push_back(parse_manifest(manifest_filepath));
                } catch (const std::exception& e) {
                    std::cerr << "Warning: " << e.what() << "\n";
                }
            }
        }
    }

    return manifests;
}

std::unique_ptr<Extension> PluginLoader::load_extension(
        const ExtensionManifest& manifest,
        std::map<std::string, std::string> config) {
    // Build library path
    std::string library_filename = manifest.library_stem + kSharedLibSuffix;
    auto library_filepath = manifest.manifest_dirpath / library_filename;

    if (!std::filesystem::exists(library_filepath)) {
        // Also try lib prefix on POSIX
        library_filepath = manifest.manifest_dirpath / ("lib" + library_filename);
        if (!std::filesystem::exists(library_filepath)) {
            throw std::runtime_error(
                "Extension library not found for '" + manifest.name + "': "
                + (manifest.manifest_dirpath / library_filename).string());
        }
    }

    // Load the shared library. Provider plugins (those with "provides" in
    // their manifest) are loaded with RTLD_GLOBAL so child plugins can
    // resolve their symbols at load time.
    bool is_provider = !manifest.provides.empty();
    void* handle = platform_dlopen(library_filepath, is_provider);
    if (!handle) {
        throw std::runtime_error(
            "Failed to load extension library '" + library_filepath.string()
            + "': " + platform_dlerror());
    }

    // Find the entry point
    auto* create_fn = reinterpret_cast<CreateExtensionFn>(
        platform_dlsym(handle, "beebium_create_extension"));
    if (!create_fn) {
        platform_dlclose(handle);
        throw std::runtime_error(
            "Extension library '" + library_filepath.string()
            + "' does not export 'beebium_create_extension'");
    }

    // Create the extension, passing the manifest
    Extension* ext = create_fn(manifest);
    if (!ext) {
        platform_dlclose(handle);
        throw std::runtime_error(
            "beebium_create_extension() returned null for '" + manifest.name + "'");
    }

    // Set config before returning (config is available during init()).
    if (!config.empty()) {
        ext->set_config(std::move(config));
    }

    // Track the loaded library for cleanup.
    loaded_plugins_.push_back({handle, manifest.name});

    // Caller takes ownership and is responsible for downcasting to the
    // appropriate extension-point base (PeripheralExtension,
    // EconetTransportExtension, etc.) per manifest.extension_kind.
    return std::unique_ptr<Extension>(ext);
}

const ExtensionManifest* PluginLoader::find_manifest(
        const std::vector<ExtensionManifest>& manifests,
        std::string_view name) {
    for (const auto& m : manifests) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

}  // namespace beebium
