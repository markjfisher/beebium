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

#include "RpcSerialUi.hpp"

#include "RpcSerialExtension.hpp"
#include "extension_ui.pb.h"

#include <string>

namespace beebium {

void RpcSerialUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();

    auto* role = group->add_controls();
    role->set_id("role");
    role->mutable_label()->set_text(
        "Client-driven peer (drive via the RpcSerial service)");

    auto* tx = group->add_controls();
    tx->set_id("tx_pending");
    tx->mutable_label()->set_text(
        "BBC -> client, awaiting receive: " + std::to_string(ext_.tx_pending())
        + " bytes");

    auto* rx = group->add_controls();
    rx->set_id("rx_pending");
    rx->mutable_label()->set_text(
        "client -> BBC, queued to deliver: " + std::to_string(ext_.rx_pending())
        + " bytes");
}

void RpcSerialUi::handle_event(const DispatchRequest& /*request*/) {
    // Read-only: no interactive controls.
}

}  // namespace beebium
