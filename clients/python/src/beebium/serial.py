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

"""Serial port (MC6850 ACIA + Serial ULA) client for beebium.

The serial port is the BBC Micro's on-board RS423 / cassette interface. This
module wraps ``SerialService``, providing status queries plus a scriptable,
in-process transport so scripts and tests can:

  - inject bytes for the BBC to receive (``send_to_device``), and
  - collect bytes the BBC has transmitted (``receive_from_device``),

without any PTY or external device. A real PTY/serial bridge to fujinet-nio is
a separate transport that plugs in at the application layer.

Endpoint modes (``EndpointMode``):

  - ``NONE``: no transport attached -- receive idles, transmit is discarded.
  - ``LOOPBACK``: transmitted bytes echo straight back to the receiver.
  - ``SCRIPTABLE``: client-driven via ``send_to_device`` / ``receive_from_device``
    (the default).
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum

from beebium._proto import serial_pb2, serial_pb2_grpc
from beebium.exceptions import SerialError


class EndpointMode(IntEnum):
    """Host transport endpoint attached to the serial port."""

    NONE = serial_pb2.SERIAL_ENDPOINT_NONE
    LOOPBACK = serial_pb2.SERIAL_ENDPOINT_LOOPBACK
    SCRIPTABLE = serial_pb2.SERIAL_ENDPOINT_SCRIPTABLE


@dataclass(frozen=True)
class SerialStatus:
    """Serial hardware status (MC6850 ACIA + Serial ULA)."""

    has_serial_socket: bool
    # MC6850 ACIA
    acia_control: int
    acia_status: int
    tdre: bool
    rdrf: bool
    not_dcd: bool
    not_cts: bool
    irq_pending: bool
    # Serial ULA (SERPROC)
    ula_control: int
    tx_baud: int
    rx_baud: int
    rs423_selected: bool
    motor_on: bool
    tx_bit_period: int
    rx_bit_period: int
    # Host transport endpoint
    endpoint_mode: EndpointMode
    tx_pending: int
    rx_pending: int


def _response_to_status(response: serial_pb2.SerialStatus) -> SerialStatus:
    """Convert a SerialStatus proto into a SerialStatus dataclass."""
    return SerialStatus(
        has_serial_socket=response.has_serial_socket,
        acia_control=response.acia_control,
        acia_status=response.acia_status,
        tdre=response.tdre,
        rdrf=response.rdrf,
        not_dcd=response.not_dcd,
        not_cts=response.not_cts,
        irq_pending=response.irq_pending,
        ula_control=response.ula_control,
        tx_baud=response.tx_baud,
        rx_baud=response.rx_baud,
        rs423_selected=response.rs423_selected,
        motor_on=response.motor_on,
        tx_bit_period=response.tx_bit_period,
        rx_bit_period=response.rx_bit_period,
        endpoint_mode=EndpointMode(response.endpoint_mode),
        tx_pending=response.tx_pending,
        rx_pending=response.rx_pending,
    )


class Serial:
    """Serial port (MC6850 ACIA + Serial ULA) management.

    Usage::

        # Make the BBC receive some bytes
        bbc.serial.set_endpoint_mode(EndpointMode.SCRIPTABLE)
        bbc.serial.send_to_device(b"HELLO")

        # Collect bytes the BBC transmitted
        data = bbc.serial.receive_from_device()

        # Inspect live register state
        status = bbc.serial.status
        print(f"tx_baud={status.tx_baud}, rdrf={status.rdrf}")
    """

    def __init__(self, stub: serial_pb2_grpc.SerialServiceStub):
        """Create a Serial interface.

        Args:
            stub: The gRPC stub for the SerialService.
        """
        self._stub = stub

    @property
    def status(self) -> SerialStatus:
        """Get serial hardware status (ACIA + Serial ULA registers, endpoint)."""
        request = serial_pb2.GetSerialStatusRequest()
        response = self._stub.GetSerialStatus(request)
        return _response_to_status(response)

    def set_endpoint_mode(self, mode: EndpointMode) -> None:
        """Select the host transport endpoint attached to the serial port.

        Args:
            mode: One of ``EndpointMode.NONE``, ``LOOPBACK``, or ``SCRIPTABLE``.

        Raises:
            SerialError: If the machine has no serial socket.
        """
        request = serial_pb2.SetEndpointModeRequest(mode=int(mode))
        response = self._stub.SetEndpointMode(request)
        if not response.success:
            raise SerialError(response.error or "SetEndpointMode failed")

    def send_to_device(self, data: bytes) -> int:
        """Queue bytes for the BBC to receive (device -> Beeb).

        Requires the endpoint to be in ``SCRIPTABLE`` mode.

        Args:
            data: Raw bytes to deliver to the BBC's serial receiver.

        Returns:
            The number of bytes queued.

        Raises:
            SerialError: If the endpoint is not in scriptable mode.
        """
        request = serial_pb2.SendToDeviceRequest(data=bytes(data))
        response = self._stub.SendToDevice(request)
        if not response.success:
            raise SerialError(response.error or "SendToDevice failed")
        return response.queued

    def receive_from_device(self, max_bytes: int = 0) -> bytes:
        """Collect bytes the BBC has transmitted (Beeb -> device).

        Requires the endpoint to be in ``SCRIPTABLE`` mode.

        Args:
            max_bytes: Maximum bytes to collect (0 = all currently available).

        Returns:
            The bytes the BBC transmitted (may be empty).

        Raises:
            SerialError: If the endpoint is not in scriptable mode.
        """
        request = serial_pb2.ReceiveFromDeviceRequest(max_bytes=max_bytes)
        response = self._stub.ReceiveFromDevice(request)
        if not response.success:
            raise SerialError(response.error or "ReceiveFromDevice failed")
        return response.data
