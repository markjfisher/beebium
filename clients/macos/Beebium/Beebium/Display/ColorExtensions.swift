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

import AppKit
import Metal
import simd
import SwiftUI

/// sRGB hex parsing/formatting and Metal clear-colour conversion for SwiftUI
/// Color. Used for the user-configurable window background, which is bound to
/// a Color in SwiftUI but persisted as a hex string in UserDefaults and
/// applied to MTKView.clearColor as MTLClearColor.
extension Color {
    /// Parse a 6 or 8 hex-digit sRGB string. The leading `#` is optional.
    /// Returns nil for malformed input.
    init?(sRGBHex: String) {
        var s = sRGBHex.trimmingCharacters(in: .whitespacesAndNewlines)
        if s.hasPrefix("#") { s.removeFirst() }
        guard s.count == 6 || s.count == 8,
              let value = UInt32(s, radix: 16) else { return nil }
        let r: Double
        let g: Double
        let b: Double
        let a: Double
        if s.count == 8 {
            r = Double((value >> 24) & 0xFF) / 255.0
            g = Double((value >> 16) & 0xFF) / 255.0
            b = Double((value >> 8) & 0xFF) / 255.0
            a = Double(value & 0xFF) / 255.0
        } else {
            r = Double((value >> 16) & 0xFF) / 255.0
            g = Double((value >> 8) & 0xFF) / 255.0
            b = Double(value & 0xFF) / 255.0
            a = 1.0
        }
        self.init(.sRGB, red: r, green: g, blue: b, opacity: a)
    }

    /// Six-digit `#RRGGBB` representation in the sRGB colour space. Alpha is
    /// dropped because the window background is always opaque. Components
    /// outside sRGB are clamped during conversion.
    var sRGBHex: String {
        let components = sRGBComponents
        let r = Int((components.red * 255).rounded())
        let g = Int((components.green * 255).rounded())
        let b = Int((components.blue * 255).rounded())
        return String(format: "#%02X%02X%02X",
                      max(0, min(255, r)),
                      max(0, min(255, g)),
                      max(0, min(255, b)))
    }

    /// Convert this Color to a Metal clear colour in the sRGB colour space.
    /// Used to drive `MTKView.clearColor`, which expects linear values per
    /// Metal convention. The drawable pixel format is `.bgra8Unorm` (not
    /// sRGB-tagged), so passing sRGB components directly matches how the
    /// renderer treats the rest of its input.
    var mtlClearColor: MTLClearColor {
        let c = sRGBComponents
        return MTLClearColor(red: Double(c.red),
                             green: Double(c.green),
                             blue: Double(c.blue),
                             alpha: Double(c.alpha))
    }

    /// Pack into a SIMD4<Float> in sRGB order for shader uniforms.
    var simd4: SIMD4<Float> {
        let c = sRGBComponents
        return SIMD4<Float>(Float(c.red), Float(c.green),
                            Float(c.blue), Float(c.alpha))
    }

    /// Decompose into sRGB components in 0...1 range. Falls back to opaque
    /// black if the underlying NSColor cannot be converted to sRGB (e.g.
    /// some pattern colours).
    private var sRGBComponents: (red: CGFloat, green: CGFloat, blue: CGFloat, alpha: CGFloat) {
        let nsColor = NSColor(self).usingColorSpace(.sRGB) ?? .black
        return (red: nsColor.redComponent,
                green: nsColor.greenComponent,
                blue: nsColor.blueComponent,
                alpha: nsColor.alphaComponent)
    }
}
