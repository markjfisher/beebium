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

#ifndef _WIN32

#include "beebium/serial/EnumeratePorts.hpp"

#include <algorithm>
#include <dirent.h>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::serial {

namespace {

bool starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), s.begin());
}

// Tests name against the platform's tty-prefix set. macOS and Linux use
// different conventions; the caller decides which set to apply. Kept as
// a single overload rather than one-per-platform because both sets are
// tiny (two entries) and inlining the test avoids a per-file split.
bool name_is_serial_tty(std::string_view name) noexcept {
#ifdef __APPLE__
    // The call-out (cu.*) nodes are the ones you open for a serial bridge; the
    // dial-in (tty.*) forms are listed too for completeness. A virtual port made
    // with `socat pty,link=/dev/cu.usbserial-<name>` therefore also shows up.
    return starts_with(name, "cu.usbmodem") ||
           starts_with(name, "cu.usbserial") ||
           starts_with(name, "tty.usbmodem") ||
           starts_with(name, "tty.usbserial");
#else
    return starts_with(name, "ttyUSB") ||
           starts_with(name, "ttyACM");
#endif
}

}  // namespace

std::vector<std::string> enumerate_ports_from_dirs(
    const std::string& dev_dir,
    const std::string& by_id_dir)
{
    std::vector<std::string> ports;

    // Scan dev_dir for prefix-matched tty devices. opendir failure is
    // silently tolerated (the directory may not exist in test fixtures
    // or on stripped-down systems); callers get an empty result rather
    // than an error.
    if (DIR* d = ::opendir(dev_dir.c_str())) {
        while (struct dirent* entry = ::readdir(d)) {
            std::string_view name(entry->d_name);
            if (name_is_serial_tty(name)) {
                ports.push_back(dev_dir + "/" + std::string(name));
            }
        }
        ::closedir(d);
    }

    // On Linux the kernel exposes stable symlink names under
    // /dev/serial/by-id/usb-<vendor>_<product>-... which persist across
    // reboots and USB renumbering. When present, surface them verbatim
    // so the user can pick a stable identifier and not worry about
    // ttyACM<N> renumbering after a reconnect. Skipped on macOS
    // (by_id_dir passed empty by the default enumerate_ports).
    if (!by_id_dir.empty()) {
        if (DIR* d = ::opendir(by_id_dir.c_str())) {
            while (struct dirent* entry = ::readdir(d)) {
                std::string_view name(entry->d_name);
                if (name == "." || name == "..") continue;
                ports.push_back(by_id_dir + "/" + std::string(name));
            }
            ::closedir(d);
        }
    }

    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

std::vector<std::string> enumerate_ports() {
#ifdef __APPLE__
    // macOS has no kernel-level stable-id scheme comparable to Linux's
    // /dev/serial/by-id. Device nodes are assigned in USB enumeration
    // order, which can change across reconnects (this is exactly the
    // renumbering problem the ModalEditor-based device-path editor
    // exists to let the user work around).
    return enumerate_ports_from_dirs("/dev", "");
#else
    // Linux: the by-id directory is only present if the kernel has the
    // usb-serial scheme built in; if absent enumerate_ports_from_dirs
    // silently skips it.
    return enumerate_ports_from_dirs("/dev", "/dev/serial/by-id");
#endif
}

}  // namespace beebium::serial

#endif  // !_WIN32
