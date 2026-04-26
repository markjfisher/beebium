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

#ifndef BEEBIUM_EXTENSION_RESOLVER_HPP
#define BEEBIUM_EXTENSION_RESOLVER_HPP

#include "Export.hpp"
#include "ExtensionManifest.hpp"

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// Merges multiple ordered sources of extension manifests into a single
// view, with later sources overriding earlier ones for matching
// effective_cli_name (case-insensitive).
//
// Typical use: built-in manifests are added first (lowest priority),
// then the auto-resolved default extension directory, then each
// user-supplied --extension-dir in order. The last entry to claim a
// given CLI name wins, so a user dir can replace a stock built-in.
//
// Manifests are referenced by pointer; the caller owns them and must
// keep them alive for the lifetime of the resolver.
class BEEBIUM_EXT_API ExtensionResolver {
public:
    struct Entry {
        const ExtensionManifest* manifest;
        std::string source_label;  // diagnostic only (e.g. "built-in", "/usr/lib/beebium/extensions")
    };

    // Append a source. The label is used for diagnostics only.
    void add_source(std::string source_label,
                    std::span<const ExtensionManifest> manifests);

    // One Entry per unique cli_name, with the highest-priority manifest selected.
    // Order: each cli_name appears in the order it was first introduced.
    const std::vector<Entry>& entries() const { return entries_; }

    // Lookup map: lowercase "--<cli-name>" -> winning manifest.
    std::map<std::string, const ExtensionManifest*> cli_name_map() const;

    // Look up the winning manifest by canonical name. Returns nullptr if
    // no manifest with that name has been registered.
    const ExtensionManifest* find_by_name(std::string_view name) const;

private:
    std::vector<Entry> entries_;
    std::map<std::string, std::size_t> index_by_cli_name_;  // lowercase cli_name -> entries_ index
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_RESOLVER_HPP
