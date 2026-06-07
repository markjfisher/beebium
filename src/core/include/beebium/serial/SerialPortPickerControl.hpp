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

#ifndef BEEBIUM_SERIAL_SERIAL_PORT_PICKER_CONTROL_HPP
#define BEEBIUM_SERIAL_SERIAL_PORT_PICKER_CONTROL_HPP

// Shared Extension-UI helper for the "which host serial port?" control, used by
// any extension that wraps a host serial device (host-serial, piconet, ...).
//
// This header composes a Control from extension_ui.pb.h, so it must be included
// from a translation unit that already has that header on its include path
// (i.e. one that links beebium_extension_ui_proto). It is not part of
// beebium_core's own compilation.

#include "extension_ui.pb.h"
#include <beebium/serial/EnumeratePorts.hpp>

#include <string>
#include <utility>
#include <vector>

namespace beebium::serial {

// Populate `control` as a serial-port picker: an EditableChoice combobox over
// the host ports enumerate_ports() found, or a plain TextInput when there are
// none -- an empty combobox reads oddly, and the device you want then (e.g. a
// socat pty) is one you type. Both control types dispatch their value as
// string_value, so the caller's handle_event reads field.string_value() either
// way, regardless of which branch built the control.
inline void build_port_picker_control(beebium::Control* control,
                                      const std::string& id,
                                      const std::string& label,
                                      const std::string& current_value,
                                      const std::string& placeholder) {
    control->set_id(id);
    const std::string effective_placeholder =
        current_value.empty() ? placeholder : current_value;

    std::vector<std::string> ports = enumerate_ports();
    if (ports.empty()) {
        auto* text_input = control->mutable_text_input();
        text_input->set_label(label);
        text_input->set_value(current_value);
        text_input->set_placeholder(effective_placeholder);
    } else {
        auto* editable_choice = control->mutable_editable_choice();
        editable_choice->set_label(label);
        editable_choice->set_value(current_value);
        editable_choice->set_placeholder(effective_placeholder);
        for (auto& port : ports) {
            *editable_choice->add_options() = std::move(port);
        }
    }
}

}  // namespace beebium::serial

#endif  // BEEBIUM_SERIAL_SERIAL_PORT_PICKER_CONTROL_HPP
