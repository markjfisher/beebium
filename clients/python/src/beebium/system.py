# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

"""System information and server status for the beebium client."""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass
from enum import Enum

from beebium._proto import system_pb2, system_pb2_grpc


class ServerStatus(Enum):
    """Server status types."""

    READY = "ready"
    SHUTTING_DOWN = "shutting_down"


@dataclass(frozen=True)
class SystemInfo:
    """Machine identification information."""

    machine_type: str  # "ModelB", "ModelBPlus"
    machine_display_name: str  # "BBC Model B+ 64K"


@dataclass(frozen=True)
class Provenance:
    """Launch provenance information.

    Identifies who/what launched the emulator core and when.
    """

    type: str  # e.g., "python-client", "macos-gui", "terminal"
    instance_uuid: str  # RFC 4122 UUID
    version: str  # Version of the launching client
    timestamp: int  # Unix timestamp (seconds since epoch)


@dataclass(frozen=True)
class ServerStatusEvent:
    """Server status change event."""

    status: ServerStatus
    message: str
    shutdown_grace_ms: int  # Grace period for SHUTTING_DOWN


class System:
    """System information and server status.

    Provides access to machine identification and server lifecycle events.

    Usage:
        # Get machine info
        info = bbc.system.info
        print(f"Machine: {info.machine_display_name}")

        # Shortcut properties
        print(f"Type: {bbc.system.machine_type}")

        # Watch for shutdown
        for event in bbc.system.watch_status():
            if event.status == ServerStatus.SHUTTING_DOWN:
                print(f"Server shutting down in {event.shutdown_grace_ms}ms")
                break
    """

    def __init__(self, stub: system_pb2_grpc.SystemServiceStub):
        """Create a System interface.

        Args:
            stub: The gRPC stub for the SystemService.
        """
        self._stub = stub
        self._info_cache: SystemInfo | None = None
        self._provenance_cache: Provenance | None = None

    @property
    def info(self) -> SystemInfo:
        """Get system/machine information (cached)."""
        if self._info_cache is None:
            request = system_pb2.GetSystemInfoRequest()
            response = self._stub.GetSystemInfo(request)
            self._info_cache = SystemInfo(
                machine_type=response.machine_type,
                machine_display_name=response.machine_display_name,
            )
        return self._info_cache

    @property
    def machine_type(self) -> str:
        """Machine type identifier (e.g., "ModelB", "ModelBPlus")."""
        return self.info.machine_type

    @property
    def machine_display_name(self) -> str:
        """Human-readable machine name (e.g., "BBC Model B+ 64K")."""
        return self.info.machine_display_name

    @property
    def provenance(self) -> Provenance:
        """Get launch provenance information (cached).

        Returns information about who/what launched this server instance
        and when it was launched.
        """
        if self._provenance_cache is None:
            request = system_pb2.GetSystemInfoRequest()
            response = self._stub.GetSystemInfo(request)
            prov = response.provenance
            self._provenance_cache = Provenance(
                type=prov.type,
                instance_uuid=prov.instance_uuid,
                version=prov.version,
                timestamp=prov.timestamp,
            )
        return self._provenance_cache

    def watch_status(self) -> Iterator[ServerStatusEvent]:
        """Stream server status events.

        Server sends READY immediately upon subscription, then status changes.
        Stream ends when server shuts down or client disconnects.

        Yields:
            ServerStatusEvent for each status change.
        """
        request = system_pb2.WatchServerStatusRequest()
        for response in self._stub.WatchServerStatus(request):
            if response.status == system_pb2.SERVER_STATUS_READY:
                status = ServerStatus.READY
            elif response.status == system_pb2.SERVER_STATUS_SHUTTING_DOWN:
                status = ServerStatus.SHUTTING_DOWN
            else:
                # Unknown status, treat as ready
                status = ServerStatus.READY

            yield ServerStatusEvent(
                status=status,
                message=response.message,
                shutdown_grace_ms=response.shutdown_grace_ms,
            )

    def wait_for_ready(self, timeout: float = 5.0) -> bool:
        """Block until server sends READY status.

        Args:
            timeout: Maximum time to wait in seconds.

        Returns:
            True if server is ready, False on timeout.
        """
        import time

        deadline = time.monotonic() + timeout

        for event in self.watch_status():
            if event.status == ServerStatus.READY:
                return True
            if time.monotonic() >= deadline:
                return False

        return False
