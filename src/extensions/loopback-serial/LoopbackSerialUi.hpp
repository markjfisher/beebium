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

#ifndef BEEBIUM_EXT_LOOPBACK_SERIAL_UI_HPP
#define BEEBIUM_EXT_LOOPBACK_SERIAL_UI_HPP

#include <beebium/extension/ExtensionUi.hpp>

namespace beebium {

// Minimal read-only Peripherals-sidebar panel for the loopback-serial
// extension. The loopback plug has no configuration or live state, so this is a
// single static status line -- enough to confirm the extension is present.
class LoopbackSerialUi : public ExtensionUi {
public:
    void build_view(View* out) const override;
    void handle_event(const DispatchRequest& request) override;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_LOOPBACK_SERIAL_UI_HPP
