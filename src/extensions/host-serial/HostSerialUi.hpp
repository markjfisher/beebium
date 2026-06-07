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

#ifndef BEEBIUM_EXT_HOST_SERIAL_UI_HPP
#define BEEBIUM_EXT_HOST_SERIAL_UI_HPP

#include <beebium/extension/ExtensionUi.hpp>

namespace beebium {

class HostSerialExtension;

// Read-only Peripherals-sidebar panel for the host-serial bridge: shows the
// configured mode/path/baud and whether the host port is currently open. A
// snapshot at build time -- no background ticker yet.
class HostSerialUi : public ExtensionUi {
public:
    explicit HostSerialUi(HostSerialExtension& ext) : ext_(ext) {}

    void build_view(View* out) const override;
    void handle_event(const DispatchRequest& request) override;

private:
    HostSerialExtension& ext_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_HOST_SERIAL_UI_HPP
