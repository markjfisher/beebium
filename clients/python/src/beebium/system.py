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
from typing import TYPE_CHECKING

from beebium._proto import system_pb2, system_pb2_grpc

if TYPE_CHECKING:
    from typing import Optional


class ServerStatus(Enum):
    """Server status types."""

    READY = "ready"
    SHUTTING_DOWN = "shutting_down"
    IDENTITY_CHANGED = "identity_changed"


@dataclass(frozen=True)
class Provenance:
    """Launch provenance information.

    Identifies who/what launched the emulator core and when.
    """

    type: str  # e.g., "python-client", "macos-gui", "terminal"
    instance_uuid: str  # RFC 4122 UUID
    version: str  # Version of the launching client
    timestamp: int  # Unix timestamp (seconds since epoch)


class MachineIdentity:
    """Machine identity - UUID and mutable name.

    The `name` property can be read and written. Writing triggers
    a gRPC call to update the server-side name.

    Attributes:
        uuid: RFC 4122 v4 UUID, stable for machine lifetime (read-only)
        name: User-assignable label (read/write)
        model_type: e.g., "ModelB" (read-only)
        model_name: e.g., "BBC Model B" (read-only)
        system: Reference to the parent System object (read-only)
    """

    def __init__(
        self,
        uuid: str,
        name: str,
        model_type: str,
        model_name: str,
        system: "System",
    ):
        self._uuid = uuid
        self._name = name
        self._model_type = model_type
        self._model_name = model_name
        self._system = system

    @property
    def uuid(self) -> str:
        """RFC 4122 v4 UUID, stable for machine lifetime."""
        return self._uuid

    @property
    def name(self) -> str:
        """User-assignable machine label."""
        return self._name

    @name.setter
    def name(self, value: str) -> None:
        """Set machine name via gRPC."""
        request = system_pb2.SetMachineNameRequest(name=value)
        response = self._system._stub.SetMachineName(request)
        self._name = response.identity.name

    @property
    def model_type(self) -> str:
        """Machine model type identifier (immutable)."""
        return self._model_type

    @property
    def model_name(self) -> str:
        """Human-readable model name (immutable)."""
        return self._model_name

    @property
    def system(self) -> "System":
        """Reference to the parent System object."""
        return self._system

    def __repr__(self) -> str:
        return (
            f"MachineIdentity(uuid={self._uuid!r}, name={self._name!r}, "
            f"model_type={self._model_type!r}, model_name={self._model_name!r})"
        )

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, MachineIdentity):
            return NotImplemented
        return (
            self._uuid == other._uuid
            and self._name == other._name
            and self._model_type == other._model_type
            and self._model_name == other._model_name
        )


@dataclass(frozen=True)
class ServerStatusEvent:
    """Server status change event."""

    status: ServerStatus
    message: str
    shutdown_grace_ms: int  # Grace period for SHUTTING_DOWN
    identity: "Optional[MachineIdentity]" = None  # For IDENTITY_CHANGED events


class System:
    """System information and server status.

    Provides access to machine identification and server lifecycle events.

    Usage:
        # Get machine identity
        identity = bbc.system.identity
        print(f"Machine: {identity.model_name}")
        print(f"Name: {identity.name}")

        # Change machine name (triggers gRPC call)
        identity.name = "My BBC Micro"

        # Watch for status changes
        for event in bbc.system.watch_status():
            if event.status == ServerStatus.SHUTTING_DOWN:
                print(f"Server shutting down in {event.shutdown_grace_ms}ms")
                break
            elif event.status == ServerStatus.IDENTITY_CHANGED:
                print(f"Machine renamed to: {event.identity.name}")
    """

    def __init__(self, stub: system_pb2_grpc.SystemServiceStub):
        """Create a System interface.

        Args:
            stub: The gRPC stub for the SystemService.
        """
        self._stub = stub
        self._identity_cache: MachineIdentity | None = None
        self._provenance_cache: Provenance | None = None

    @property
    def identity(self) -> MachineIdentity:
        """Get machine identity.

        Returns a MachineIdentity object whose `name` property
        can be read or written (writing updates the server).
        """
        if self._identity_cache is None:
            request = system_pb2.GetSystemInfoRequest()
            response = self._stub.GetSystemInfo(request)
            self._identity_cache = MachineIdentity(
                uuid=response.identity.uuid,
                name=response.identity.name,
                model_type=response.identity.model_type,
                model_name=response.identity.model_name,
                system=self,
            )
        return self._identity_cache

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
                identity = None
            elif response.status == system_pb2.SERVER_STATUS_SHUTTING_DOWN:
                status = ServerStatus.SHUTTING_DOWN
                identity = None
            elif response.status == system_pb2.SERVER_STATUS_IDENTITY_CHANGED:
                status = ServerStatus.IDENTITY_CHANGED
                # Create MachineIdentity from the response
                identity = MachineIdentity(
                    uuid=response.identity.uuid,
                    name=response.identity.name,
                    model_type=response.identity.model_type,
                    model_name=response.identity.model_name,
                    system=self,
                )
                # Update cache
                self._identity_cache = identity
            else:
                # Unknown status, treat as ready
                status = ServerStatus.READY
                identity = None

            yield ServerStatusEvent(
                status=status,
                message=response.message,
                shutdown_grace_ms=response.shutdown_grace_ms,
                identity=identity,
            )

    @property
    def client_count(self) -> int:
        """Number of clients with active WatchServerStatus streams.

        This count reflects how many clients are currently watching
        the server status. It does not include clients that are merely
        connected but not watching.
        """
        request = system_pb2.GetSystemInfoRequest()
        response = self._stub.GetSystemInfo(request)
        return response.connections.client_count

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
