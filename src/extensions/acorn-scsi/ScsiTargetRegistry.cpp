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

#include "ScsiTargetRegistry.hpp"
#include "ScsiBus.hpp"

namespace beebium {

void ScsiTargetRegistry::wire_to_bus(ScsiBus& bus) const {
    for (uint8_t id = 0; id < scsi::MAX_TARGETS; ++id) {
        bus.set_target(id, targets_[id].get());
    }
}

}  // namespace beebium
