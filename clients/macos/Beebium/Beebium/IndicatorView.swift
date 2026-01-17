// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

import SwiftUI
import AppKit

/// Annulus (ring) shape where the ring thickness is proportional to a value
///
/// When fillFraction is 0, displays as a thin ring (outline only).
/// When fillFraction is 1, displays as a completely filled circle.
/// The inner radius is calculated so that the ring thickness is linearly
/// proportional to fillFraction: r_inner = R * (1 - fillFraction)
struct AnnulusShape: Shape {
    /// Fill fraction from 0.0 (empty ring) to 1.0 (filled circle)
    var fillFraction: Double

    /// Inset from frame edge for outer radius
    var inset: CGFloat = 0

    var animatableData: Double {
        get { fillFraction }
        set { fillFraction = newValue }
    }

    func path(in rect: CGRect) -> Path {
        var path = Path()

        let center = CGPoint(x: rect.midX, y: rect.midY)
        let outerRadius = min(rect.width, rect.height) / 2 - inset

        // Clamp fillFraction to valid range
        let f = max(0, min(1, fillFraction))

        // Calculate inner radius for thickness-proportional fill
        // Ring thickness = R - r = R * f
        // Therefore: r = R * (1 - f)
        let innerRadius = outerRadius * (1 - f)

        // Draw outer circle clockwise
        path.addArc(
            center: center,
            radius: outerRadius,
            startAngle: .degrees(0),
            endAngle: .degrees(360),
            clockwise: false
        )

        // Draw inner circle counter-clockwise (creates hole via even-odd fill)
        if innerRadius > 0.5 {  // Only draw inner circle if radius is meaningful
            path.addArc(
                center: center,
                radius: innerRadius,
                startAngle: .degrees(0),
                endAngle: .degrees(360),
                clockwise: true
            )
        }

        return path
    }
}

/// A single indicator with annulus visualization and label
struct IndicatorView: View {
    /// Indicator value from 0 to 255
    let value: UInt32

    /// Text label to display next to the indicator
    let label: String

    /// Color of the indicator
    let color: Color

    /// BBC key name this indicator relates to (enables click interaction)
    let relatedKey: String?

    /// Callback when indicator is tapped (only if relatedKey is set)
    var onTap: (() -> Void)?

    /// Size of the indicator circle
    var size: CGFloat = 12

    /// Whether this indicator is interactive (has a related key)
    private var isInteractive: Bool {
        relatedKey != nil && onTap != nil
    }

    var body: some View {
        HStack(spacing: 4) {
            ZStack {
                // Outline ring (always visible, provides structure when value is 0)
                // Use darker, more saturated version like macOS traffic light buttons
                Circle()
                    .strokeBorder(outlineColor, lineWidth: 1)
                    .frame(width: size, height: size)

                // Filled annulus (inset to hide outer edge behind stroke border)
                AnnulusShape(fillFraction: fillFraction, inset: 0.5)
                    .fill(color)
                    .frame(width: size, height: size)
            }
            .frame(width: size, height: size)

            Text(label)
                .font(.system(size: 11))
                .foregroundColor(.secondary)
        }
        .contentShape(Rectangle())
        .onTapGesture {
            if isInteractive {
                onTap?()
            }
        }
        .help(isInteractive ? "Click to toggle \(label)" : label)
    }

    /// Convert 0-255 value to 0.0-1.0 fill fraction
    private var fillFraction: Double {
        Double(value) / 255.0
    }

    /// Outline color: darker and more saturated than fill (like macOS traffic light buttons)
    private var outlineColor: Color {
        // Convert to NSColor to access HSB components
        let nsColor = NSColor(color)
        var hue: CGFloat = 0
        var saturation: CGFloat = 0
        var brightness: CGFloat = 0
        var alpha: CGFloat = 0

        // Get HSB components (convert to calibrated RGB color space first)
        if let calibrated = nsColor.usingColorSpace(.sRGB) {
            calibrated.getHue(&hue, saturation: &saturation, brightness: &brightness, alpha: &alpha)
        } else {
            // Fallback if conversion fails
            return color.opacity(0.6)
        }

        // Adjust: ~12% less brightness, ~10% more saturation, clamped to [0, 1]
        let adjustedBrightness = min(1, max(0, brightness * 0.88))
        let adjustedSaturation = min(1, max(0, saturation * 1.10 + 0.05))

        return Color(
            hue: Double(hue),
            saturation: Double(adjustedSaturation),
            brightness: Double(adjustedBrightness)
        )
    }
}

#if DEBUG
struct IndicatorView_Previews: PreviewProvider {
    static var previews: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Show different fill levels (with related key - clickable)
            IndicatorView(value: 0, label: "CAPS LOCK", color: .red, relatedKey: "Caps Lock")
            IndicatorView(value: 64, label: "CAPS LOCK", color: .red, relatedKey: "Caps Lock")
            IndicatorView(value: 128, label: "CAPS LOCK", color: .red, relatedKey: "Caps Lock")
            IndicatorView(value: 192, label: "CAPS LOCK", color: .red, relatedKey: "Caps Lock")
            IndicatorView(value: 255, label: "CAPS LOCK", color: .red, relatedKey: "Caps Lock")

            Divider()

            // Drive indicators in orange (no related key - not clickable)
            IndicatorView(value: 0, label: "Drive 0", color: .orange, relatedKey: nil)
            IndicatorView(value: 255, label: "Drive 0", color: .orange, relatedKey: nil)
        }
        .padding()
        .background(.bar)
    }
}
#endif
