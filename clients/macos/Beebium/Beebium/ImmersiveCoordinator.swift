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

import AppKit

/// Manages the AppKit-level transformation of NSWindows into and out of Immersive Mode.
///
/// Per-window: snapshots frame, style mask, button visibility, and movability so they can
/// be restored on exit. Process-global: refcounts the presentation options
/// (autoHideMenuBar, autoHideDock) so multiple immersive windows compose correctly — the
/// flags are applied while at least one window is immersive and cleared when the last one
/// leaves.
///
/// The window's `.titled` style mask is preserved (the window's title bar is already
/// transparent and titleless via `titlebarAppearsTransparent` / `titleVisibility = .hidden`).
/// `.fullSizeContentView` is added so the SwiftUI content view extends behind the title bar
/// area, the standard buttons are hidden, and `isMovable` is set false. Going fully
/// `.borderless` would risk losing key-window status for SwiftUI-managed NSWindows.
@MainActor
final class ImmersiveCoordinator {
    static let shared = ImmersiveCoordinator()

    private init() {}

    private struct WindowState {
        let frame: NSRect
        let styleMask: NSWindow.StyleMask
        let isMovable: Bool
        let closeButtonHidden: Bool
        let miniaturizeButtonHidden: Bool
        let zoomButtonHidden: Bool
        var willCloseObserver: NSObjectProtocol?
    }

    private var states: [ObjectIdentifier: WindowState] = [:]
    private var presentationCount: Int = 0

    /// Transform `window` into Immersive Mode: hide all chrome, fill the current screen,
    /// and apply the process-global menu-bar / Dock auto-hide presentation options if
    /// this is the first immersive window. Idempotent for an already-immersive window.
    func enterImmersive(window: NSWindow) {
        let id = ObjectIdentifier(window)
        guard states[id] == nil else { return }

        var state = WindowState(
            frame: window.frame,
            styleMask: window.styleMask,
            isMovable: window.isMovable,
            closeButtonHidden: window.standardWindowButton(.closeButton)?.isHidden ?? false,
            miniaturizeButtonHidden: window.standardWindowButton(.miniaturizeButton)?.isHidden ?? false,
            zoomButtonHidden: window.standardWindowButton(.zoomButton)?.isHidden ?? false,
            willCloseObserver: nil
        )

        // Make content extend behind the (already-transparent) title bar.
        window.styleMask.insert(.fullSizeContentView)
        window.standardWindowButton(.closeButton)?.isHidden = true
        window.standardWindowButton(.miniaturizeButton)?.isHidden = true
        window.standardWindowButton(.zoomButton)?.isHidden = true
        window.isMovable = false

        if let screen = window.screen ?? NSScreen.main {
            window.setFrame(screen.frame, display: true)
        }

        // If the user closes the window while immersive, the snapshot must still be
        // discarded and the presentation count decremented; otherwise the flags leak
        // and the menu bar stays auto-hidden.
        state.willCloseObserver = NotificationCenter.default.addObserver(
            forName: NSWindow.willCloseNotification,
            object: window,
            queue: .main
        ) { [weak self] _ in
            Task { @MainActor in
                self?.handleWindowClosed(id: id)
            }
        }

        states[id] = state

        presentationCount += 1
        if presentationCount == 1 {
            NSApp.presentationOptions.formUnion([.autoHideMenuBar, .autoHideDock])
        }
    }

    /// Reverse `enterImmersive(window:)`: restore the snapshotted style mask, frame,
    /// button visibility, and movability, and decrement the presentation refcount.
    /// No-op for a window that is not currently immersive.
    func exitImmersive(window: NSWindow) {
        let id = ObjectIdentifier(window)
        guard let state = states.removeValue(forKey: id) else { return }

        if let observer = state.willCloseObserver {
            NotificationCenter.default.removeObserver(observer)
        }

        window.setFrame(state.frame, display: true)
        window.isMovable = state.isMovable
        window.standardWindowButton(.closeButton)?.isHidden = state.closeButtonHidden
        window.standardWindowButton(.miniaturizeButton)?.isHidden = state.miniaturizeButtonHidden
        window.standardWindowButton(.zoomButton)?.isHidden = state.zoomButtonHidden
        window.styleMask = state.styleMask

        decrementPresentationCount()
    }

    func isImmersive(window: NSWindow) -> Bool {
        states[ObjectIdentifier(window)] != nil
    }

    private func handleWindowClosed(id: ObjectIdentifier) {
        guard let state = states.removeValue(forKey: id) else { return }
        if let observer = state.willCloseObserver {
            NotificationCenter.default.removeObserver(observer)
        }
        decrementPresentationCount()
    }

    private func decrementPresentationCount() {
        presentationCount -= 1
        if presentationCount == 0 {
            NSApp.presentationOptions.subtract([.autoHideMenuBar, .autoHideDock])
        }
    }
}
