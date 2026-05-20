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

#include "ScsiHardDiscUi.hpp"

#include "ScsiHardDiscExtension.hpp"
#include "ScsiConstants.hpp"

#include "extension_ui.pb.h"

#include <cstdio>
#include <cstdint>
#include <string>

namespace beebium {

namespace {

constexpr const char* CONTROL_CAPACITY = "capacity";

// Trim a trailing ".0" from a fixed-point string so whole values
// render as "4 MB" rather than "4.0 MB". Leaves "4.5" unchanged.
std::string strip_trailing_zero(std::string s) {
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) {
        s.resize(s.size() - 2);
    }
    return s;
}

std::string format_one_decimal(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", value);
    return strip_trailing_zero(std::string(buf));
}

}  // namespace

std::string ScsiHardDiscUi::format_capacity(std::uint32_t total_sectors) {
    if (total_sectors == 0) {
        return "No image";
    }
    std::uint64_t bytes =
        static_cast<std::uint64_t>(total_sectors) * scsi::ACORN_BLOCK_SIZE;
    if (bytes < 1'000'000ull) {
        return format_one_decimal(static_cast<double>(bytes) / 1000.0) + " KB";
    }
    return format_one_decimal(static_cast<double>(bytes) / 1'000'000.0) + " MB";
}

void ScsiHardDiscUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();

    auto* control = group->add_controls();
    control->set_id(CONTROL_CAPACITY);
    control->mutable_label()->set_text(
        "Capacity: " + format_capacity(ext_.total_sectors()));
}

void ScsiHardDiscUi::handle_event(const DispatchRequest& /*request*/) {
    // No interactive controls today; the framework's validation
    // gauntlet wouldn't route any event here, but the override is
    // pure-virtual on the base so we provide an empty body.
}

}  // namespace beebium
