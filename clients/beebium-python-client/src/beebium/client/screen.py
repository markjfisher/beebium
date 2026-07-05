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

"""MODE 7 screen-reading helpers for the beebium client.

The BBC scrolls MODE 7 in hardware: the 6845 moves the display start address
(R12/R13) and the 1 KB screen RAM behaves as a circular buffer. A fixed read
from 0x7C00 therefore returns a grid rotated relative to what is on screen once
any scrolling has occurred. These helpers anchor the read at the CRTC
screen-start so they return what is actually displayed.

The functions form a small composable library:

* ``cells_from_region`` / ``cells_to_text`` / ``linearise`` are pure transforms
  (no emulator required) over the raw region, the 25x40 grid, and a flattened
  view respectively.
* ``read_mode7_cells`` / ``read_mode7_screen`` / ``screen_contains`` / ``find``
  / ``dump_screen`` compose those transforms onto a running client.

The MODE 7 address mapping mirrors the emulator's own renderer
(``VideoRenderer::translate_screen_address``): displayed position ``p`` reads
``mem[0x7C00 + ((screen_start + p) & 0x3FF)]``.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import TYPE_CHECKING, Protocol, TypeVar, cast

if TYPE_CHECKING:
    from beebium.client.crtc import Crtc
    from beebium.client.memory import Memory

MODE7_BASE = 0x7C00
MODE7_REGION_SIZE = 0x400  # 1 KB MODE 7 screen RAM, addressed as a circular buffer
MODE7_COLS = 40
MODE7_ROWS = 25

# A row is 40 raw screen bytes; rows compose into the 25-row display grid.
Row = bytes | str

# Row-polymorphic helpers preserve the row type: given byte rows they return
# bytes, given text rows they return str.
RowT = TypeVar("RowT", bytes, str)

# A separator strategy decides what (if anything) to insert between two adjacent
# rows when flattening the grid. It receives the two full-width rows and returns
# a separator of the same type (bytes for byte rows, str for text rows), so that
# decisions based on the last/first cell of each row are possible.
Separator = Callable[[Row, Row], Row]


class _ScreenClient(Protocol):
    """The slice of the Beebium client these helpers need.

    Declared as read-only properties (not plain attributes) so the client's
    ``@property`` accessors satisfy the protocol -- a read-only property is not
    assignable to a writable protocol attribute.
    """

    @property
    def memory(self) -> Memory: ...
    @property
    def crtc(self) -> Crtc: ...


# ---------------------------------------------------------------------------
# Pure transforms
# ---------------------------------------------------------------------------


def cells_from_region(region: bytes, screen_start: int) -> list[bytes]:
    """Map the raw 1 KB MODE 7 region to the displayed 25x40 grid.

    Corrects for hardware scroll: ``region`` is treated as a circular buffer
    anchored at ``screen_start`` (the CRTC R12/R13 value). Only the low 10 bits
    of ``screen_start`` are significant.

    Args:
        region: The full ``MODE7_REGION_SIZE`` (1 KB) MODE 7 region, as read
            from 0x7C00.
        screen_start: The CRTC display start address (``bbc.crtc.state.screen_start``).

    Returns:
        25 rows of 40 raw bytes each, in display order (top-left first).
    """
    if len(region) < MODE7_REGION_SIZE:
        raise ValueError(
            f"region must be at least {MODE7_REGION_SIZE} bytes, got {len(region)}"
        )
    mask = MODE7_REGION_SIZE - 1
    offset = screen_start & mask
    rows: list[bytes] = []
    for row in range(MODE7_ROWS):
        base = offset + row * MODE7_COLS
        rows.append(bytes(region[(base + col) & mask] for col in range(MODE7_COLS)))
    return rows


def cells_to_text(cells: list[bytes]) -> list[str]:
    """Convert raw grid rows to printable text rows.

    Each byte is masked to 7 bits (the SAA5050 ignores bit 7, so e.g. 0xC1
    displays as 'A'). Printable ASCII (0x20-0x7E) is kept; teletext control
    codes (0x00-0x1F) and 0x7F become spaces.

    Args:
        cells: Rows of raw screen bytes.

    Returns:
        One text string per row.
    """
    text_rows: list[str] = []
    for row in cells:
        chars = []
        for byte in row:
            code = byte & 0x7F
            chars.append(chr(code) if 0x20 <= code <= 0x7E else " ")
        text_rows.append("".join(chars))
    return text_rows


def _is_letter(cell: int | str) -> bool:
    code = cell if isinstance(cell, int) else ord(cell)
    return 0x41 <= code <= 0x5A or 0x61 <= code <= 0x7A


def _like(template: RowT, byte_value: bytes, str_value: str) -> RowT:
    """Return byte_value or str_value, matching the row type of template.

    The isinstance check guarantees the correct branch; the cast tells the type
    checker what it cannot infer -- that the result matches the template type.
    """
    return cast(
        RowT, byte_value if isinstance(template, (bytes, bytearray)) else str_value
    )


def _blank(like: RowT) -> RowT:
    return _like(like, b"", "")


def _space(like: RowT) -> RowT:
    return _like(like, b" ", " ")


def no_separator(left: RowT, right: RowT) -> RowT:
    """Separator strategy: never insert anything between rows."""
    return _blank(left)


def spaced(left: RowT, right: RowT) -> RowT:
    """Separator strategy: always insert a single space between rows."""
    return _space(left)


def lined(left: RowT, right: RowT) -> RowT:
    """Separator strategy: insert a newline between rows."""
    return _like(left, b"\n", "\n")


def dewrapped(left: RowT, right: RowT) -> RowT:
    """Separator strategy that reassembles character-wrapped words.

    The BBC's default character wrap can split a word at the 40-column
    boundary, leaving the line full (a letter in the last column) and the word
    continuing in the first column of the next line. In that case no separator
    is inserted, so the word is rejoined. Otherwise a space is inserted.

    Note the inherent ambiguity: a character-wrapped line that happens to fill
    to column 40 ending a complete word, followed by a new word, is
    indistinguishable from a split word and will be joined without a space.
    Most application text is word-wrapped (the last column is a space), where
    this does not arise.
    """
    if left and right and _is_letter(left[-1]) and _is_letter(right[0]):
        return _blank(left)
    return _space(left)


def linearise(rows: list[RowT], separator: Callable[[RowT, RowT], RowT] = spaced) -> RowT:
    """Flatten grid rows into a single sequence for searching or display.

    Trailing padding is stripped from each row; ``separator`` decides what to
    place between adjacent rows (it sees the full, unstripped rows).

    Args:
        rows: Grid rows, all ``bytes`` or all ``str``.
        separator: A separator strategy (e.g. ``spaced``, ``lined``,
            ``no_separator``, ``dewrapped``).

    Returns:
        The flattened ``bytes`` or ``str`` (matching the row type).
    """
    if not rows:
        # No rows to sample the type from; the historical fallback is an empty
        # string.
        return cast(RowT, "")
    parts: list[RowT] = []
    last = len(rows) - 1
    for index, row in enumerate(rows):
        parts.append(row.rstrip())
        if index < last:
            parts.append(separator(row, rows[index + 1]))
    # The parts are homogeneous (all the row type), but the type checker cannot
    # prove join() preserves that, so branch on the concrete type and assert it.
    if isinstance(rows[0], bytes):
        return cast(RowT, b"".join(cast("list[bytes]", parts)))
    return cast(RowT, "".join(cast("list[str]", parts)))


# ---------------------------------------------------------------------------
# Composition layer (operates on a running client)
# ---------------------------------------------------------------------------


def read_mode7_cells(bbc: _ScreenClient) -> list[bytes]:
    """Read the MODE 7 display as a scroll-corrected 25x40 grid of raw bytes.

    Reads the full 1 KB region and the CRTC screen-start, then maps them
    through the circular buffer. These are two separate reads; call this while
    the machine is paused (or otherwise settled) so they describe one frame.

    Args:
        bbc: A Beebium client.

    Returns:
        25 rows of 40 raw bytes each, in display order.
    """
    region = bbc.memory.address.peek.read(MODE7_BASE, MODE7_REGION_SIZE)
    return cells_from_region(region, bbc.crtc.state.screen_start)


def read_mode7_screen(bbc: _ScreenClient) -> list[str]:
    """Read the MODE 7 display as 25 scroll-corrected rows of 40 characters.

    Args:
        bbc: A Beebium client.

    Returns:
        A list of 25 strings, each 40 characters long.
    """
    return cells_to_text(read_mode7_cells(bbc))


def screen_contains(bbc: _ScreenClient, text: str) -> bool:
    """Return whether ``text`` appears anywhere on the MODE 7 display.

    The display is flattened with the ``dewrapped`` strategy, so text is found
    whether or not it has been wrapped across a line boundary.

    Args:
        bbc: A Beebium client.
        text: The text to search for.

    Returns:
        True if the text is present on screen.
    """
    return text in linearise(read_mode7_screen(bbc), dewrapped)


def find(bbc: _ScreenClient, text: str) -> tuple[int, int] | None:
    """Locate ``text`` within a single row of the MODE 7 display.

    Searches each row independently and returns the first match. Text that
    spans a row boundary is not located here; use ``screen_contains`` to test
    for presence anywhere on screen.

    Args:
        bbc: A Beebium client.
        text: The text to locate.

    Returns:
        ``(row, column)`` of the first occurrence, or ``None`` if not found.
    """
    for row, line in enumerate(read_mode7_screen(bbc)):
        column = line.find(text)
        if column != -1:
            return (row, column)
    return None


def dump_screen(bbc: _ScreenClient) -> str:
    """Dump the MODE 7 display as a formatted string for diagnostics.

    Args:
        bbc: A Beebium client.

    Returns:
        A multi-line string with the screen-start address, row numbers, and
        scroll-corrected screen content.
    """
    screen_start = bbc.crtc.state.screen_start
    rows = read_mode7_screen(bbc)
    lines = [f"MODE 7 screen (screen_start={screen_start:04X}):"]
    for index, row in enumerate(rows):
        lines.append(f"Row {index:2d}: [{row}]")
    return "\n".join(lines)
