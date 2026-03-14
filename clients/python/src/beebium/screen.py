# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

"""MODE 7 screen reading helpers for the beebium client."""

from __future__ import annotations

from beebium.memory import Memory

MODE7_START = 0x7C00
MODE7_END = 0x7FFF
MODE7_COLS = 40
MODE7_ROWS = 25


def read_mode7_screen(memory: Memory) -> list[str]:
    """Read MODE 7 screen memory as 25 rows of 40 characters.

    Bytes in the range 0x20-0x7E are returned as ASCII characters.
    All other bytes (including teletext control codes 0x00-0x1F
    and 0x7F) are replaced with spaces.

    Args:
        memory: A Memory instance (e.g., bbc.memory).

    Returns:
        A list of 25 strings, each 40 characters long.
    """
    data = memory.address.peek.read(MODE7_START, MODE7_ROWS * MODE7_COLS)
    rows = []
    for row in range(MODE7_ROWS):
        offset = row * MODE7_COLS
        row_bytes = data[offset:offset + MODE7_COLS]
        chars = []
        for b in row_bytes:
            if 0x20 <= b <= 0x7E:
                chars.append(chr(b))
            else:
                chars.append(" ")
        rows.append("".join(chars))
    return rows


def screen_contains(memory: Memory, text: str) -> bool:
    """Search MODE 7 screen memory for an ASCII string.

    Searches the raw byte values in screen memory (0x7C00-0x7FFF)
    for the given text, ignoring teletext control codes.

    Args:
        memory: A Memory instance (e.g., bbc.memory).
        text: The ASCII text to search for.

    Returns:
        True if the text is found in screen memory.
    """
    data = memory.address.peek.read(MODE7_START, MODE7_END - MODE7_START + 1)
    text_bytes = text.encode("ascii")
    return text_bytes in data


def dump_screen(memory: Memory) -> str:
    """Dump MODE 7 screen memory as a formatted string for diagnostics.

    Args:
        memory: A Memory instance (e.g., bbc.memory).

    Returns:
        A multi-line string with row numbers and screen content.
    """
    rows = read_mode7_screen(memory)
    lines = []
    for i, row in enumerate(rows):
        lines.append(f"Row {i:2d}: [{row}]")
    return "\n".join(lines)
