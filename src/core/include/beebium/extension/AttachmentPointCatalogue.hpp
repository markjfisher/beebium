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

#ifndef BEEBIUM_EXTENSION_ATTACHMENT_POINT_CATALOGUE_HPP
#define BEEBIUM_EXTENSION_ATTACHMENT_POINT_CATALOGUE_HPP

#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// An attachment point is a place a peripheral extension can plug in: the RS423
// serial port, the user port, the 1MHz bus, the Tube, a SCSI bus, etc. Each
// extension declares the point(s) it attaches to via attaches_to() (and, for
// listing without loading, in its manifest). This catalogue gives every point a
// human-readable display name plus its occupancy, so a configuration UI can ask
// "which of these <point> extensions would you like?" -- a single-occupancy
// point (one connector) takes at most one extension; a multi-occupancy point (a
// bus) takes several.
//
// The point *ids* are the same strings used by attaches_to() / provides() and
// register_extension_point(). This is the single source of truth for their
// display names. It is deliberately open to extension: future points (e.g. an
// "analogue-port" for joysticks) just gain an entry here.
struct AttachmentPoint {
    std::string id;            // e.g. "serial-port" (matches attaches_to() ids)
    std::string display_name;  // e.g. "Serial Port"
    std::string description;   // one-line human-readable summary
    bool single_occupancy;     // true: one connector, at most one extension;
                               // false: a bus, several extensions may share it
};

// The known attachment points, in a stable presentation order.
inline const std::vector<AttachmentPoint>& attachment_point_catalogue() {
    static const std::vector<AttachmentPoint> kPoints = {
        {"serial-port", "Serial Port",
         "The BBC RS423 serial port (MC6850 ACIA + Serial ULA).", true},
        {"user-port", "User Port",
         "The 6522 VIA user port (8-bit parallel + handshake lines).", true},
        {"1mhz-bus", "1 MHz Bus",
         "The 1 MHz bus expansion connector (FRED/JIM paged I/O).", false},
        {"tube", "Tube",
         "The Tube interface for a second processor / coprocessor.", true},
        {"scsi", "SCSI Bus",
         "The SCSI bus exposed by a host adapter; holds several targets.", false},
    };
    return kPoints;
}

// The catalogue entry for an id, or nullptr if the id is not catalogued.
inline const AttachmentPoint* find_attachment_point(std::string_view id) {
    for (const auto& p : attachment_point_catalogue()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

// The display name for a point id, falling back to the id itself when the point
// is not (yet) catalogued, so callers always have something to show.
inline std::string_view attachment_point_display_name(std::string_view id) {
    const AttachmentPoint* p = find_attachment_point(id);
    return p ? std::string_view(p->display_name) : id;
}

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ATTACHMENT_POINT_CATALOGUE_HPP
