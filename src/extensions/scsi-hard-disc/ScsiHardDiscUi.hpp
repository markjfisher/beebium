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

#ifndef BEEBIUM_SCSI_HARD_DISC_UI_HPP
#define BEEBIUM_SCSI_HARD_DISC_UI_HPP

#include <beebium/extension/ExtensionUi.hpp>

#include <cstdint>
#include <string>

namespace beebium {

class ScsiHardDiscExtension;

// Server-driven UI panel for a single SCSI hard disc. Today (first
// peripheral ExtensionUi in the project) it surfaces just the disc
// capacity as a Label. Future slices will add image filename, write-
// protect indicator, activity LED hookup, etc.
//
// Read-only: there are no interactive controls yet, so handle_event
// is a no-op. The View is recomputed on every push because the
// underlying state can in principle change (e.g. a future swap-image
// feature), even though today total_sectors is fixed at init.
class ScsiHardDiscUi : public ExtensionUi {
public:
    explicit ScsiHardDiscUi(const ScsiHardDiscExtension& ext) : ext_(ext) {}

    void build_view(View* out) const override;
    void handle_event(const DispatchRequest& request) override;

    // Format a sector count as a human-readable capacity string.
    //   0          -> "No image"     (no DAT loaded; honest, not "0 KB")
    //   < 1 MB     -> "X.X KB"       (one decimal, trailing .0 stripped)
    //   >= 1 MB    -> "X.X MB"       (one decimal, trailing .0 stripped)
    // Decimal MB to match how Acorn drives were advertised (a "20 MB"
    // drive is 20 x 10^6 bytes worth of sectors, not 20 x 2^20).
    static std::string format_capacity(std::uint32_t total_sectors);

    // Format the disc geometry as "C cyl x H head(s) x 33 spt", used
    // as the Capacity Label's tooltip. SPT is constant (Acorn
    // convention) so it's hard-coded; if the geometry is unknown
    // (cylinders == 0), callers should suppress the tooltip rather
    // than render misleading zeros.
    static std::string format_geometry(std::uint16_t cylinders,
                                       std::uint8_t heads);

private:
    const ScsiHardDiscExtension& ext_;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_HARD_DISC_UI_HPP
