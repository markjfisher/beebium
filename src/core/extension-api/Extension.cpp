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

// Vtable anchor + destructor definition for Extension. Class-level
// BEEBIUM_EXT_API was dropped so consumer DLLs (notably
// beebium_extension_ui_proto, which compiles ExtensionUiServiceImpl)
// can inline the small accessors without resolving cross-DLL imports.
// The out-of-line destructor here keeps the vtable anchored in
// beebium_extension_api.

#include "beebium/extension/Extension.hpp"

#include <algorithm>

namespace beebium {

Extension::~Extension() = default;

std::string make_extension_id(
    std::string_view manifest_name,
    std::span<const std::string> existing_ids) {
    auto is_taken = [&](std::string_view candidate) {
        return std::any_of(
            existing_ids.begin(), existing_ids.end(),
            [&](const std::string& id) { return id == candidate; });
    };

    std::string candidate(manifest_name);
    if (!is_taken(candidate)) {
        return candidate;
    }
    for (std::size_t n = 1; ; ++n) {
        candidate = std::string(manifest_name) + "-" + std::to_string(n);
        if (!is_taken(candidate)) {
            return candidate;
        }
    }
}

}  // namespace beebium
