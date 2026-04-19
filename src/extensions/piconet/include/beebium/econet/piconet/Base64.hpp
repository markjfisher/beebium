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

#pragma once

// RFC 4648 base64 encoder/decoder for the Piconet wire protocol.
//
// Piconet uses libb64 (piconet/board/src/lib/b64/) configured with
// chars_per_line=0, so the wire format is single-line base64 with '=' padding
// and no embedded newlines. This implementation matches that shape.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::piconet {

// Encode bytes to single-line RFC 4648 base64 with '=' padding. Empty input
// produces an empty string. Never fails.
std::string encode_base64(std::span<const std::uint8_t> bytes);

// Decode a base64 token to bytes. Returns nullopt if the input is malformed
// (invalid characters, bad length, padding inconsistencies). Empty input
// decodes to empty output, not failure.
//
// Strict: the input must be a clean base64 token. No whitespace, no embedded
// newlines, no length-prefix or other framing. Trim before passing.
std::optional<std::vector<std::uint8_t>> decode_base64(std::string_view text);

}  // namespace beebium::piconet
