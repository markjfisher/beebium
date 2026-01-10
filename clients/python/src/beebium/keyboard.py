# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

"""Keyboard input interface for the beebium client."""

from __future__ import annotations

import time
from dataclasses import dataclass

from beebium._proto import keyboard_pb2, keyboard_pb2_grpc
from beebium.keyboard_map import (
    CTRL_KEY,
    DELETE_KEY,
    ESCAPE_KEY,
    RETURN_KEY,
    SHIFT_KEY,
    SPACE_KEY,
    char_to_matrix,
)


@dataclass
class KeyboardState:
    """Current state of the keyboard matrix."""

    pressed_rows: list[int]  # 10 rows of pressed key bitmaps

    def is_pressed(self, row: int, column: int) -> bool:
        """Check if a specific key is pressed.

        Args:
            row: The keyboard matrix row (0-9).
            column: The keyboard matrix column (0-9).

        Returns:
            True if the key is pressed.
        """
        if 0 <= row < len(self.pressed_rows):
            return bool(self.pressed_rows[row] & (1 << column))
        return False


class Keyboard:
    """Keyboard input interface.

    Provides both low-level matrix access and high-level text input.

    Usage:
        # Type text
        bbc.keyboard.type("PRINT 42")
        bbc.keyboard.press_return()

        # Press specific keys
        bbc.keyboard.key_down('A')
        bbc.keyboard.key_up('A')

        # Matrix-level access
        bbc.keyboard.matrix_down(row=4, column=1)  # 'A' key
    """

    def __init__(self, stub: keyboard_pb2_grpc.KeyboardServiceStub):
        """Create a keyboard interface.

        Args:
            stub: The gRPC stub for the KeyboardService.
        """
        self._stub = stub
        self._pressed_keys: set[tuple[str, bool]] = set()

    # High-level text input

    def type(
        self,
        text: str,
        inter_key_delay: float = 0.05,
        release_delay: float = 0.02,
    ) -> None:
        """Type a string of text.

        Each character is pressed and released with configurable delays.
        Handles shift automatically for uppercase and symbols.

        Args:
            text: The text to type.
            inter_key_delay: Delay between key presses (seconds).
            release_delay: Delay between press and release (seconds).
        """
        for char in text:
            if self.key_down(char):
                time.sleep(release_delay)
                self.key_up(char)
                time.sleep(inter_key_delay)

    def press_return(self, hold_time: float = 0.02) -> None:
        """Press and release the RETURN key.

        Args:
            hold_time: How long to hold the key (seconds).
        """
        self.matrix_down(*RETURN_KEY)
        time.sleep(hold_time)
        self.matrix_up(*RETURN_KEY)

    def press_escape(self, hold_time: float = 0.02) -> None:
        """Press and release the ESCAPE key.

        Args:
            hold_time: How long to hold the key (seconds).
        """
        self.matrix_down(*ESCAPE_KEY)
        time.sleep(hold_time)
        self.matrix_up(*ESCAPE_KEY)

    def press_delete(self, hold_time: float = 0.02) -> None:
        """Press and release the DELETE key.

        Args:
            hold_time: How long to hold the key (seconds).
        """
        self.matrix_down(*DELETE_KEY)
        time.sleep(hold_time)
        self.matrix_up(*DELETE_KEY)

    def press_space(self, hold_time: float = 0.02) -> None:
        """Press and release the SPACE key.

        Args:
            hold_time: How long to hold the key (seconds).
        """
        self.matrix_down(*SPACE_KEY)
        time.sleep(hold_time)
        self.matrix_up(*SPACE_KEY)

    # Character-level input

    def key_down(self, char: str) -> bool:
        """Press a key for the given character.

        Automatically handles SHIFT for uppercase and shifted symbols.

        Args:
            char: The character to press.

        Returns:
            True if the character is mapped, False otherwise.
        """
        mapping = char_to_matrix(char)
        if mapping is None:
            return False

        row, column, needs_shift = mapping

        if needs_shift:
            self.matrix_down(*SHIFT_KEY)
        self.matrix_down(row, column)

        self._pressed_keys.add((char, needs_shift))
        return True

    def key_up(self, char: str) -> bool:
        """Release a key for the given character.

        Args:
            char: The character to release.

        Returns:
            True if the character is mapped, False otherwise.
        """
        mapping = char_to_matrix(char)
        if mapping is None:
            return False

        row, column, needs_shift = mapping

        self.matrix_up(row, column)

        # Only release shift if no other shifted keys are pressed
        if needs_shift:
            self._pressed_keys.discard((char, True))
            if not any(shifted for _, shifted in self._pressed_keys):
                self.matrix_up(*SHIFT_KEY)
        else:
            self._pressed_keys.discard((char, False))

        return True

    def release_all(self) -> None:
        """Release all currently pressed keys."""
        for char, _ in list(self._pressed_keys):
            self.key_up(char)

    # Modifier keys

    def shift_down(self) -> None:
        """Press the SHIFT key."""
        self.matrix_down(*SHIFT_KEY)

    def shift_up(self) -> None:
        """Release the SHIFT key."""
        self.matrix_up(*SHIFT_KEY)

    def ctrl_down(self) -> None:
        """Press the CTRL key."""
        self.matrix_down(*CTRL_KEY)

    def ctrl_up(self) -> None:
        """Release the CTRL key."""
        self.matrix_up(*CTRL_KEY)

    # Matrix-level input

    def matrix_down(self, row: int, column: int) -> bool:
        """Press a key by BBC keyboard matrix position.

        Args:
            row: The keyboard matrix row (0-9).
            column: The keyboard matrix column (0-9).

        Returns:
            True if accepted by server.
        """
        request = keyboard_pb2.KeyRequest(ik_number=(row << 4) | column)
        response = self._stub.KeyDown(request)
        return response.accepted

    def matrix_up(self, row: int, column: int) -> bool:
        """Release a key by BBC keyboard matrix position.

        Args:
            row: The keyboard matrix row (0-9).
            column: The keyboard matrix column (0-9).

        Returns:
            True if accepted by server.
        """
        request = keyboard_pb2.KeyRequest(ik_number=(row << 4) | column)
        response = self._stub.KeyUp(request)
        return response.accepted

    def get_state(self) -> KeyboardState:
        """Get current keyboard state (pressed keys bitmap).

        Returns:
            The current keyboard state.
        """
        request = keyboard_pb2.GetStateRequest()
        response = self._stub.GetState(request)
        return KeyboardState(pressed_rows=list(response.pressed_rows))

    # Type-ahead (TypeQuickly)

    def type_quickly(self, text: str, cycles_per_key: int = 0) -> int:
        """Enqueue text for typing at machine speed (non-blocking).

        The text is added to a queue and typed character-by-character
        as the emulator runs. Returns immediately.

        Args:
            text: The text to type.
            cycles_per_key: CPU cycles per keystroke (0 = use default 100000).

        Returns:
            Total pending characters in queue after enqueue.

        Raises:
            ValueError: If text contains unmappable characters.
        """
        request = keyboard_pb2.TypeQuicklyRequest(
            text=text,
            cycles_per_key=cycles_per_key,
        )
        response = self._stub.TypeQuickly(request)
        if not response.accepted:
            raise ValueError(f"Text rejected: {response.error}")
        return response.pending_characters

    def typing_status(self) -> tuple[bool, int, int]:
        """Get type-ahead queue status.

        Returns:
            Tuple of (idle, pending_characters, strings_queued).
        """
        request = keyboard_pb2.GetTypingStatusRequest()
        response = self._stub.GetTypingStatus(request)
        return (response.idle, response.pending_characters, response.strings_queued)

    def clear_typing(self) -> int:
        """Clear the type-ahead queue.

        Returns:
            Number of characters that were cleared.
        """
        request = keyboard_pb2.ClearTypingRequest()
        response = self._stub.ClearTyping(request)
        return response.characters_cleared

    def wait_for_typing(self, poll_interval: float = 0.01) -> None:
        """Block until type-ahead queue is empty.

        Args:
            poll_interval: Seconds between status checks.
        """
        while True:
            idle, _, _ = self.typing_status()
            if idle:
                return
            time.sleep(poll_interval)

    # Character-to-key mapping (server-side canonical mapping)

    def get_key_mapping(self, char: str) -> tuple[int, bool, str] | None:
        """Get the BBC key mapping for a character from the server.

        This queries the canonical mapping table maintained by the server.

        Args:
            char: A single character.

        Returns:
            Tuple of (ik_number, needs_shift, name), or None if unmappable.
        """
        request = keyboard_pb2.GetKeyMappingRequest(character=char)
        entry = self._stub.GetKeyMapping(request)
        if not entry.found:
            return None
        return (entry.ik_number, entry.needs_shift, entry.name)

    def get_all_mappings(self) -> list[tuple[str, int, bool, str]]:
        """Get the complete key mapping table from the server.

        Returns:
            List of (character, ik_number, needs_shift, name) tuples.
            Character is empty string for named-only keys (function keys, etc.).
        """
        request = keyboard_pb2.GetAllKeyMappingsRequest()
        response = self._stub.GetAllKeyMappings(request)
        return [
            (entry.character, entry.ik_number, entry.needs_shift, entry.name)
            for entry in response.mappings
        ]

    def is_typeable_on_server(self, text: str) -> bool:
        """Check if all characters in text can be typed (using server mapping).

        Args:
            text: The text to check.

        Returns:
            True if all characters are typeable according to server.
        """
        return all(self.get_key_mapping(c) is not None for c in text)

    # =========================================================================
    # Break key operations
    # =========================================================================
    # The Break key is NOT part of the keyboard matrix. It is directly
    # connected to the reset circuit (IC16 NE555 timer) via pin 4.
    #
    # While Break is held: CPU is halted (reset line held low)
    # On Break release: Soft reset sequence begins
    #
    # Note: Hardware always does a soft reset. MOS checks if Ctrl is held
    # during its reset sequence to decide between warm/cold reset behavior.

    def break_down(self) -> bool:
        """Hold the Break key (halt the CPU).

        This asserts the reset line, halting the CPU. The CPU remains
        halted until break_up() is called.

        Returns:
            True if successful.
        """
        request = keyboard_pb2.BreakDownRequest()
        response = self._stub.BreakDown(request)
        return response.success

    def break_up(self) -> bool:
        """Release the Break key (begin soft reset sequence).

        This releases the reset line. The CPU will execute the 7-cycle
        reset sequence and then begin execution from the reset vector.

        MOS checks if Ctrl is held at this moment to determine reset type:
        - If Ctrl pressed: MOS performs "hard reset" (clears VIA config)
        - If Ctrl not pressed: MOS performs "warm reset" (preserves state)

        Returns:
            True if successful.
        """
        request = keyboard_pb2.BreakUpRequest()
        response = self._stub.BreakUp(request)
        return response.success

    def is_break_held(self) -> bool:
        """Check if the Break key is currently held.

        Returns:
            True if Break is currently held (CPU halted).
        """
        request = keyboard_pb2.GetBreakStateRequest()
        response = self._stub.GetBreakState(request)
        return response.is_held

    def press_break(self, hold_time: float = 0.02) -> bool:
        """Press and release the Break key (perform soft reset).

        This is a convenience method that calls break_down(), waits
        briefly, then calls break_up().

        Args:
            hold_time: How long to hold Break (seconds).

        Returns:
            True if both operations succeeded.
        """
        down_ok = self.break_down()
        time.sleep(hold_time)
        up_ok = self.break_up()
        return down_ok and up_ok

    def ctrl_break(self, hold_time: float = 0.02) -> bool:
        """Press Ctrl-Break (trigger MOS hard reset).

        This holds Ctrl while pressing Break, which causes MOS to
        detect a "hard reset" and perform full reinitialization.

        Note: The hardware always performs a soft reset. MOS checks
        the keyboard matrix during its reset routine and clears the
        VIA configuration if Ctrl is held, simulating a hard reset.

        Args:
            hold_time: How long to hold Break (seconds).

        Returns:
            True if all operations succeeded.
        """
        self.ctrl_down()
        time.sleep(0.01)  # Brief delay to ensure Ctrl is registered
        down_ok = self.break_down()
        time.sleep(hold_time)
        up_ok = self.break_up()
        time.sleep(0.01)  # Brief delay before releasing Ctrl
        self.ctrl_up()
        return down_ok and up_ok

    # =========================================================================
    # Keyboard links (DIP switches)
    # =========================================================================
    # The BBC Micro has 8 keyboard links (row 0, columns 2-9) that form a
    # startup options byte. Active-low: bit SET = link broken (open),
    # bit CLEAR = link made.
    #
    # Bit layout:
    #   Bits 0-2: Screen mode (XOR'd with 7 by MOS, so 0x07 = Mode 7)
    #   Bit 3: SHIFT-BREAK action (1 = normal, 0 = reversed/auto-boot)
    #   Bits 4-7: ROM-dependent (disc timing, filing system, etc.)
    #
    # Default value 0xFF (all links broken) = Mode 7, normal SHIFT-BREAK.

    def get_links(self) -> int:
        """Get the raw keyboard links byte (8 bits).

        Returns:
            The current 8-bit links value (0-255).
        """
        request = keyboard_pb2.GetLinksRequest()
        response = self._stub.GetLinks(request)
        return response.value

    def set_links(self, value: int) -> bool:
        """Set the raw keyboard links byte (8 bits).

        Args:
            value: The 8-bit links value (0-255).

        Returns:
            True if successful.

        Raises:
            ValueError: If value is not in range 0-255.
        """
        if not 0 <= value <= 255:
            raise ValueError("Links value must be 0-255")
        request = keyboard_pb2.SetLinksRequest(value=value)
        response = self._stub.SetLinks(request)
        if not response.success:
            raise ValueError(response.error)
        return True

    def get_startup_screen_mode(self) -> int:
        """Get the startup screen mode (0-7).

        Returns:
            The configured startup screen mode.
        """
        request = keyboard_pb2.GetStartupScreenModeRequest()
        response = self._stub.GetStartupScreenMode(request)
        return response.mode

    def set_startup_screen_mode(self, mode: int) -> bool:
        """Set the startup screen mode (0-7).

        Args:
            mode: The screen mode (0-7).

        Returns:
            True if successful.

        Raises:
            ValueError: If mode is not in range 0-7.
        """
        if not 0 <= mode <= 7:
            raise ValueError("Mode must be 0-7")
        request = keyboard_pb2.SetStartupScreenModeRequest(mode=mode)
        response = self._stub.SetStartupScreenMode(request)
        if not response.success:
            raise ValueError(response.error)
        return True

    def get_startup_auto_boot(self) -> bool:
        """Check if auto-boot on SHIFT-BREAK is enabled.

        Returns:
            True if SHIFT-BREAK triggers auto-boot.
        """
        request = keyboard_pb2.GetStartupAutoBootRequest()
        response = self._stub.GetStartupAutoBoot(request)
        return response.enabled

    def set_startup_auto_boot(self, enabled: bool) -> bool:
        """Enable or disable auto-boot on SHIFT-BREAK.

        When enabled, SHIFT-BREAK causes MOS to load and run !Boot.

        Args:
            enabled: True to enable auto-boot.

        Returns:
            True if successful.
        """
        request = keyboard_pb2.SetStartupAutoBootRequest(enabled=enabled)
        response = self._stub.SetStartupAutoBoot(request)
        return response.success
