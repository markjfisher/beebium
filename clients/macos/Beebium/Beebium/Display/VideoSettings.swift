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

/// Per-window video display settings: which display style is active and (in
/// later commits) the global toggles like Pixels mode and window background.
///
/// Owned by ContentView via @StateObject. Changes propagate to the renderer
/// through MetalRenderer.setActiveStyle(_:).
@MainActor
final class VideoSettings: ObservableObject {
    /// All styles available to choose from. The order here drives the order
    /// in the sidebar picker.
    let availableStyles: [any DisplayStyle]

    /// The id of the currently selected style. Changes are observed by the
    /// view that owns the renderer.
    @Published var activeStyleID: String

    /// Resolves activeStyleID to a concrete style. Falls back to the first
    /// available style if the id has somehow been set to something unknown.
    var activeStyle: any DisplayStyle {
        availableStyles.first { $0.id == activeStyleID } ?? availableStyles[0]
    }

    /// - Parameters:
    ///   - styles: Available styles. Default: Standard then Debug. Order is
    ///             preserved in the sidebar picker.
    ///   - initialStyleID: Which style to select on first display. If unknown,
    ///                     falls back to the first style.
    init(styles: [any DisplayStyle] = [StandardDisplayStyle(), DebugDisplayStyle()],
         initialStyleID: String = "standard") {
        precondition(!styles.isEmpty, "VideoSettings requires at least one display style")
        self.availableStyles = styles
        let isKnown = styles.contains { $0.id == initialStyleID }
        self.activeStyleID = isKnown ? initialStyleID : styles[0].id
    }

    /// Set the active style by id. No-op if the id is unknown.
    func selectStyle(id: String) {
        guard availableStyles.contains(where: { $0.id == id }) else { return }
        activeStyleID = id
    }
}
