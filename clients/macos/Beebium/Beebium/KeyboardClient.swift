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

import Foundation
import GRPC
import NIO

/// BBC keyboard internal key numbers for modifier keys
private enum BBCModifierKey {
    static let shift: UInt8 = 0x00  // Row 0, Column 0
    static let ctrl: UInt8 = 0x01   // Row 0, Column 1
}

/// Tracks a pressed key and its synthetic modifier state
private struct PressedKeyState {
    let ikNumber: UInt8
    let syntheticShift: Bool
    let syntheticCtrl: Bool
    let isBreak: Bool
}

/// Client for sending keyboard input to beebium-server via gRPC.
///
/// This client uses the keyboard mapping system to resolve host key inputs
/// to BBC keyboard matrix positions, including automatic handling of
/// synthetic Shift and Ctrl keys.
@MainActor
final class KeyboardClient: ObservableObject {

    private var channel: GRPCChannel?
    private var client: Beebium_KeyboardServiceClient?

    /// The keyboard mapping manager (provides cache and active mapping)
    weak var mappingManager: KeyboardMappingManager?

    /// Track pressed keys by their keyCode (for proper release matching)
    /// Maps keyCode to the state of the pressed key
    private var pressedKeys: [UInt16: PressedKeyState] = [:]

    /// Count of keys currently holding synthetic Shift
    private var syntheticShiftCount: Int = 0

    /// Count of keys currently holding synthetic Ctrl
    private var syntheticCtrlCount: Int = 0

    /// Connect to the keyboard service using an existing channel
    /// - Parameter channel: The gRPC channel (shared with VideoClient)
    func connect(channel: GRPCChannel) {
        self.channel = channel
        self.client = Beebium_KeyboardServiceClient(channel: channel)
        pressedKeys.removeAll()
        syntheticShiftCount = 0
        syntheticCtrlCount = 0
        print("[KeyboardClient] Connected to keyboard service")
    }

    /// Disconnect from the server
    func disconnect() {
        // Release all pressed keys
        for (_, state) in pressedKeys {
            Task {
                await sendKeyUp(ikNumber: state.ikNumber)
            }
        }

        // Release synthetic modifiers
        if syntheticShiftCount > 0 {
            Task {
                await sendKeyUp(ikNumber: BBCModifierKey.shift)
            }
        }
        if syntheticCtrlCount > 0 {
            Task {
                await sendKeyUp(ikNumber: BBCModifierKey.ctrl)
            }
        }

        pressedKeys.removeAll()
        syntheticShiftCount = 0
        syntheticCtrlCount = 0

        client = nil
        channel = nil
    }

    /// Fetch the keyboard mappings from the core and populate the cache
    func loadKeyMappings() async {
        guard let client = client else {
            print("[KeyboardClient] loadKeyMappings: no client!")
            return
        }

        do {
            let request = Beebium_GetAllKeyMappingsRequest()
            let response = try await client.getAllKeyMappings(request).response.get()
            mappingManager?.loadCache(from: response)
            print("[KeyboardClient] Loaded \(response.mappings.count) key mappings from core")
        } catch {
            print("[KeyboardClient] Failed to load key mappings: \(error)")
        }
    }

    /// Handle a key down event
    /// - Parameter input: The key input event
    func keyDown(input: KeyInput) {
        guard let manager = mappingManager,
              let mapping = manager.activeMapping,
              manager.isCacheLoaded else {
            print("[KeyboardClient] keyDown: mapping not ready")
            return
        }

        // Resolve the input using the active mapping
        guard let resolved = mapping.resolve(input, cache: manager.bbcKeyCache) else {
            // Key not mapped - ignore
            return
        }

        // Check if already pressed (by keyCode)
        if pressedKeys[input.keyCode] != nil {
            return
        }

        // Track this key press
        let state = PressedKeyState(
            ikNumber: resolved.ikNumber,
            syntheticShift: resolved.bbcShift,
            syntheticCtrl: resolved.bbcCtrl,
            isBreak: resolved.isBreak
        )
        pressedKeys[input.keyCode] = state

        Task {
            // Break key is handled specially - not part of keyboard matrix
            if resolved.isBreak {
                await sendBreakDown()
                return
            }

            // Press synthetic Shift if needed
            if resolved.bbcShift {
                if syntheticShiftCount == 0 {
                    await sendKeyDown(ikNumber: BBCModifierKey.shift)
                }
                syntheticShiftCount += 1
            }

            // Press synthetic Ctrl if needed
            if resolved.bbcCtrl {
                if syntheticCtrlCount == 0 {
                    await sendKeyDown(ikNumber: BBCModifierKey.ctrl)
                }
                syntheticCtrlCount += 1
            }

            // Press the actual key
            await sendKeyDown(ikNumber: resolved.ikNumber)
        }
    }

    /// Handle a key up event
    /// - Parameter input: The key input event
    func keyUp(input: KeyInput) {
        // Look up the state from when this key was pressed
        guard let state = pressedKeys.removeValue(forKey: input.keyCode) else {
            // Key wasn't tracked as pressed
            return
        }

        Task {
            // Break key is handled specially - not part of keyboard matrix
            if state.isBreak {
                await sendBreakUp()
                return
            }

            // Release the key
            await sendKeyUp(ikNumber: state.ikNumber)

            // Release synthetic Shift if this key was holding it
            if state.syntheticShift {
                syntheticShiftCount -= 1
                if syntheticShiftCount == 0 {
                    await sendKeyUp(ikNumber: BBCModifierKey.shift)
                }
            }

            // Release synthetic Ctrl if this key was holding it
            if state.syntheticCtrl {
                syntheticCtrlCount -= 1
                if syntheticCtrlCount == 0 {
                    await sendKeyUp(ikNumber: BBCModifierKey.ctrl)
                }
            }
        }
    }

    // MARK: - Private gRPC Methods

    private func sendKeyDown(ikNumber: UInt8) async {
        guard let client = client else {
            print("[KeyboardClient] sendKeyDown: no client!")
            return
        }

        var request = Beebium_KeyRequest()
        request.ikNumber = UInt32(ikNumber)

        do {
            _ = try await client.keyDown(request).response.get()
        } catch {
            print("[KeyboardClient] sendKeyDown gRPC error: \(error)")
        }
    }

    private func sendKeyUp(ikNumber: UInt8) async {
        guard let client = client else {
            print("[KeyboardClient] sendKeyUp: no client!")
            return
        }

        var request = Beebium_KeyRequest()
        request.ikNumber = UInt32(ikNumber)

        do {
            _ = try await client.keyUp(request).response.get()
        } catch {
            print("[KeyboardClient] sendKeyUp gRPC error: \(error)")
        }
    }

    private func sendBreakDown() async {
        guard let client = client else {
            print("[KeyboardClient] sendBreakDown: no client!")
            return
        }

        let request = Beebium_BreakDownRequest()

        do {
            _ = try await client.breakDown(request).response.get()
        } catch {
            print("[KeyboardClient] sendBreakDown gRPC error: \(error)")
        }
    }

    private func sendBreakUp() async {
        guard let client = client else {
            print("[KeyboardClient] sendBreakUp: no client!")
            return
        }

        let request = Beebium_BreakUpRequest()

        do {
            _ = try await client.breakUp(request).response.get()
        } catch {
            print("[KeyboardClient] sendBreakUp gRPC error: \(error)")
        }
    }
}
