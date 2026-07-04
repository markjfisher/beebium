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

"""Client for the host-serial extension's typed config API.

The host-serial extension bridges the BBC serial port to a host PTY or serial
device. This client queries and re-points that bridge (mode / path / baud)
programmatically -- the scripting-friendly equivalent of the GUI panel, without
needing the declarative ExtensionUi control tree. Requires the server to be
launched with ``--host-serial``.
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium._proto import host_serial_pb2
from beebium.extension import ExtensionAdapter

# The logical service name the host-serial extension's dispatcher registers
# (matches HostSerialDispatcher::service_name() in the extension).
_SERVICE = "HostSerial"


@dataclass(frozen=True)
class HostSerialConfig:
    """The host-serial bridge configuration and open state."""

    mode: str  # "pty" or "device"
    path: str  # pty slave path, or the opened device path
    baud: int  # host line speed (device mode; informational for pty)
    serial_open: bool  # is the host port currently open?
    open_error: str  # OS error text when serial_open is False


class HostSerial(ExtensionAdapter):
    """Query and re-point the host-serial bridge.

    The HostSerial messages are tunnelled over the core's ExtensionRpc channel;
    the host-serial extension no longer hosts its own gRPC service.

    Usage:
        host_serial = bbc.extensions[HostSerial]     # or HostSerial.attach(bbc)
        host_serial.set_config(mode="device", path="/dev/ttyUSB0", baud=9600)
    """

    EXTENSION_NAME = "host-serial"

    def get_config(self) -> HostSerialConfig:
        """Read the current bridge configuration and open state."""
        request = host_serial_pb2.HostSerialGetConfigRequest()
        reply = self._invoke_bytes(_SERVICE, "GetConfig", request.SerializeToString())
        response = host_serial_pb2.HostSerialConfig()
        response.ParseFromString(reply)
        return _config_from_proto(response)

    def set_config(
        self,
        *,
        mode: str | None = None,
        path: str | None = None,
        baud: int | None = None,
    ) -> HostSerialConfig:
        """Re-point the bridge. A partial update: only the arguments you pass
        change; the rest are kept. ``mode`` may only be ``"device"`` at runtime
        (a pty can only be created at startup). The re-point applies on the next
        emulation tick, so the returned config may still show the previous device
        until it lands -- call :meth:`get_config` to confirm. Never blocks.
        """
        request = host_serial_pb2.HostSerialSetConfigRequest()
        if mode is not None:
            request.mode = mode
        if path is not None:
            request.path = path
        if baud is not None:
            request.baud = baud
        reply = self._invoke_bytes(_SERVICE, "SetConfig", request.SerializeToString())
        response = host_serial_pb2.HostSerialConfig()
        response.ParseFromString(reply)
        return _config_from_proto(response)


def _config_from_proto(response: host_serial_pb2.HostSerialConfig) -> HostSerialConfig:
    return HostSerialConfig(
        mode=response.mode,
        path=response.path,
        baud=response.baud,
        serial_open=response.serial_open,
        open_error=response.open_error,
    )
