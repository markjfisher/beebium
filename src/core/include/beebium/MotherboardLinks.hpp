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

// Motherboard link (jumper) state for machine variants whose ROM/RAM slot
// mapping depends on physical link positions on the PCB. The most prominent
// example is the Model B+ S13 link, which selects which slot numbers the
// IC71 (BASIC) socket responds to.
//
// Each Hardware policy defines a nested `MotherboardLinks` type. Machines
// with no slot-affecting links use EmptyMotherboardLinks. Machines with
// slot-affecting links (currently just ModelBPlusHardware) define their own
// struct conforming to the loose contract used by configuration code:
//
//   * Default-constructible to the standard factory link positions.
//
//   * Provides:
//        std::optional<std::string> parse(std::string_view key,
//                                         std::string_view value);
//     which returns nullopt on success or a human-readable error message
//     when the assignment is rejected.

#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// Description of one motherboard link's current state, returned by
// MotherboardLinks::describe(). Used by the SidewaysService to populate
// the proto MotherboardLink list so clients can see what link state the
// server was started with.
struct MotherboardLinkInfo {
    std::string name;
    std::string value;
    std::string description;
};

// Default link state for machines whose sideways slot mapping is fixed by
// the motherboard wiring and does not vary with any link position.
struct EmptyMotherboardLinks {
    // Reject any attempt to set a link on a machine that has none.
    std::optional<std::string> parse(std::string_view key,
                                     std::string_view /*value*/) {
        std::ostringstream msg;
        msg << "Unknown motherboard link '" << key
            << "': this machine variant has no configurable slot-mapping links.";
        return msg.str();
    }

    // No links to describe.
    std::vector<MotherboardLinkInfo> describe() const { return {}; }
};

}  // namespace beebium
