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

"""Econet transport discovery (which transport is active on the server).

Wraps EconetTransportService. Use this to decide whether to drive
``bbc.aun`` or ``bbc.piconet`` (or any future transport-specific
service): each transport extension has a canonical name (``aun``,
``piconet``, ...) which maps one-to-one with the corresponding
service stub.
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium._proto import econet_transport_pb2, econet_transport_pb2_grpc


@dataclass(frozen=True)
class TransportInfo:
    """A single Econet transport extension known to the server."""

    # Canonical extension name -- "aun", "piconet", etc. Maps to the
    # transport-specific gRPC service the client should use.
    name: str

    # Human-readable description from the extension manifest.
    description: str

    # True if this transport is the one currently producing the
    # backend behind EconetSocket on the server.
    active: bool

    # Opaque, server-assigned instance id. This is the key to pass to
    # ExtensionUiService (SubscribeView / Dispatch) to drive this
    # transport's control panel; typically a UUID, with no relationship
    # to ``name``. Discover it here rather than hardcoding names.
    id: str

    # True if this transport implements an Extension UI, so a frontend
    # can decide whether to render an ExtensionUiService panel for it.
    has_ui: bool


class EconetTransport:
    """Discover which Econet transport extension is active.

    Usage:
        if bbc.transport.active is None:
            print("No Econet transport configured")
        elif bbc.transport.active.name == "aun":
            bbc.aun.add_peer(...)
        elif bbc.transport.active.name == "piconet":
            print(bbc.piconet.status.device_path)
    """

    def __init__(self, stub: econet_transport_pb2_grpc.EconetTransportServiceStub):
        self._stub = stub

    def list(self) -> list[TransportInfo]:
        """List all econet transports the server knows about.

        Returns one entry per loaded transport extension. The
        ``active`` field is True for whichever one is currently
        producing the wire backend; for BBC machine variants at most
        one will be active.
        """
        request = econet_transport_pb2.ListTransportsRequest()
        response = self._stub.ListTransports(request)
        return [
            TransportInfo(
                name=t.name,
                description=t.description,
                active=t.active,
                id=t.id,
                has_ui=t.has_ui,
            )
            for t in response.transports
        ]

    @property
    def active(self) -> TransportInfo | None:
        """The single active transport, or None if none configured."""
        request = econet_transport_pb2.GetActiveTransportRequest()
        response = self._stub.GetActiveTransport(request)
        if not response.HasField("active"):
            return None
        a = response.active
        return TransportInfo(
            name=a.name,
            description=a.description,
            active=a.active,
            id=a.id,
            has_ui=a.has_ui,
        )
