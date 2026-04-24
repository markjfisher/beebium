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

#ifdef _WIN32

#include "beebium/serial/EnumeratePorts.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::serial {

namespace {

// True iff the device-object name looks like a standard COM port:
// "COM" (case-insensitive) followed by one or more decimal digits.
// QueryDosDeviceW returns many device names (A:, MAILSLOT\..., etc.),
// so we filter tightly rather than accepting anything that starts with
// "COM".
bool is_com_name(std::wstring_view name) noexcept {
    if (name.size() < 4) return false;
    if (std::towupper(name[0]) != L'C' ||
        std::towupper(name[1]) != L'O' ||
        std::towupper(name[2]) != L'M') {
        return false;
    }
    for (std::size_t i = 3; i < name.size(); ++i) {
        if (name[i] < L'0' || name[i] > L'9') return false;
    }
    return true;
}

std::string wide_to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(
        CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
        nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
        out.data(), n, nullptr, nullptr);
    return out;
}

}  // namespace

std::vector<std::string> enumerate_ports() {
    std::vector<std::string> ports;

    // QueryDosDeviceW(nullptr, buffer, size) reports every device-
    // object name in the global namespace as a double-null-terminated
    // list. Grow the buffer until the call fits; on unrelated errors
    // return whatever we have gathered (empty list from a failed
    // initial call).
    std::vector<wchar_t> buf(1u << 14);
    while (true) {
        DWORD got = ::QueryDosDeviceW(
            nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (got != 0) {
            std::size_t i = 0;
            while (i < buf.size() && buf[i] != L'\0') {
                std::wstring_view name(buf.data() + i);
                if (is_com_name(name)) {
                    ports.push_back(wide_to_utf8(name));
                }
                i += name.size() + 1;
            }
            break;
        }
        if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            buf.resize(buf.size() * 2);
            continue;
        }
        break;
    }

    // Lexicographic sort keeps the list stable; note this places COM10
    // between COM1 and COM2 which is mildly counter-intuitive but
    // consistent with the POSIX implementation's sort semantics.
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

}  // namespace beebium::serial

#endif  // _WIN32
