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

#ifndef BEEBIUM_EXTENSION_UI_PROTO_EXPORT_H
#define BEEBIUM_EXTENSION_UI_PROTO_EXPORT_H

// Symbol visibility for the extension_ui proto shared library.
//
// protoc's --cpp_out=dllexport_decl=BEEBIUM_EXT_UI_PROTO_API option
// stamps every generated message class, its default-instance, and
// the descriptor-pool getters with this macro. The beebium_ext_ui_proto_export
// INTERFACE target's /FI (MSVC) or -include (GCC/Clang) compile
// option force-includes this header ahead of every translation unit
// that parses the generated .pb.h, so the macro is always defined
// before protoc output is seen.
//
// CMake automatically defines beebium_extension_ui_proto_EXPORTS when
// building the shared library target.

#if defined(_WIN32)
  #ifdef beebium_extension_ui_proto_EXPORTS
    #define BEEBIUM_EXT_UI_PROTO_API __declspec(dllexport)
  #else
    #define BEEBIUM_EXT_UI_PROTO_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define BEEBIUM_EXT_UI_PROTO_API __attribute__((visibility("default")))
#else
  #define BEEBIUM_EXT_UI_PROTO_API
#endif

#endif  // BEEBIUM_EXTENSION_UI_PROTO_EXPORT_H
