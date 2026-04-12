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

// Symbol visibility for the extension API shared library.
//
// Classes and functions in the extension API are marked with BEEBIUM_EXT_API
// to ensure they are exported from the shared library and available to plugins.
// All other symbols in the library default to hidden visibility.
//
// CMake automatically defines beebium_extension_api_EXPORTS when building
// the shared library target.

#if defined(_WIN32)
  #ifdef beebium_extension_api_EXPORTS
    #define BEEBIUM_EXT_API __declspec(dllexport)
  #else
    #define BEEBIUM_EXT_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define BEEBIUM_EXT_API __attribute__((visibility("default")))
#else
  #define BEEBIUM_EXT_API
#endif
