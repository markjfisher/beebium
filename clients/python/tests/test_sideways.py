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

"""Integration tests for the Python Sideways client.

Drive a real beebium-server (the default Model B) and assert the
Sideways wrapper maps GetSlotStatus / SubscribeEvents into the
typed Python dataclasses correctly.
"""

from __future__ import annotations

import pytest

from beebium.exceptions import BeebiumError
from beebium.sideways import (
    BankSelectedEvent,
    RomHeader,
    SlotConfiguredEvent,
    SlotStatusReport,
    SlotType,
    SocketStatus,
)


def test_get_slot_status_returns_typed_report(bbc):
    """Default Model B preset loads BASIC; GetSlotStatus must reflect it."""
    status = bbc.sideways.get_slot_status()

    assert isinstance(status, SlotStatusReport)
    assert status.has_aliasing is True
    # Model B has four physical sockets (IC52, IC88, IC100, IC101).
    assert status.num_physical_slots == 4
    assert len(status.sockets) == 4

    for socket in status.sockets:
        assert isinstance(socket, SocketStatus)
        # Model B aliases each socket to four slots via partial decoding.
        assert len(socket.aliased_slots) == 4

    # IC101 (socket 3) holds BASIC, mirrored at slots 3, 7, 11, 15.
    ic101 = status.find_socket_for_slot(15)
    assert ic101 is not None
    assert ic101.label == "IC101"
    assert ic101.priority == 15
    assert ic101.type is SlotType.ROM
    assert ic101.populated is True

    header = ic101.rom_header
    assert isinstance(header, RomHeader)
    assert header.title == "BASIC"
    assert "language" in header.kinds


def test_empty_sockets_have_no_rom_header(bbc):
    status = bbc.sideways.get_slot_status()
    # Sockets other than IC101 are empty in the stock Model B fixture.
    # On Model B the AliasedBankedMemory keeps slot type as the default
    # (ROM) until configure_socket runs, so populated / rom_header are
    # the load-bearing "is anything here" signals - not type.
    empty = [s for s in status.sockets if s.label != "IC101"]
    assert len(empty) == 3
    for socket in empty:
        assert socket.populated is False
        assert socket.rom_header is None


def test_configure_slot_rejects_non_runtime_configurable(bbc):
    """Model B sockets are real chips - ConfigureSlot must error."""
    with pytest.raises(BeebiumError):
        bbc.sideways.configure_slot(15, SlotType.RAM)


def test_read_slot_data_returns_bytes(bbc):
    # First 16 bytes of slot 15 are BASIC's header.
    data = bbc.sideways.read_slot_data(15, offset=0, length=16)
    assert isinstance(data, bytes)
    assert len(data) == 16
    # BBC BASIC 2 starts with a CMP immediate (0xC9) - same signature
    # checked by the C++ debugger test for bank_15.
    assert data[0] == 0xC9


def test_subscribe_events_yields_typed_events(bbc):
    """Writing ROMSEL (&FE30) should produce a typed BankSelectedEvent.

    Subscription has to start before the writes - the stream only
    delivers events that arrive after the server has it open.
    """
    import threading

    seen: list = []

    def reader():
        events = bbc.sideways.subscribe_events()
        for event in events:
            seen.append(event)
            if len(seen) >= 1:
                return

    bbc.debugger.stop()
    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    try:
        # Poke ROMSEL (&FE30) to bank 0 - the server emits a
        # BankSelectedEvent on the *change* from the previous bank.
        bbc.memory.address.bus.write(0xFE30, bytes([0]))
        thread.join(timeout=5.0)
    finally:
        bbc.debugger.run()

    assert seen, "no event arrived within 5 s"
    assert isinstance(seen[0], (SlotConfiguredEvent, BankSelectedEvent))
