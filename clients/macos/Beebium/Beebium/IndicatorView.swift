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

/// Annulus (ring) shape where the filled area is proportional to a value
///
/// When fillFraction is 0, displays as a thin ring (outline only).
/// When fillFraction is 1, displays as a completely filled circle.
/// The inner radius is calculated so that the filled area is linearly
/// proportional to fillFraction: r_inner = R * sqrt(1 - fillFraction)
struct AnnulusShape: Shape {
    /// Fill fraction from 0.0 (empty ring) to 1.0 (filled circle)
    var fillFraction: Double

    var animatableData: Double {
        get { fillFraction }
        set { fillFraction = newValue }
    }

    func path(in rect: CGRect) -> Path {
        var path = Path()

        let center = CGPoint(x: rect.midX, y: rect.midY)
        let outerRadius = min(rect.width, rect.height) / 2

        // Clamp fillFraction to valid range
        let f = max(0, min(1, fillFraction))

        // Calculate inner radius for area-proportional fill
        // Filled area = pi * (R^2 - r^2) = pi * R^2 * f
        // Therefore: r = R * sqrt(1 - f)
        let innerRadius = outerRadius * sqrt(1 - f)

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

    /// Size of the indicator circle (traffic-light button size)
    var size: CGFloat = 10

    var body: some View {
        HStack(spacing: 4) {
            ZStack {
                // Outline ring (always visible, provides structure when value is 0)
                Circle()
                    .strokeBorder(color.opacity(0.4), lineWidth: 1)
                    .frame(width: size, height: size)

                // Filled annulus
                AnnulusShape(fillFraction: fillFraction)
                    .fill(color)
                    .frame(width: size - 2, height: size - 2)  // Inset to fit within outline
            }
            .frame(width: size, height: size)

            Text(label)
                .font(.system(size: 10))
                .foregroundColor(.secondary)
        }
    }

    /// Convert 0-255 value to 0.0-1.0 fill fraction
    private var fillFraction: Double {
        Double(value) / 255.0
    }
}

#if DEBUG
struct IndicatorView_Previews: PreviewProvider {
    static var previews: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Show different fill levels
            IndicatorView(value: 0, label: "CAPS LOCK", color: .red)
            IndicatorView(value: 64, label: "CAPS LOCK", color: .red)
            IndicatorView(value: 128, label: "CAPS LOCK", color: .red)
            IndicatorView(value: 192, label: "CAPS LOCK", color: .red)
            IndicatorView(value: 255, label: "CAPS LOCK", color: .red)

            Divider()

            // Drive indicators in orange
            IndicatorView(value: 0, label: "Drive 0", color: .orange)
            IndicatorView(value: 255, label: "Drive 0", color: .orange)
        }
        .padding()
        .background(.bar)
    }
}
#endif
