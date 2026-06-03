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

#include "HostSerialUi.hpp"

#include "HostSerialExtension.hpp"
#include "extension_ui.pb.h"

#include <string>

namespace beebium {

void HostSerialUi::build_view(View* out) const {
    const std::string mode = std::string(ext_.config_value("mode").value_or("pty"));
    const std::string path = std::string(ext_.config_value("path").value_or(""));
    const std::string baud = std::string(ext_.config_value("baud").value_or("19200"));

    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();

    auto* mode_ctrl = group->add_controls();
    mode_ctrl->set_id("mode");
    mode_ctrl->mutable_label()->set_text("Mode: " + mode);

    if (!path.empty()) {
        auto* path_ctrl = group->add_controls();
        path_ctrl->set_id("path");
        path_ctrl->mutable_label()->set_text("Path: " + path);
    }

    if (mode == "device") {
        auto* baud_ctrl = group->add_controls();
        baud_ctrl->set_id("baud");
        baud_ctrl->mutable_label()->set_text("Baud: " + baud);
    }

    auto* status_ctrl = group->add_controls();
    status_ctrl->set_id("status");
    status_ctrl->mutable_label()->set_text(
        ext_.bridge_open() ? "Status: connected" : "Status: not connected");
}

void HostSerialUi::handle_event(const DispatchRequest& /*request*/) {
    // Read-only: no interactive controls.
}

}  // namespace beebium
