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

"""End-to-end test that the sidebar stops lying.

When the emulated BBC writes a new ROM image into a sideways RAM slot
at runtime, the gRPC stream must emit a ``SlotHeaderChangedEvent``
carrying the parsed header of the newly-loaded ROM. This drives the
exact failure mode that motivated the live header monitor: the user
``*SRLOAD``s a ROM at the keyboard, the sidebar should reflect what
just landed.

This test will fail until the server-side scanner described in
``docs/discussion/sideways-live-header-updates.md`` is implemented.
"""

from __future__ import annotations

import threading
import time

import pytest

from beebium.sideways import (
    SlotHeaderChangedEvent,
    SlotType,
)


# *SRLOAD is implemented inside the DFS SRAM utilities and reads through
# the filing system. With a 16 KiB ROM coming off a single-sided floppy
# image, the whole transfer is well under one emulated second; the
# scanner is 1 Hz so the event should arrive within a few seconds.
EMULATED_SECONDS_FOR_SRLOAD = 8.0
EVENT_WAIT_SECONDS = 6.0


def test_srload_into_sideways_ram_emits_header_changed_event(
    romram_with_srload_disc,
):
    """Run the emulator through `*SRLOAD R.ANFS 8000 7 Q` and expect a
    SlotHeaderChangedEvent for slot 7 carrying an ANFS-looking header.
    """
    bbc = romram_with_srload_disc

    # Sanity: before the SRLOAD, slot 7 is empty RAM (no recognised header).
    initial = bbc.sideways.get_slot_status()
    slot7 = initial.find_socket_for_slot(7)
    assert slot7 is not None
    assert slot7.type is SlotType.RAM
    assert slot7.rom_header is None, (
        f"slot 7 unexpectedly already has a header at boot: {slot7.rom_header!r}"
    )

    # Open the event stream BEFORE the *SRLOAD. The server only sees
    # events that occur after the stream is open.
    events: list = []
    stream_error: list = []

    def reader():
        try:
            for event in bbc.sideways.subscribe_events(
                monitor_header_changes=True,
            ):
                events.append(event)
        except Exception as exc:  # noqa: BLE001 - bubble up to the test
            stream_error.append(exc)

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    # Small grace period for the gRPC stream to be established on the server.
    time.sleep(0.2)

    # Drive the *SRLOAD. The Q suffix asks for the quick transfer; for
    # this test we don't care about preserving main RAM.
    bbc.keyboard.type("*SRLOAD R.ANFS 8000 7 Q")
    bbc.keyboard.press_return()

    # Let the emulator actually run the command and the scanner tick.
    bbc.run_for_emulated_seconds(EMULATED_SECONDS_FOR_SRLOAD)

    # Give the stream a moment to deliver the event, then look at what
    # arrived. The scanner runs at ~1 Hz so a few seconds of wall-clock
    # is plenty.
    deadline = time.monotonic() + EVENT_WAIT_SECONDS
    while time.monotonic() < deadline:
        if any(isinstance(e, SlotHeaderChangedEvent) and e.slot == 7
               for e in events):
            break
        time.sleep(0.1)

    assert not stream_error, (
        f"subscribe_events raised: {stream_error[0]!r}"
    )

    header_events = [
        e for e in events
        if isinstance(e, SlotHeaderChangedEvent) and e.slot == 7
    ]
    if not header_events:
        # Diagnostic: did the *SRLOAD actually land the bytes? If yes,
        # the test infrastructure is sound and the missing piece is the
        # server-side header-change scanner. If no, the boot/disc/
        # keyboard sequence has a problem upstream.
        after_state = bbc.sideways.get_slot_status()
        slot7_after = after_state.find_socket_for_slot(7)
        slot7_header = (
            slot7_after.rom_header if slot7_after is not None else None
        )
        pytest.fail(
            f"No SlotHeaderChangedEvent for slot 7 arrived within "
            f"{EVENT_WAIT_SECONDS:.0f}s after *SRLOAD.\n"
            f"  events seen: {events!r}\n"
            f"  post-state slot 7 header (via GetSlotStatus): {slot7_header!r}"
        )

    # The most recent event should describe the just-loaded ANFS ROM.
    final = header_events[-1]
    assert "ANFS" in final.rom_header.title, (
        f"Expected ANFS title in event, got {final.rom_header.title!r}"
    )
    # ANFS exposes both a language entry and a service entry on the B+
    # (it's the filing system). We only assert presence of the service
    # kind, since that's the load-bearing one.
    assert "service" in final.rom_header.kinds, (
        f"Expected 'service' in kinds, got {final.rom_header.kinds!r}"
    )

    # And the canonical snapshot read should now agree with the event.
    after = bbc.sideways.get_slot_status()
    slot7_after = after.find_socket_for_slot(7)
    assert slot7_after is not None
    assert slot7_after.rom_header is not None
    assert "ANFS" in slot7_after.rom_header.title
