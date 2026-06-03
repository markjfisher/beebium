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

"""Integration tests for the serial device extensions over gRPC.

Each launches a real server with a serial extension owning the serial port and
drives the BBC's ACIA/Serial ULA through the bus, exactly as a ROM would.
"""

from __future__ import annotations

import pytest

from beebium import Beebium
from beebium.exceptions import ServerNotFoundError

# MC6850 control: /16 divide, 8N1, /RTS low, no TX IRQ -- the MOS serial config.
_ACIA_8N1 = 0x15
# Serial ULA: RS423 select (carrier present), 19200 baud on TX and RX.
_ULA_RS423_19200 = 0x40


def _program_serial(bbc: Beebium) -> None:
    """Poke the ACIA + Serial ULA into the standard 19200 8N1 configuration."""
    bbc.memory.address.bus[0xFE08] = _ACIA_8N1
    bbc.memory.address.bus[0xFE10] = _ULA_RS423_19200


@pytest.fixture
def rpc_serial_bbc(mos_filepath, basic_filepath, beebium_server_filepath):
    """A BBC whose serial port is owned by the rpc-serial extension."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=["--rpc-serial"],
        ) as instance:
            instance.debugger.stop()
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.fixture
def loopback_serial_bbc(mos_filepath, basic_filepath, beebium_server_filepath):
    """A BBC whose serial port is owned by the loopback-serial extension."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=["--loopback-serial"],
        ) as instance:
            instance.debugger.stop()
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_rpc_serial_transmit_round_trip(rpc_serial_bbc):
    """A byte the BBC transmits is collected by the rpc-serial client."""
    bbc = rpc_serial_bbc
    _program_serial(bbc)

    bbc.memory.address.bus[0xFE09] = ord("Q")  # write TDR
    bbc.debugger.step_cycles(4000)

    assert bytes(bbc.rpc_serial.receive()) == b"Q"


def test_rpc_serial_receive_round_trip(rpc_serial_bbc):
    """Bytes the rpc-serial client sends are shifted into the ACIA."""
    bbc = rpc_serial_bbc
    _program_serial(bbc)

    assert bbc.rpc_serial.send(b"Z") == 1

    got_rdrf = False
    for _ in range(10):
        bbc.debugger.step_cycles(2000)
        if bbc.memory.address.peek[0xFE08] & 0x01:  # RDRF
            got_rdrf = True
            break
    assert got_rdrf, "ACIA never reported RDRF for the injected byte"
    assert bbc.memory.address.bus[0xFE09] == ord("Z")


def test_loopback_serial_echo(loopback_serial_bbc):
    """The loopback-serial extension echoes a transmitted byte back to RDRF."""
    bbc = loopback_serial_bbc
    _program_serial(bbc)

    bbc.memory.address.bus[0xFE09] = ord("K")  # transmit

    got_rdrf = False
    for _ in range(12):
        bbc.debugger.step_cycles(2000)
        if bbc.memory.address.peek[0xFE08] & 0x01:  # RDRF
            got_rdrf = True
            break
    assert got_rdrf, "loopback byte never arrived at the receiver"
    assert bbc.memory.address.bus[0xFE09] == ord("K")
