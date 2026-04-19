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

// Vtable anchor for ExtensionUi. The class is declared with BEEBIUM_EXT_API
// (dllimport on Windows when consumed outside beebium_extension_api), so MSVC
// needs the vtable to live in the exporting DLL: at least one virtual
// function must have a non-inline definition here. The out-of-line
// destructor provides that anchor.

#include "beebium/extension/ExtensionUi.hpp"

namespace beebium {

ExtensionUi::~ExtensionUi() = default;

}  // namespace beebium
