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

"""Econet/AUN networking management for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass

from beebium._proto import econet_pb2, econet_pb2_grpc
from beebium.exceptions import EconetError


@dataclass(frozen=True)
class AdlcStatus:
    """MC6854 ADLC register state."""

    cr1: int
    cr2: int
    cr3: int
    cr4: int
    sr1: int
    sr2: int
    irq_output: bool
    tx_fifo_empty: bool
    tx_fifo_full: bool
    rx_fifo_empty: bool
    rx_fifo_full: bool
    tx_frame_field: str
    rx_frame_field: str
    pse_level: int
    cts_input: bool


@dataclass(frozen=True)
class HandshakeStatus:
    """Four-way handshake state."""

    stage: str
    flag_fill_active: bool


@dataclass(frozen=True)
class EconetStatus:
    """Econet hardware status."""

    has_econet_socket: bool
    enabled: bool
    station_id: int
    aun_mode: bool
    connected: bool
    aun_port: int
    peer_count: int
    adlc: AdlcStatus | None
    handshake: HandshakeStatus | None


@dataclass(frozen=True)
class PeerInfo:
    """AUN peer mapping."""

    net: int
    stn: int
    ip_address: str
    port: int


class Econet:
    """Econet/AUN networking management.

    Provides access to Econet hardware configuration, status queries,
    and AUN peer management.

    Usage:
        # Enable Econet with station ID 254
        port = bbc.econet.enable(station_id=254)
        print(f"AUN port: {port}")

        # Add a peer
        bbc.econet.add_peer(net=0, stn=1, ip_address="192.168.1.100")

        # Check status
        status = bbc.econet.status
        print(f"Station {status.station_id}, peers: {status.peer_count}")

        # Disable
        bbc.econet.disable()
    """

    def __init__(self, stub: econet_pb2_grpc.EconetServiceStub):
        """Create an Econet interface.

        Args:
            stub: The gRPC stub for the EconetService.
        """
        self._stub = stub

    @property
    def status(self) -> EconetStatus:
        """Get Econet hardware status."""
        request = econet_pb2.GetEconetStatusRequest()
        response = self._stub.GetEconetStatus(request)

        adlc = None
        if response.HasField("adlc"):
            a = response.adlc
            adlc = AdlcStatus(
                cr1=a.cr1,
                cr2=a.cr2,
                cr3=a.cr3,
                cr4=a.cr4,
                sr1=a.sr1,
                sr2=a.sr2,
                irq_output=a.irq_output,
                tx_fifo_empty=a.tx_fifo_empty,
                tx_fifo_full=a.tx_fifo_full,
                rx_fifo_empty=a.rx_fifo_empty,
                rx_fifo_full=a.rx_fifo_full,
                tx_frame_field=a.tx_frame_field,
                rx_frame_field=a.rx_frame_field,
                pse_level=a.pse_level,
                cts_input=a.cts_input,
            )

        handshake = None
        if response.HasField("handshake"):
            h = response.handshake
            handshake = HandshakeStatus(
                stage=h.stage,
                flag_fill_active=h.flag_fill_active,
            )

        return EconetStatus(
            has_econet_socket=response.has_econet_socket,
            enabled=response.enabled,
            station_id=response.station_id,
            aun_mode=response.aun_mode,
            connected=response.connected,
            aun_port=response.aun_port,
            peer_count=response.peer_count,
            adlc=adlc,
            handshake=handshake,
        )

    @property
    def is_enabled(self) -> bool:
        """True if Econet hardware is currently fitted."""
        return self.status.enabled

    @property
    def station_id(self) -> int:
        """Station number (1-254, 0 if disabled)."""
        return self.status.station_id

    @property
    def peers(self) -> list[PeerInfo]:
        """List all configured AUN peers."""
        request = econet_pb2.ListPeersRequest()
        response = self._stub.ListPeers(request)
        return [
            PeerInfo(
                net=peer.net,
                stn=peer.stn,
                ip_address=peer.ip_address,
                port=peer.port,
            )
            for peer in response.peers
        ]

    def enable(
        self,
        station_id: int,
        *,
        aun_port: int = 0,
        no_network: bool = False,
    ) -> int:
        """Fit Econet hardware and optionally bind AUN socket.

        Args:
            station_id: Station number (1-254).
            aun_port: UDP port to bind (0 = use default 32768).
            no_network: If True, fit hardware with no network connection.

        Returns:
            Actual AUN port bound (useful when 0/default was requested).

        Raises:
            EconetError: If the operation fails.
        """
        request = econet_pb2.EnableEconetRequest(
            station_id=station_id,
            aun_port=aun_port,
            no_network=no_network,
        )
        response = self._stub.EnableEconet(request)
        if not response.success:
            raise EconetError(response.error)
        return response.actual_aun_port

    def set_station_id(self, station_id: int) -> None:
        """Set the station number (takes effect on next machine reset).

        On the Model B this is equivalent to changing the 8 address links.
        The NFS ROM re-reads the station number on Ctrl-Break.

        Args:
            station_id: Station number (1-254).

        Raises:
            EconetError: If the operation fails.
        """
        request = econet_pb2.SetStationIdRequest(station_id=station_id)
        response = self._stub.SetStationId(request)
        if not response.success:
            raise EconetError(response.error)

    def set_connected(self, connected: bool) -> None:
        """Connect or disconnect the network cable.

        Simulates plugging/unplugging the Econet cable. Takes effect
        immediately — DCD and CTS update on the next ADLC tick.

        Args:
            connected: True to connect, False to disconnect.

        Raises:
            EconetError: If the operation fails (e.g. Econet not enabled,
                or not in AUN mode).
        """
        request = econet_pb2.SetConnectedRequest(connected=connected)
        response = self._stub.SetConnected(request)
        if not response.success:
            raise EconetError(response.error)

    def disable(self) -> None:
        """Remove Econet hardware (disable station, close AUN socket).

        Raises:
            EconetError: If the operation fails.
        """
        request = econet_pb2.DisableEconetRequest()
        response = self._stub.DisableEconet(request)
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
            EconetError: If the operation fails.
        """
        request = econet_pb2.AddPeerRequest(
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

        Args:
            net: Econet network number (0-127).
            stn: Econet station number (1-254).

        Raises:
            EconetError: If the operation fails.
        """
        request = econet_pb2.RemovePeerRequest(net=net, stn=stn)
        response = self._stub.RemovePeer(request)
        if not response.success:
            raise EconetError(response.error)
