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

"""DFS disc write-back integration tests.

Reproduces the reported failure where data saved to a .ssd image is not flushed
through to the host filesystem once drive activity has ended: the catalogue
entry appears, but the file's data sectors are empty or truncated when read by
an external program.

These tests deliberately read the host .ssd file with oaknut-dfs -- a separate,
out-of-process DFS implementation -- so the check is independent of any code
inside Beebium.
"""

from __future__ import annotations

from oaknut.dfs import DFS, ACORN_DFS_40T_SINGLE_SIDED

from beebium.client import Beebium
from beebium.screen import read_mode7_screen, dump_screen


# 8 KiB = 32 sectors. On a freshly-formatted disc DFS lays the file down from
# sector 2, so it spans tracks 0, 1, 2 and 3. Tracks 1-3 hold only file data
# (no catalogue), which is precisely what is lost when flushing is tied to the
# catalogue track alone.
SAVE_ADDRESS = 0x3000
SAVE_LENGTH = 0x2000
FILENAME = "DATA"


def _pattern(length: int) -> bytes:
    """A deterministic byte pattern that differs in every 256-byte sector.

    Each byte encodes both its sector index and its offset, so a sector that
    is missing, zeroed, or written to the wrong place is detectable.
    """
    return bytes(((i >> 8) * 31 + (i & 0xFF) * 7 + 13) & 0xFF for i in range(length))


def _prompt_after(bbc: Beebium, command_fragment: str) -> bool:
    """True once a '>' prompt appears on a line after the echoed command."""
    seen_command = False
    for row in read_mode7_screen(bbc):
        stripped = row.strip()
        if command_fragment in stripped:
            seen_command = True
        elif seen_command and stripped == ">":
            return True
    return False


def _save_multitrack_file(bbc: Beebium) -> bytes:
    """Poke a known pattern into RAM and *SAVE it as a multi-track DFS file.

    Returns the pattern that was written so the caller can compare.
    """
    pattern = _pattern(SAVE_LENGTH)

    # Select DFS as the current filing system.
    bbc.keyboard.type("*DISC\r")
    assert bbc.run_until_or_timeout(
        lambda: _prompt_after(bbc, "DISC"),
        emulated_seconds=5.0,
    ), f"*DISC did not complete:\n{dump_screen(bbc)}"

    # Place the known data directly in main RAM, clear of the MODE 7 screen
    # (&7C00) and of DFS/BASIC workspace.
    bus = bbc.memory.address.bus
    bus[SAVE_ADDRESS:SAVE_ADDRESS + SAVE_LENGTH] = pattern
    written = bytes(bbc.memory.address.peek[SAVE_ADDRESS:SAVE_ADDRESS + SAVE_LENGTH])
    assert written == pattern, "RAM did not hold the pattern before *SAVE"

    # *SAVE takes hexadecimal addresses; +length form saves SAVE_LENGTH bytes.
    command = f"*SAVE {FILENAME} {SAVE_ADDRESS:X} +{SAVE_LENGTH:X}\r"
    bbc.keyboard.type(command)
    assert bbc.run_until_or_timeout(
        lambda: _prompt_after(bbc, f"SAVE {FILENAME}"),
        emulated_seconds=30.0,
    ), f"*SAVE did not complete:\n{dump_screen(bbc)}"

    return pattern


def _wait_for_activity_end(bbc: Beebium) -> None:
    """Run emulated time until the drive motor (activity LED) is off.

    This is the user-visible 'activity has ended' boundary: once the LED is off
    and the disc is quiescent, every byte written should be on the host disc.
    """
    ended = bbc.run_until_or_timeout(
        lambda: not bbc.disc.drive0.motor_on,
        emulated_seconds=8.0,
    )
    assert ended, "Drive motor never spun down after the save"


def _read_host_file(ssd_filepath) -> tuple[object, bytes]:
    """Read FILENAME from the host .ssd using oaknut-dfs (out-of-process)."""
    raw = ssd_filepath.read_bytes()
    dfs = DFS.from_buffer(memoryview(bytearray(raw)), ACORN_DFS_40T_SINGLE_SIDED)
    entry = dfs.path(f"$.{FILENAME}")
    assert entry.exists(), (
        f"DFS catalogue on the host disc has no file {FILENAME!r}; "
        f"catalogue: {[str(p) for p in dfs.path('$').iterdir()]}"
    )
    return entry.stat(), bytes(entry.read_bytes())


def test_data_flushed_while_motor_still_running(bbc_dfs, blank_ssd_filepath):
    """Writes reach the host disc on a short inactivity timer, not motor-off.

    Persistence is decoupled from the ~2s motor spin-down (and from the eject
    quiescence path): a brief pause after the writes is enough. This advances
    only a fraction of a second of emulated time -- long enough for the
    controller's write-inactivity flush, but well short of motor spin-down --
    and asserts the data is already on the host disc while the motor is still
    running.
    """
    pattern = _save_multitrack_file(bbc_dfs)

    # Run less emulated time than the motor idle timeout (~2s) but more than the
    # write-inactivity flush threshold (~0.25s).
    bbc_dfs.run_for_emulated_seconds(0.6)

    assert bbc_dfs.disc.drive0.motor_on, (
        "Motor already spun down; cannot prove flush happened before motor-off"
    )

    _, data = _read_host_file(blank_ssd_filepath)
    assert data == pattern, (
        "Data was not flushed to the host disc while the motor was still on; "
        "persistence is still coupled to motor spin-down"
    )


def test_catalogue_entry_is_persisted(bbc_dfs, blank_ssd_filepath):
    """The DFS catalogue entry reaches the host disc with the right length."""
    _save_multitrack_file(bbc_dfs)
    _wait_for_activity_end(bbc_dfs)

    stat, _ = _read_host_file(blank_ssd_filepath)
    assert stat.length == SAVE_LENGTH, (
        f"Catalogue records length {stat.length:#x}, expected {SAVE_LENGTH:#x}"
    )


def test_file_data_is_flushed_to_host_disc(bbc_dfs, blank_ssd_filepath):
    """All data sectors are flushed to the host .ssd once activity ends.

    This is the core regression: with the bug present the catalogue is written
    but data sectors on the non-catalogue tracks remain only in memory, so the
    bytes read by oaknut-dfs are zeroed/truncated.
    """
    pattern = _save_multitrack_file(bbc_dfs)
    _wait_for_activity_end(bbc_dfs)

    stat, data = _read_host_file(blank_ssd_filepath)

    assert stat.length == SAVE_LENGTH, "catalogue length wrong (precondition)"
    assert len(data) == len(pattern), (
        f"Read {len(data)} bytes from host disc, expected {len(pattern)}"
    )

    if data != pattern:
        # Report the first divergent sector to make the failure diagnosable.
        sector_size = 256
        for sector in range(len(pattern) // sector_size):
            lo = sector * sector_size
            got = data[lo:lo + sector_size]
            want = pattern[lo:lo + sector_size]
            if got != want:
                zeroed = "zeroed" if set(got) == {0} else "corrupt"
                raise AssertionError(
                    f"File data not flushed to host disc: sector {sector} "
                    f"(file offset {lo:#x}, disc sector {stat.start_sector + sector}) "
                    f"is {zeroed}. Catalogue entry is correct, so the symptom is "
                    f"'catalogue present, data missing'."
                )
        raise AssertionError("File data differs from the saved pattern")
