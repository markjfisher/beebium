// Copyright © 2025-2026 Robert Smallshire <robert@smallshire.org.uk>
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

import Foundation

/// Whether to render with the BBC's authentic non-square pixels (PAR 0.96)
/// or with square pixels (PAR 1.0) for cleaner integer scaling on modern
/// displays.
///
/// Affects only the horizontal dimension: a BBC pixel is roughly 0.96 as wide
/// as it is tall on the original PAL CRT. Using PAR 1.0 makes the picture a
/// few percent wider than authentic, but lets each BBC pixel land on a whole
/// number of host pixels at common drawable sizes - sharper edges, no
/// fractional-pixel sampling artefacts.
enum PixelShape: String, CaseIterable, Identifiable {
    /// PAR 0.96 - matches the BBC PAL CRT geometry.
    case authentic
    /// PAR 1.0 - square pixels for crisp scaling on modern displays.
    case crisp

    var id: String { rawValue }

    /// User-visible label shown in pickers.
    var displayName: String {
        switch self {
        case .authentic: return "Authentic"
        case .crisp: return "Crisp"
        }
    }

    /// Pixel Aspect Ratio scale passed to the vertex shader.
    var parScale: Float {
        switch self {
        case .authentic: return 0.96
        case .crisp: return 1.0
        }
    }
}
