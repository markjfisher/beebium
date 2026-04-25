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

// Class-level visibility for polymorphic types whose vtable+typeinfo
// must be reachable by consumers. On POSIX with hidden default, the
// vtable inherits the class-level visibility, so without an explicit
// "default" annotation a consumer in a different shared library can't
// see the vtable and gets an undefined-symbol error at link time. On
// Windows we deliberately do NOT use class-level dllimport here --
// it makes MSVC emit dllimport calls for inline-in-class methods,
// which fails to link in consumer DLLs that don't link
// beebium_extension_api (e.g. beebium_extension_ui_proto compiling
// ExtensionUiServiceImpl). The destructor's BEEBIUM_EXT_API on each
// such class is what carries the dllexport annotation that anchors
// the vtable in the exporting DLL on Windows.
#if defined(__GNUC__) || defined(__clang__)
  #define BEEBIUM_EXT_TYPE_VISIBLE __attribute__((visibility("default")))
#else
  #define BEEBIUM_EXT_TYPE_VISIBLE
#endif
