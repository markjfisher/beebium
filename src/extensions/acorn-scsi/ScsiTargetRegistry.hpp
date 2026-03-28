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

#ifndef BEEBIUM_SCSI_TARGET_REGISTRY_HPP
#define BEEBIUM_SCSI_TARGET_REGISTRY_HPP

#include "ScsiConstants.hpp"
#include "ScsiTarget.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace beebium {

class ScsiBus;

// Manages SCSI target slots (IDs 0-7). Owns target lifetime
// and provides enumeration for gRPC discovery.
class ScsiTargetRegistry {
public:
    struct TargetInfo {
        uint8_t id;
        bool present;
        std::string_view device_type;
        std::string_view description;
    };

    // Install a target at the given SCSI ID. Takes ownership.
    void install(uint8_t id, std::unique_ptr<ScsiTarget> target) {
        if (id < scsi::MAX_TARGETS) {
            targets_[id] = std::move(target);
        }
    }

    // Remove and return a target.
    std::unique_ptr<ScsiTarget> remove(uint8_t id) {
        if (id < scsi::MAX_TARGETS) {
            return std::move(targets_[id]);
        }
        return nullptr;
    }

    // Query a target (non-owning).
    ScsiTarget* target(uint8_t id) const {
        if (id < scsi::MAX_TARGETS) {
            return targets_[id].get();
        }
        return nullptr;
    }

    bool has_target(uint8_t id) const {
        return id < scsi::MAX_TARGETS && targets_[id] != nullptr;
    }

    // Enumerate all targets for gRPC discovery.
    std::vector<TargetInfo> enumerate() const {
        std::vector<TargetInfo> infos;
        for (uint8_t id = 0; id < scsi::MAX_TARGETS; ++id) {
            if (targets_[id]) {
                infos.push_back({
                    id,
                    targets_[id]->is_present(),
                    targets_[id]->device_type(),
                    targets_[id]->description()
                });
            }
        }
        return infos;
    }

    // Connect all installed targets to a ScsiBus instance.
    void wire_to_bus(ScsiBus& bus) const;

private:
    std::array<std::unique_ptr<ScsiTarget>, scsi::MAX_TARGETS> targets_;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_TARGET_REGISTRY_HPP
