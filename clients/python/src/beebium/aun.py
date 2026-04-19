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

"""AUN (Acorn Universal Networking) transport-specific operations.

These RPCs are surfaced by AunService when AUN is the active Econet
transport on the server. Use bbc.transport.active to confirm AUN is
the active transport before calling these methods; otherwise the RPC
returns an error indicating "AUN backend is not active".
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium._proto import aun_pb2, aun_pb2_grpc
from beebium.exceptions import EconetError


@dataclass(frozen=True)
class AunStatus:
    """AUN-specific transport status."""

    connected: bool
    local_port: int
    peer_count: int


@dataclass(frozen=True)
class PeerInfo:
    """An AUN peer mapping."""

    net: int
    stn: int
    ip_address: str
    port: int


class Aun:
    """AUN-specific RPCs (peer table, cable plug, port status).

    Available on the server's gRPC surface only when AUN is the active
    Econet transport. Check ``bbc.transport.active`` first if your code
    might run against a server configured for Piconet or no transport.

    Usage:
        if bbc.transport.active and bbc.transport.active.name == "aun":
            bbc.aun.add_peer(net=0, stn=254, ip_address="192.168.1.10")
            print(bbc.aun.status)
    """

    def __init__(self, stub: aun_pb2_grpc.AunServiceStub):
        self._stub = stub

    @property
    def status(self) -> AunStatus:
        """Read the AUN backend status."""
        request = aun_pb2.AunGetStatusRequest()
        response = self._stub.GetStatus(request)
        return AunStatus(
            connected=response.connected,
            local_port=response.local_port,
            peer_count=response.peer_count,
        )

    @property
    def peers(self) -> list[PeerInfo]:
        """Enumerate all configured AUN peers."""
        request = aun_pb2.AunListPeersRequest()
        response = self._stub.ListPeers(request)
        return [
            PeerInfo(
                net=p.net,
                stn=p.stn,
                ip_address=p.ip_address,
                port=p.port,
            )
            for p in response.peers
        ]

    def set_connected(self, connected: bool) -> None:
        """Plug or unplug the simulated network cable.

        While disconnected the ADLC sees DCD high (no carrier).

        Raises:
            EconetError: If the AUN backend is not active or the call fails.
        """
        request = aun_pb2.AunSetConnectedRequest(connected=connected)
        response = self._stub.SetConnected(request)
        if not response.success:
            raise EconetError(response.error)

    def add_peer(
        self,
        net: int,
        stn: int,
        ip_address: str,
        port: int = 0,
    ) -> None:
        """Add an Econet address to UDP endpoint peer mapping.

        Args:
            net: Econet network number (0-127).
            stn: Econet station number (1-254).
            ip_address: Dotted-quad IP address.
            port: UDP port (0 = use AUN default 32768).

        Raises:
            EconetError: If the call fails.
        """
        request = aun_pb2.AunAddPeerRequest(
            net=net,
            stn=stn,
            ip_address=ip_address,
            port=port,
        )
        response = self._stub.AddPeer(request)
        if not response.success:
            raise EconetError(response.error)

    def remove_peer(self, net: int, stn: int) -> None:
        """Remove a peer mapping by Econet address.

        Raises:
            EconetError: If the call fails.
        """
        request = aun_pb2.AunRemovePeerRequest(net=net, stn=stn)
        response = self._stub.RemovePeer(request)
        if not response.success:
            raise EconetError(response.error)
