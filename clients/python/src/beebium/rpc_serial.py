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

"""Client for the rpc-serial extension.

The rpc-serial extension makes the RPC client the device on the far end of the
BBC's serial wire: ``send`` injects bytes for the BBC to receive, ``receive``
collects bytes the BBC has transmitted. Requires the server to be launched with
``--rpc-serial`` (so the extension owns the serial port).
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium._proto import rpc_serial_pb2, rpc_serial_pb2_grpc


@dataclass(frozen=True)
class RpcSerialStatus:
    """Pending byte counts for the rpc-serial peer."""

    tx_pending: int  # bytes the BBC sent, awaiting receive()
    rx_pending: int  # bytes queued (via send()) to deliver to the BBC


class RpcSerial:
    """Drive the client-driven serial peer provided by the rpc-serial extension."""

    def __init__(self, stub: rpc_serial_pb2_grpc.RpcSerialStub):
        self._stub = stub

    def send(self, data: bytes) -> int:
        """Inject bytes for the BBC to receive.

        Returns the number of bytes accepted, which is fewer than ``len(data)``
        when the receive queue is full; resend ``data[accepted:]`` after the BBC
        has read some. Never blocks.
        """
        response = self._stub.Send(rpc_serial_pb2.RpcSerialSendRequest(data=data))
        return response.accepted

    def receive(self, max_bytes: int = 0) -> bytes:
        """Collect bytes the BBC has transmitted (0 = all currently available)."""
        response = self._stub.Receive(
            rpc_serial_pb2.RpcSerialReceiveRequest(max_bytes=max_bytes)
        )
        return response.data

    @property
    def status(self) -> RpcSerialStatus:
        """Pending byte counts in each direction."""
        response = self._stub.GetStatus(rpc_serial_pb2.RpcSerialStatusRequest())
        return RpcSerialStatus(
            tx_pending=response.tx_pending, rx_pending=response.rx_pending
        )
