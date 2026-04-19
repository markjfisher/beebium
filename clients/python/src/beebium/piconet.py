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

"""Piconet (USB-CDC Econet bridge) transport-specific operations.

These RPCs are surfaced by PiconetService when Piconet is the active
Econet transport on the server. The current scope is intentionally
minimal -- just enough for a user to confirm "yes, my Piconet is
plugged in, and it's on /dev/X". Future Piconet RPCs (firmware
version, mode, counters, live clock detection via PollHardware,
SetMode, monitor-frame streaming) are documented in
src/extensions/piconet/piconet_service.proto.
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium._proto import piconet_service_pb2, piconet_service_pb2_grpc


@dataclass(frozen=True)
class PiconetStatus:
    """Piconet adapter status."""

    # Serial device path the Piconet is configured to use, e.g.
    # "/dev/tty.usbmodem101". Set at startup from the
    # --piconet device_path=... CLI argument or preset value.
    device_path: str

    # True if the SerialPort is currently open. On POSIX this is the
    # file descriptor's validity; if the user yanks the USB cable
    # mid-run this transitions to False. False at startup means the
    # device couldn't be opened.
    #
    # This says nothing about whether a real Econet wire is connected
    # to the Piconet adapter; only whether the host-side USB CDC
    # endpoint is responsive.
    serial_open: bool


class Piconet:
    """Piconet-specific RPCs.

    Available on the server's gRPC surface only when Piconet is the
    active Econet transport. Check ``bbc.transport.active`` first if
    your code might run against a server configured for AUN or no
    transport.

    Usage:
        if bbc.transport.active and bbc.transport.active.name == "piconet":
            status = bbc.piconet.status
            print(f"Piconet on {status.device_path} (open={status.serial_open})")
    """

    def __init__(self, stub: piconet_service_pb2_grpc.PiconetServiceStub):
        self._stub = stub

    @property
    def status(self) -> PiconetStatus:
        """Read the Piconet adapter status (device path + serial open)."""
        request = piconet_service_pb2.PiconetGetStatusRequest()
        response = self._stub.GetStatus(request)
        return PiconetStatus(
            device_path=response.device_path,
            serial_open=response.serial_open,
        )
