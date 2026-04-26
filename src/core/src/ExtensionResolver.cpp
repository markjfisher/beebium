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

#include "beebium/extension/ExtensionResolver.hpp"

#include <algorithm>
#include <cctype>

namespace beebium {

namespace {

std::string to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

void ExtensionResolver::add_source(std::string source_label,
                                    std::span<const ExtensionManifest> manifests) {
    for (const auto& m : manifests) {
        std::string key = to_lower(m.effective_cli_name());
        auto it = index_by_cli_name_.find(key);
        if (it == index_by_cli_name_.end()) {
            index_by_cli_name_[key] = entries_.size();
            entries_.push_back({&m, source_label});
        } else {
            // Override: newer source wins.
            entries_[it->second] = {&m, source_label};
        }
    }
}

std::map<std::string, const ExtensionManifest*> ExtensionResolver::cli_name_map() const {
    std::map<std::string, const ExtensionManifest*> result;
    for (const auto& e : entries_) {
        result["--" + to_lower(e.manifest->effective_cli_name())] = e.manifest;
    }
    return result;
}

const ExtensionManifest* ExtensionResolver::find_by_name(std::string_view name) const {
    for (const auto& e : entries_) {
        if (e.manifest->name == name) {
            return e.manifest;
        }
    }
    return nullptr;
}

}  // namespace beebium
