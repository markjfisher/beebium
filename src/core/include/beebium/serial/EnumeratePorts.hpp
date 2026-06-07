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

#ifndef BEEBIUM_SERIAL_ENUMERATE_PORTS_HPP
#define BEEBIUM_SERIAL_ENUMERATE_PORTS_HPP

// Host serial-port enumeration for populating UI pickers (e.g. the
// ModalEditor inside the Piconet extension's panel).
//
// This is a best-effort, UI-oriented helper:
//   * macOS:   /dev/cu.usbmodem*, /dev/cu.usbserial* (call-out, preferred) plus
//              the /dev/tty.usbmodem*, /dev/tty.usbserial* dial-in forms
//   * Linux:   /dev/ttyUSB*, /dev/ttyACM*, plus any symlink entries
//              in /dev/serial/by-id/ (the stable-id form preferred
//              where the kernel provides it)
//   * Windows: QueryDosDeviceW-reported COM<n> names
//
// Not a source-of-truth; callers should still validate / try-open the
// path returned. A missing / unreadable enumeration target yields an
// empty result rather than an error. Entries are sorted lexicographically
// and deduplicated.

#include <string>
#include <vector>

namespace beebium::serial {

// Enumerate host serial ports. Best-effort; returns an empty vector on
// platforms or configurations where enumeration isn't possible.
std::vector<std::string> enumerate_ports();

#ifndef _WIN32
// Test seam: enumerate ports from arbitrary directories rather than the
// platform defaults. Used by unit tests with a tmpfs fixture.
//
// * dev_dir is scanned for entries matching the platform's tty-prefix
//   set (macOS: cu.usbmodem, cu.usbserial, tty.usbmodem, tty.usbserial;
//   Linux: ttyUSB, ttyACM).
//   Matching entries are returned as "<dev_dir>/<name>".
// * by_id_dir, if non-empty, is scanned for all entries (its contents
//   are preserved verbatim as "<by_id_dir>/<name>") -- this mirrors
//   the Linux /dev/serial/by-id behaviour where each symlink is a
//   stable device identifier.
std::vector<std::string> enumerate_ports_from_dirs(
    const std::string& dev_dir,
    const std::string& by_id_dir);
#endif

}  // namespace beebium::serial

#endif  // BEEBIUM_SERIAL_ENUMERATE_PORTS_HPP
