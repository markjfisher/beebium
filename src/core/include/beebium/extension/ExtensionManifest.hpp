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

#ifndef BEEBIUM_EXTENSION_MANIFEST_HPP
#define BEEBIUM_EXTENSION_MANIFEST_HPP

#include <filesystem>
#include <string>

namespace beebium {

// Metadata describing a peripheral extension.
// For dynamically loaded extensions, this is read from manifest.json.
// For built-in extensions, this is constructed programmatically.
// The manifest is the single source of truth for extension metadata.
struct ExtensionManifest {
    std::string name;             // e.g. "test-scratch-ram"
    std::string description;      // human-readable description
    std::string library_stem;     // shared library filename stem (platform adds suffix)
    std::filesystem::path manifest_dirpath;  // directory containing manifest.json (empty for built-in)
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_MANIFEST_HPP
