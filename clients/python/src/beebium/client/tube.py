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

"""Tube coprocessor management for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client._proto import tube_pb2, tube_pb2_grpc


@dataclass(frozen=True)
class TubeStatus:
    """Current Tube coprocessor status."""

    has_tube_socket: bool
    enabled: bool
    parasite_connected: bool
    parasite_type: str
    parasite_clock_hz: int
    shared_memory_name: str
    parasite_grpc_address: str


class Tube:
    """Tube coprocessor management.

    Provides access to Tube status queries.

    Usage:
        # Check Tube status
        status = bbc.tube.status
        print(f"Enabled: {status.enabled}, Parasite: {status.parasite_type}")

        # Quick checks
        if bbc.tube.is_enabled:
            print(f"Parasite at: {bbc.tube.parasite_grpc_address}")
    """

    def __init__(self, stub: tube_pb2_grpc.TubeServiceStub):
        """Create a Tube interface.

        Args:
            stub: The gRPC stub for the TubeService.
        """
        self._stub = stub

    @property
    def status(self) -> TubeStatus:
        """Get current Tube status."""
        request = tube_pb2.GetTubeStatusRequest()
        response = self._stub.GetStatus(request)
        return TubeStatus(
            has_tube_socket=response.has_tube_socket,
            enabled=response.enabled,
            parasite_connected=response.parasite_connected,
            parasite_type=response.parasite_type,
            parasite_clock_hz=response.parasite_clock_hz,
            shared_memory_name=response.shared_memory_name,
            parasite_grpc_address=response.parasite_grpc_address,
        )

    @property
    def is_enabled(self) -> bool:
        """True if Tube hardware is currently enabled."""
        return self.status.enabled

    @property
    def parasite_connected(self) -> bool:
        """True if a parasite process is connected."""
        return self.status.parasite_connected

    @property
    def parasite_grpc_address(self) -> str:
        """Parasite's gRPC address (empty if not registered)."""
        return self.status.parasite_grpc_address
