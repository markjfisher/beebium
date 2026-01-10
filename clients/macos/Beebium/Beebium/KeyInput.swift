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

/// Source of a keyboard input event
enum KeyInputSource {
    /// Physical keyboard (NSEvent from keyDown/keyUp)
    case physicalKeyboard

    /// Touch Bar button press
    case touchBar

    /// On-screen keyboard UI
    case onScreenKeyboard

    /// Programmatic input (e.g., TypeQuickly, scripts)
    case programmatic
}

/// A unified representation of a key input from any source.
///
/// This struct captures both the physical key (keyCode) and the character(s)
/// it produces, allowing keyboard mappings to use either for resolution.
struct KeyInput {
    /// The macOS virtual key code (from NSEvent.keyCode)
    let keyCode: UInt16

    /// The character(s) produced by this key event
    let characters: String

    /// The character(s) ignoring modifiers (for physical mappings)
    let charactersIgnoringModifiers: String

    /// Modifier flags active during this event
    let modifiers: NSEvent.ModifierFlags

    /// The source of this input
    let source: KeyInputSource

    /// The first character produced, if any (for logical mapping)
    var primaryCharacter: Character? {
        characters.first
    }

    /// Create a KeyInput from an NSEvent
    /// - Parameters:
    ///   - event: The keyboard event
    ///   - source: The input source (defaults to physical keyboard)
    init(event: NSEvent, source: KeyInputSource = .physicalKeyboard) {
        self.keyCode = event.keyCode
        self.characters = event.characters ?? ""
        self.charactersIgnoringModifiers = event.charactersIgnoringModifiers ?? ""
        self.modifiers = event.modifierFlags
        self.source = source
    }

    /// Create a KeyInput for a named key (Touch Bar, on-screen keyboard)
    /// - Parameters:
    ///   - keyCode: The virtual key code
    ///   - source: The input source
    init(keyCode: UInt16, source: KeyInputSource) {
        self.keyCode = keyCode
        self.characters = ""
        self.charactersIgnoringModifiers = ""
        self.modifiers = []
        self.source = source
    }

    /// Create a KeyInput for programmatic character input
    /// - Parameter character: The character to input
    init(character: Character) {
        self.keyCode = 0
        self.characters = String(character)
        self.charactersIgnoringModifiers = String(character)
        self.modifiers = []
        self.source = .programmatic
    }
}
