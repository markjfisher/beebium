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

#include "LoopbackSerialUi.hpp"

#include "extension_ui.pb.h"

namespace beebium {

void LoopbackSerialUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();

    auto* status = group->add_controls();
    status->set_id("status");
    status->mutable_label()->set_text(
        "Loopback active: bytes the BBC transmits echo back to its receiver");
}

void LoopbackSerialUi::handle_event(const DispatchRequest& /*request*/) {
    // Read-only: no interactive controls.
}

}  // namespace beebium
