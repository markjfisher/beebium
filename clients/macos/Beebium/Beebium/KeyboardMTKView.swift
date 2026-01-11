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
import MetalKit

/// Custom MTKView subclass that handles keyboard input.
///
/// MTKView doesn't accept keyboard input by default. This subclass overrides
/// the necessary methods to receive and process keyboard events, forwarding
/// them to the KeyboardClient for transmission to the emulator.
final class KeyboardMTKView: MTKView {

    /// Keyboard client for sending key events to the server
    weak var keyboardClient: KeyboardClient?

    /// Track previous modifier flags for change detection
    private var lastModifiers: NSEvent.ModifierFlags = []

    // MARK: - First Responder

    override var acceptsFirstResponder: Bool {
        return true
    }

    override func becomeFirstResponder() -> Bool {
        return true
    }

    // MARK: - Keyboard Events

    override func keyDown(with event: NSEvent) {
        // Ignore macOS key repeat - BBC MOS handles its own auto-repeat
        if event.isARepeat {
            return
        }

        let input = KeyInput(event: event, source: .physicalKeyboard)
        print("[KeyboardMTKView] keyDown: keyCode=\(event.keyCode) chars='\(input.characters)'")

        keyboardClient?.keyDown(input: input)
    }

    override func keyUp(with event: NSEvent) {
        let input = KeyInput(event: event, source: .physicalKeyboard)
        print("[KeyboardMTKView] keyUp: keyCode=\(event.keyCode) chars='\(input.characters)'")

        keyboardClient?.keyUp(input: input)
    }

    override func flagsChanged(with event: NSEvent) {
        let current = event.modifierFlags
        let diff = current.symmetricDifference(lastModifiers)

        // Caps Lock changed?
        if diff.contains(.capsLock) {
            keyboardClient?.handleCapsLockToggle()
        }

        lastModifiers = current
    }

    /// Reset modifier tracking to current state (call on window focus gain)
    func resetModifierTracking() {
        lastModifiers = NSEvent.modifierFlags
    }

    // MARK: - Focus Handling

    /// Observer for window becoming key (focus gained)
    private var windowDidBecomeKeyObserver: NSObjectProtocol?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()

        // Remove old observer if any
        if let observer = windowDidBecomeKeyObserver {
            NotificationCenter.default.removeObserver(observer)
            windowDidBecomeKeyObserver = nil
        }

        if let window = window {
            // Become first responder when added to window
            DispatchQueue.main.async { [weak self] in
                window.makeFirstResponder(self)
            }

            // Observe window becoming key to reset modifier tracking
            windowDidBecomeKeyObserver = NotificationCenter.default.addObserver(
                forName: NSWindow.didBecomeKeyNotification,
                object: window,
                queue: .main
            ) { [weak self] _ in
                self?.resetModifierTracking()
            }
        }
    }

    deinit {
        if let observer = windowDidBecomeKeyObserver {
            NotificationCenter.default.removeObserver(observer)
        }
    }
}
