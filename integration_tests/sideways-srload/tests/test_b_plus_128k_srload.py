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

"""B+ 128K *SRLOAD repro for the bug seen in the running app.

User report: ``*SRLOAD R.ADFS 8000 W Q`` on the B+ 128K does not
produce a SlotHeaderChangedEvent, and pressing Break afterwards
prints "Bad sum" and hangs the MOS. The hang suggests the bytes
landing in SRAM W (slot 12) don't match the source ROM, so the
MOS's sideways-ROM scan rejects it.

This test does the equivalent end-to-end on the emulator and checks
the bytes byte-for-byte against the source ANFS image. The
SlotHeaderChangedEvent and the live header fields in GetSlotStatus
are checked too so we can tell whether it's a load-time corruption,
a scanner gap, or both.
"""

from __future__ import annotations

import threading
import time
from pathlib import Path

import pytest

from beebium.sideways import (
    SlotHeaderChangedEvent,
    SlotType,
)


SRAM_W_SLOT = 12

# *SRLOAD ... Q reads the whole 16 KiB ROM through a main-RAM buffer
# between OSHWM and the bottom of screen memory. On the B+ in Mode 7
# that completes in ~1 emulated second; give it a wide margin.
EMULATED_SECONDS_FOR_SRLOAD = 8.0
EVENT_WAIT_SECONDS = 6.0


def test_srload_adfs_into_sram_w_lands_byte_for_byte(
    b_plus_128k_with_adfs_disc,
    adfs_rom_filepath: Path,
):
    """After *SRLOAD R.ADFS 8000 W Q the bytes in SRAM W must equal
    the source ROM, GetSlotStatus must report the parsed ADFS header,
    and the live SlotHeaderChangedEvent must arrive.
    """
    bbc = b_plus_128k_with_adfs_disc
    source_bytes = adfs_rom_filepath.read_bytes()
    assert len(source_bytes) == 16384, "ADFS ROM should be 16 KiB"

    # Sanity: slot 12 is RAM and empty before the load.
    initial = bbc.sideways.get_slot_status()
    slot12 = initial.find_socket_for_slot(SRAM_W_SLOT)
    assert slot12 is not None
    assert slot12.type is SlotType.RAM
    assert slot12.rom_header is None, (
        f"slot 12 unexpectedly already has a header at boot: "
        f"{slot12.rom_header!r}"
    )

    # Open the event stream before driving *SRLOAD.
    events: list = []
    stream_error: list = []

    def reader():
        try:
            for event in bbc.sideways.subscribe_events(
                monitor_header_changes=True,
            ):
                events.append(event)
        except Exception as exc:  # noqa: BLE001
            stream_error.append(exc)

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    time.sleep(0.2)

    bbc.keyboard.type("*SRLOAD R.ADFS 8000 W Q")
    bbc.keyboard.press_return()
    bbc.run_for_emulated_seconds(EMULATED_SECONDS_FOR_SRLOAD)

    # Read the post-load contents of slot 12 and diff against the source.
    loaded = bbc.sideways.read_slot_data(SRAM_W_SLOT, offset=0, length=16384)

    if loaded != source_bytes:
        # Find the first mismatch for a useful diff message.
        first_diff = next(
            (i for i in range(len(source_bytes))
             if i >= len(loaded) or loaded[i] != source_bytes[i]),
            -1,
        )
        loaded_byte = (
            f"0x{loaded[first_diff]:02x}"
            if first_diff < len(loaded) else "out-of-range"
        )
        source_byte = f"0x{source_bytes[first_diff]:02x}"
        mismatch_count = sum(
            1 for i in range(min(len(loaded), len(source_bytes)))
            if loaded[i] != source_bytes[i]
        )
        pytest.fail(
            f"SRAM W bytes do not match the source ROM after *SRLOAD.\n"
            f"  source length: {len(source_bytes)}\n"
            f"  read length:   {len(loaded)}\n"
            f"  first diff at offset 0x{first_diff:04x}: "
            f"loaded={loaded_byte}, source={source_byte}\n"
            f"  total mismatching bytes: {mismatch_count}"
        )

    # If bytes match, the header should be parsed correctly too.
    after = bbc.sideways.get_slot_status()
    slot12_after = after.find_socket_for_slot(SRAM_W_SLOT)
    assert slot12_after is not None
    assert slot12_after.rom_header is not None, (
        "GetSlotStatus didn't return a rom_header for SRAM W even though the "
        "bytes match the source ROM. Header parser regression."
    )
    assert "ADFS" in slot12_after.rom_header.title, (
        f"Expected ADFS in title; got {slot12_after.rom_header.title!r}"
    )

    # And the live scanner should have emitted an event.
    deadline = time.monotonic() + EVENT_WAIT_SECONDS
    while time.monotonic() < deadline:
        if any(isinstance(e, SlotHeaderChangedEvent) and e.slot == SRAM_W_SLOT
               for e in events):
            break
        time.sleep(0.1)

    assert not stream_error, f"subscribe_events raised: {stream_error[0]!r}"

    header_events = [
        e for e in events
        if isinstance(e, SlotHeaderChangedEvent) and e.slot == SRAM_W_SLOT
    ]
    assert header_events, (
        f"No SlotHeaderChangedEvent for slot 12 within {EVENT_WAIT_SECONDS}s "
        f"after *SRLOAD. Events seen: {events!r}"
    )
    final = header_events[-1]
    assert "ADFS" in final.rom_header.title, (
        f"Expected ADFS in event header; got {final.rom_header.title!r}"
    )
