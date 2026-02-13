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

#pragma once

#include <beebium/disc/DiscUrl.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

namespace beebium::server {

// Preset storage configuration (matches docs/plans/preset-schema/storage.md)
struct PresetStorageConfig {
    std::optional<std::string> fdc_socket_id;     // e.g., "acorn-1770", "none"
    std::optional<std::string> fdc_socket_mode;   // e.g., "1770" for Solidisk DFDC
    std::map<uint8_t, std::optional<std::string>> floppy_drives;  // drive -> image_uri (nullopt = empty)
    std::optional<std::string> cassette_image_uri;
};

// Top-level preset configuration
struct PresetConfig {
    std::string name;
    std::optional<std::string> model;             // For validation against executable
    std::optional<PresetStorageConfig> storage;
    std::optional<double> thumbnail_capture_delay_seconds;  // For capture-screenshot subcommand
    // Future: sideways_bank, startup_options, networking, coprocessor, os_rom
};

// Result of loading a preset file
struct PresetLoadResult {
    std::optional<PresetConfig> config;
    std::string error;

    bool success() const { return config.has_value(); }
    explicit operator bool() const { return success(); }
};

// Check if a path string represents a Unix-style absolute path.
// On Unix, paths starting with '/' are absolute.
// On Windows, std::filesystem considers '/path' relative (no drive letter),
// but for preset portability we treat paths starting with '/' as absolute.
inline bool is_unix_absolute_path(const std::string& path_str) {
    return !path_str.empty() && path_str[0] == '/';
}

// Normalize a path or URI to a file:// URI.
//
// If the input contains "://", it's treated as a URI and returned unchanged.
// Otherwise, it's treated as a filesystem path:
// - Absolute paths are converted to file:// URIs directly
// - Relative paths are resolved relative to base_dirpath, then converted
// - Path components like ".." are resolved to produce clean URIs
// - Unix-style absolute paths (starting with /) are treated as absolute on all platforms
//
// This allows presets to use relative paths (resolved from preset file location)
// or absolute paths, while storing everything as URIs internally.
inline std::string normalize_image_uri(const std::string& uri_or_path,
                                       const std::filesystem::path& base_dirpath) {
    // Check if it's already a URI
    if (uri_or_path.find("://") != std::string::npos) {
        return uri_or_path;
    }

    // Treat as filesystem path
    std::filesystem::path path(uri_or_path);

    // Resolve relative paths against base directory.
    // On Windows, treat Unix-style absolute paths (starting with /) as absolute
    // for preset portability, even though std::filesystem considers them relative.
    bool is_absolute = path.is_absolute() || is_unix_absolute_path(uri_or_path);
    if (!is_absolute) {
        path = base_dirpath / path;
    }

    // Normalize the path to resolve .. and . components
    // Use lexically_normal for textual normalization (no filesystem access)
    path = path.lexically_normal();

    // Convert to file:// URI
    // Use generic_string() to ensure forward slashes in the URI on all platforms
    std::string path_str = path.generic_string();
    if (path_str.empty() || path_str[0] != '/') {
        // Ensure path starts with / for proper file:// URI format
        return "file:///" + path_str;
    }
    return "file://" + path_str;
}

// Parse the storage section from JSON
inline std::optional<PresetStorageConfig> parse_storage_section(
    const nlohmann::json& storage_json,
    const std::filesystem::path& preset_dirpath) {

    PresetStorageConfig storage;

    // Parse fdc_socket
    if (storage_json.contains("fdc_socket")) {
        const auto& fdc = storage_json["fdc_socket"];
        if (fdc.is_object()) {
            if (fdc.contains("id") && fdc["id"].is_string()) {
                storage.fdc_socket_id = fdc["id"].get<std::string>();
            }
            if (fdc.contains("mode") && fdc["mode"].is_string()) {
                storage.fdc_socket_mode = fdc["mode"].get<std::string>();
            }
        }
    }

    // Parse floppy_drives array
    if (storage_json.contains("floppy_drives") && storage_json["floppy_drives"].is_array()) {
        for (const auto& drive_json : storage_json["floppy_drives"]) {
            if (!drive_json.is_object() || !drive_json.contains("drive")) {
                continue;
            }

            uint8_t drive_num = drive_json["drive"].get<uint8_t>();

            if (drive_json.contains("image_uri")) {
                if (drive_json["image_uri"].is_null()) {
                    storage.floppy_drives[drive_num] = std::nullopt;  // Explicit empty
                } else if (drive_json["image_uri"].is_string()) {
                    std::string uri = drive_json["image_uri"].get<std::string>();
                    storage.floppy_drives[drive_num] = normalize_image_uri(uri, preset_dirpath);
                }
            }
        }
    }

    // Parse cassette
    if (storage_json.contains("cassette") && storage_json["cassette"].is_object()) {
        const auto& cassette = storage_json["cassette"];
        if (cassette.contains("image_uri")) {
            if (cassette["image_uri"].is_string()) {
                storage.cassette_image_uri =
                    normalize_image_uri(cassette["image_uri"].get<std::string>(), preset_dirpath);
            }
        }
    }

    return storage;
}

// Load and parse a preset file.
//
// Returns a PresetLoadResult containing either the parsed configuration or an error message.
// Unknown keys are ignored for forward compatibility.
//
// Usage:
//   auto result = load_preset("/path/to/game.preset.beebium");
//   if (result) {
//       // Use result.config
//   } else {
//       std::cerr << "Error: " << result.error << "\n";
//   }
inline PresetLoadResult load_preset(const std::filesystem::path& filepath) {
    // Open the file
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return {std::nullopt, "Cannot open preset file: " + filepath.string()};
    }

    // Parse JSON
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        return {std::nullopt, "Invalid JSON in preset file: " + filepath.string() +
                              ": parse error at byte " + std::to_string(e.byte)};
    }

    if (!json.is_object()) {
        return {std::nullopt, "Preset file must contain a JSON object: " + filepath.string()};
    }

    // Parse into PresetConfig
    PresetConfig config;

    // Name (required)
    if (json.contains("name") && json["name"].is_string()) {
        config.name = json["name"].get<std::string>();
    } else {
        config.name = filepath.stem().string();  // Use filename as fallback
    }

    // Model (optional, for validation)
    if (json.contains("model") && json["model"].is_string()) {
        config.model = json["model"].get<std::string>();
    }

    // Storage section
    if (json.contains("storage") && json["storage"].is_object()) {
        config.storage = parse_storage_section(json["storage"], filepath.parent_path());
    }

    // Thumbnail capture delay (for capture-screenshot subcommand)
    if (json.contains("thumbnail_capture_delay_seconds") &&
        json["thumbnail_capture_delay_seconds"].is_number()) {
        config.thumbnail_capture_delay_seconds =
            json["thumbnail_capture_delay_seconds"].get<double>();
    }

    // Note: Unknown keys are silently ignored for forward compatibility
    // Future: log a warning about unknown keys

    return {std::move(config), ""};
}

} // namespace beebium::server
