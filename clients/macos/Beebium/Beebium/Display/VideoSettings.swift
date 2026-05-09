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
import SwiftUI

/// Per-window video display settings: which display style is active and the
/// global toggles like Pixels mode.
///
/// Owned by ContentView via @StateObject. Changes propagate to the renderer
/// through MetalRenderer.setActiveStyle(_:) and MetalRenderer.parScale.
///
/// Construction strategies:
/// - `VideoSettings.loadFromUserDefaults()` reads the user's saved defaults
///   from UserDefaults and is the call ContentView uses for new windows.
/// - `VideoSettings(...)` with explicit arguments bypasses UserDefaults and
///   is the call tests use.
@MainActor
final class VideoSettings: ObservableObject {
    /// All styles available to choose from. The order here drives the order
    /// in the sidebar picker.
    let availableStyles: [any DisplayStyle]

    /// The id of the currently selected style. Changes are observed by the
    /// view that owns the renderer.
    @Published var activeStyleID: String

    /// Whether the renderer should use the BBC's authentic non-square pixels
    /// (PAR 0.96) or square pixels (PAR 1.0). Crisp gives cleaner integer
    /// scaling on modern displays at the cost of geometric authenticity.
    @Published var pixelShape: PixelShape

    /// Window background colour shown as letterbox/pillarbox fill around the
    /// active picture and (under Standard) inside the edge-margin frame.
    /// Drives MTKView.clearColor; persisted as an sRGB hex string.
    @Published var windowBackground: Color

    /// Resolves activeStyleID to a concrete style. Falls back to the first
    /// available style if the id has somehow been set to something unknown.
    var activeStyle: any DisplayStyle {
        availableStyles.first { $0.id == activeStyleID } ?? availableStyles[0]
    }

    /// PAR scale to pass to the renderer. Convenience over `pixelShape.parScale`.
    var parScale: Float { pixelShape.parScale }

    /// UserDefaults keys for the global defaults. Public so the Settings >
    /// Video pane can use the same constants for its @AppStorage bindings.
    static let defaultStyleIDKey = "video.defaultStyleID"
    static let defaultPixelShapeKey = "video.defaultPixelShape"
    static let defaultWindowBackgroundKey = "video.defaultWindowBackgroundHex"

    /// Built-in default for `windowBackground`: a near-black dark grey (#0F0F0F).
    /// Used when no preference is saved in UserDefaults; the same value is the
    /// "Reset to default" target in the Settings > Video pane.
    /// `nonisolated` so it can be referenced from default argument
    /// expressions evaluated at the call site under Swift 6 strict isolation.
    nonisolated static let defaultWindowBackground: Color = Color(.sRGB,
                                                                  red: 15.0 / 255.0,
                                                                  green: 15.0 / 255.0,
                                                                  blue: 15.0 / 255.0,
                                                                  opacity: 1.0)

    /// Construct with explicit values. Tests use this so they are not
    /// affected by the local UserDefaults state.
    /// - Parameters:
    ///   - styles: Available styles. Default: Standard then Debug. Order is
    ///             preserved in the sidebar picker.
    ///   - initialStyleID: Which style to select on first display. If unknown,
    ///                     falls back to the first style.
    ///   - initialPixelShape: PAR mode to start in.
    init(styles: [any DisplayStyle] = [StandardDisplayStyle(), DebugDisplayStyle()],
         initialStyleID: String = "standard",
         initialPixelShape: PixelShape = .authentic,
         initialWindowBackground: Color = VideoSettings.defaultWindowBackground) {
        precondition(!styles.isEmpty, "VideoSettings requires at least one display style")
        self.availableStyles = styles
        let isKnown = styles.contains { $0.id == initialStyleID }
        self.activeStyleID = isKnown ? initialStyleID : styles[0].id
        self.pixelShape = initialPixelShape
        self.windowBackground = initialWindowBackground
    }

    /// Production constructor used by ContentView - reads global defaults
    /// from UserDefaults so new windows pick up whatever the user has set in
    /// Settings > Video. Falls back to built-in defaults when the keys are
    /// missing or hold an unrecognised value.
    static func loadFromUserDefaults(_ defaults: UserDefaults = .standard) -> VideoSettings {
        let styleID = defaults.string(forKey: defaultStyleIDKey) ?? "standard"

        let shape: PixelShape
        if let raw = defaults.string(forKey: defaultPixelShapeKey),
           let parsed = PixelShape(rawValue: raw) {
            shape = parsed
        } else {
            shape = .authentic
        }

        let background: Color
        if let hex = defaults.string(forKey: defaultWindowBackgroundKey),
           let parsed = Color(sRGBHex: hex) {
            background = parsed
        } else {
            background = defaultWindowBackground
        }

        return VideoSettings(initialStyleID: styleID,
                             initialPixelShape: shape,
                             initialWindowBackground: background)
    }

    /// Set the active style by id. No-op if the id is unknown.
    func selectStyle(id: String) {
        guard availableStyles.contains(where: { $0.id == id }) else { return }
        activeStyleID = id
    }

    /// Capture the current per-window settings as a snapshot for caching.
    func makeSnapshot() -> VideoSettingsSnapshot {
        VideoSettingsSnapshot(
            activeStyleID: activeStyleID,
            pixelShapeRaw: pixelShape.rawValue,
            windowBackgroundHex: windowBackground.sRGBHex
        )
    }

    /// Apply a cached snapshot to this VideoSettings, ignoring fields whose
    /// values are unrecognised (defensive against snapshots created by a
    /// future or stale build).
    func apply(_ snapshot: VideoSettingsSnapshot) {
        selectStyle(id: snapshot.activeStyleID)
        if let shape = PixelShape(rawValue: snapshot.pixelShapeRaw) {
            pixelShape = shape
        }
        if let color = Color(sRGBHex: snapshot.windowBackgroundHex) {
            windowBackground = color
        }
    }
}
