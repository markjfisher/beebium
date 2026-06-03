# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
# Copyright 2026 Mark J. Fisher
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

"""Tests for the serial port (MC6850 ACIA + Serial ULA) client.

Two layers:
  - Mock-based unit tests of the ``Serial`` wrapper (deterministic, no server).
  - Integration tests that drive a real ``beebium-server`` via the ``bbc``
    fixture, exercising the SerialService and the bit-level serial engine.
"""

from __future__ import annotations

import os
import select
import sys
import time
from unittest.mock import MagicMock

import pytest

from beebium._proto import serial_pb2
from beebium.exceptions import SerialError
from beebium.serial import EndpointMode, Serial, _response_to_status


# =============================================================================
# Mock-based unit tests
# =============================================================================


def _make_status_proto(**overrides):
    defaults = dict(
        has_serial_socket=True,
        acia_control=0x15,
        acia_status=0x02,
        tdre=True,
        rdrf=False,
        not_dcd=False,
        not_cts=False,
        irq_pending=False,
        ula_control=0x40,
        tx_baud=19200,
        rx_baud=19200,
        rs423_selected=True,
        motor_on=False,
        tx_bit_period=104,
        rx_bit_period=104,
        endpoint_mode=serial_pb2.SERIAL_ENDPOINT_SCRIPTABLE,
        tx_pending=0,
        rx_pending=0,
    )
    defaults.update(overrides)
    return serial_pb2.SerialStatus(**defaults)


def test_status_parses_response():
    stub = MagicMock()
    stub.GetSerialStatus.return_value = _make_status_proto(tx_baud=9600, motor_on=True)
    serial = Serial(stub)

    status = serial.status
    assert status.has_serial_socket is True
    assert status.acia_control == 0x15
    assert status.tdre is True
    assert status.tx_baud == 9600
    assert status.motor_on is True
    assert status.endpoint_mode == EndpointMode.SCRIPTABLE


def test_response_to_status_endpoint_mode_enum():
    proto = _make_status_proto(endpoint_mode=serial_pb2.SERIAL_ENDPOINT_LOOPBACK)
    status = _response_to_status(proto)
    assert status.endpoint_mode is EndpointMode.LOOPBACK


def test_set_endpoint_mode_success():
    stub = MagicMock()
    stub.SetEndpointMode.return_value = serial_pb2.SetEndpointModeResponse(success=True)
    serial = Serial(stub)

    serial.set_endpoint_mode(EndpointMode.LOOPBACK)
    request = stub.SetEndpointMode.call_args[0][0]
    assert request.mode == serial_pb2.SERIAL_ENDPOINT_LOOPBACK


def test_set_endpoint_mode_failure_raises():
    stub = MagicMock()
    stub.SetEndpointMode.return_value = serial_pb2.SetEndpointModeResponse(
        success=False, error="no serial socket"
    )
    serial = Serial(stub)

    with pytest.raises(SerialError, match="no serial socket"):
        serial.set_endpoint_mode(EndpointMode.SCRIPTABLE)


def test_send_to_device_returns_queued_count():
    stub = MagicMock()
    stub.SendToDevice.return_value = serial_pb2.SendToDeviceResponse(
        success=True, queued=5
    )
    serial = Serial(stub)

    assert serial.send_to_device(b"HELLO") == 5
    request = stub.SendToDevice.call_args[0][0]
    assert request.data == b"HELLO"


def test_send_to_device_failure_raises():
    stub = MagicMock()
    stub.SendToDevice.return_value = serial_pb2.SendToDeviceResponse(
        success=False, error="not scriptable"
    )
    serial = Serial(stub)

    with pytest.raises(SerialError, match="not scriptable"):
        serial.send_to_device(b"x")


def test_receive_from_device_returns_bytes():
    stub = MagicMock()
    stub.ReceiveFromDevice.return_value = serial_pb2.ReceiveFromDeviceResponse(
        success=True, data=b"WORLD"
    )
    serial = Serial(stub)

    assert serial.receive_from_device(max_bytes=8) == b"WORLD"
    request = stub.ReceiveFromDevice.call_args[0][0]
    assert request.max_bytes == 8


# =============================================================================
# Integration tests (real beebium-server via the `bbc` fixture)
# =============================================================================

# MC6850 control: /16 divide, 8N1, /RTS low, no TX IRQ -- the MOS serial config.
_ACIA_8N1 = 0x15
# Serial ULA: RS423 select (carrier present), 19200 baud on TX and RX.
_ULA_RS423_19200 = 0x40


def _program_serial(bbc):
    """Poke the ACIA + Serial ULA into the standard 19200 8N1 configuration."""
    bbc.memory.address.bus[0xFE08] = _ACIA_8N1   # ACIA control
    bbc.memory.address.bus[0xFE10] = _ULA_RS423_19200  # Serial ULA latch


def test_serial_status_available(bbc):
    status = bbc.serial.status
    assert status.has_serial_socket is True


def test_scriptable_transmit_round_trip(stopped_bbc):
    """A byte written to the ACIA TDR is shifted out and collected by the client."""
    bbc = stopped_bbc
    bbc.serial.set_endpoint_mode(EndpointMode.SCRIPTABLE)
    _program_serial(bbc)

    # Write the transmit data register (&FE09) through the bus, then run the
    # bit clocks long enough to serialise a full character (10 bits * 104
    # cycles/bit, with generous margin).
    bbc.memory.address.bus[0xFE09] = ord("Q")
    bbc.debugger.step_cycles(4000)

    received = bbc.serial.receive_from_device()
    assert bytes(received) == b"Q"


def test_scriptable_receive_round_trip(stopped_bbc):
    """Bytes injected by the client are shifted into the ACIA and read back."""
    bbc = stopped_bbc
    bbc.serial.set_endpoint_mode(EndpointMode.SCRIPTABLE)
    _program_serial(bbc)

    bbc.serial.send_to_device(b"Z")

    # Run until RDRF asserts (or a cycle budget is exhausted).
    got_rdrf = False
    for _ in range(10):
        bbc.debugger.step_cycles(2000)
        status_byte = bbc.memory.address.peek[0xFE08]
        if status_byte & 0x01:  # RDRF
            got_rdrf = True
            break
    assert got_rdrf, "ACIA never reported RDRF for the injected byte"

    # Reading the data register returns the received byte.
    assert bbc.memory.address.bus[0xFE09] == ord("Z")


def test_loopback_echo(stopped_bbc):
    """In loopback mode a transmitted byte is echoed back to the receiver."""
    bbc = stopped_bbc
    bbc.serial.set_endpoint_mode(EndpointMode.LOOPBACK)
    _program_serial(bbc)

    bbc.memory.address.bus[0xFE09] = ord("K")  # transmit

    got_rdrf = False
    for _ in range(12):
        bbc.debugger.step_cycles(2000)
        if bbc.memory.address.peek[0xFE08] & 0x01:
            got_rdrf = True
            break
    assert got_rdrf, "loopback byte never arrived at the receiver"
    assert bbc.memory.address.bus[0xFE09] == ord("K")


# Note: the PTY (and real serial device) round-trip is no longer a SerialService
# concern -- it moved to the host-serial PeripheralExtension. It is covered by
# the C++ test_host_serial_extension (a device-mode round trip through a real
# pty). A Python integration test that launches the server with
# --host-serial mode=pty belongs with the host-serial client work (plan step 15).
