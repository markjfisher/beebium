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
