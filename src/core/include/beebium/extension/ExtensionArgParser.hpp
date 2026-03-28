// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXTENSION_ARG_PARSER_HPP
#define BEEBIUM_EXTENSION_ARG_PARSER_HPP

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// Schema for one extension parameter.
struct ParameterSchema {
    std::string key;
    std::string type;           // "string", "integer", "boolean", "filepath"
    std::string description;
    int position = -1;          // -1 = keyword-only; 0, 1, 2, ... = positional order
    bool required = false;
    std::string default_value;  // empty = no default
};

// Result of parsing extension arguments.
struct ParseResult {
    bool ok = false;
    std::map<std::string, std::string> config;
    std::string error;          // populated when !ok
};

// Parse a colon-separated argument string against a parameter schema.
//
// Tokens are split on ':' (but not '://' inside URIs). Each token is either:
//   - A keyword argument: "key=value"
//   - A positional argument: assigned by position order in the schema
//
// Positional arguments may also be written in keyword form (key=value)
// but must appear in the correct positional slot.
//
// After parsing, defaults are applied for missing optional parameters,
// required parameters are validated, and type checking is performed.
//
// The cli_name parameter is used in error messages (e.g. "--scsi-hdd").
//
// Framework-managed parameters (id, label) are NOT part of the schema
// and are handled separately by the caller.
ParseResult parse_extension_args(
    std::string_view cli_name,
    std::string_view arg_string,
    const std::vector<ParameterSchema>& schema);

// Split a colon-separated string, respecting '://' in URIs.
std::vector<std::string> split_colon_args(std::string_view input);

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ARG_PARSER_HPP
