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

#ifndef BEEBIUM_EXTENSIONS_PICONET_PICONET_UI_HPP
#define BEEBIUM_EXTENSIONS_PICONET_PICONET_UI_HPP

// Server-driven UI for the Piconet extension. Pilot for the Extension UI
// framework (see docs/discussion/extension-ui-architecture.md). Three
// controls:
//   * Label   "Device: <device_path>"
//   * Indicator  OK / ERROR  driven by serial_->is_open()
//   * Toggle  "Enabled"  -> SET_MODE LISTEN / STOP

#include "beebium/extension/ExtensionUi.hpp"

namespace beebium {

class PiconetEconetTransportExtension;  // forward; defined in this dir

class PiconetUi : public ExtensionUi {
public:
    explicit PiconetUi(PiconetEconetTransportExtension& ext) : ext_(ext) {}

    void build_view(View* out) const override;
    void handle_event(const DispatchRequest& request) override;

private:
    PiconetEconetTransportExtension& ext_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_PICONET_PICONET_UI_HPP
