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

"""Client adapter for the acorn-scsi extension (SCSI host adapter).

Inspects the SCSI bus: enumerate targets, read the current bus phase/status,
and stream bus events. Available when the server is launched with
``--acorn-scsi``.
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass

from beebium.ext.peripheral.acorn_scsi._proto import scsi_host_adapter_pb2 as scsi_pb2
from beebium.client.extension import PeripheralExtensionAdapter

# The logical service name the acorn-scsi dispatcher registers
# (matches ScsiHostAdapterDispatcher::service_name()).
_SERVICE = "ScsiHostAdapterService"


@dataclass(frozen=True)
class ScsiTarget:
    """A device on the SCSI bus."""

    id: int  # SCSI ID 0-7
    present: bool
    device_type: str  # "hard-disc", "test-device", "vp415", ...
    description: str


@dataclass(frozen=True)
class ScsiBusStatus:
    """The current SCSI bus phase and status register."""

    phase: str  # "BUS_FREE", "COMMAND", "DATA_IN", ...
    selected_target: int  # 0xFF if none selected
    status_register: int  # raw status register byte
    irq_pending: bool


class AcornScsi(PeripheralExtensionAdapter):
    """SCSI host adapter inspection (acorn-scsi extension).

    Usage:
        scsi = bbc.extensions[AcornScsi]     # or AcornScsi.attach(bbc)
        for target in scsi.targets:
            print(target.id, target.device_type)
    """

    EXTENSION_NAME = "acorn-scsi"

    @property
    def targets(self) -> list[ScsiTarget]:
        """Enumerate the SCSI targets on the bus."""
        reply = self._invoke_bytes(
            _SERVICE,
            "ListTargets",
            scsi_pb2.ListScsiTargetsRequest().SerializeToString(),
        )
        response = scsi_pb2.ListScsiTargetsResponse()
        response.ParseFromString(reply)
        return [
            ScsiTarget(
                id=t.id,
                present=t.present,
                device_type=t.device_type,
                description=t.description,
            )
            for t in response.targets
        ]

    @property
    def bus_status(self) -> ScsiBusStatus:
        """Read the current bus phase and status register."""
        reply = self._invoke_bytes(
            _SERVICE,
            "GetBusStatus",
            scsi_pb2.GetScsiBusStatusRequest().SerializeToString(),
        )
        response = scsi_pb2.GetScsiBusStatusResponse()
        response.ParseFromString(reply)
        return ScsiBusStatus(
            phase=response.phase,
            selected_target=response.selected_target,
            status_register=response.status_register,
            irq_pending=response.irq_pending,
        )

    def watch_bus_events(
        self, *, include_register_access: bool = False
    ) -> Iterator[scsi_pb2.ScsiBusEvent]:
        """Stream SCSI bus events.

        Yields the raw ``ScsiBusEvent`` protobuf messages -- the event is a
        oneof over several phase/command/data/status/selection sub-messages, so
        this minimal adapter surfaces the message directly rather than a typed
        wrapper. Set ``include_register_access`` for verbose register-level
        events.
        """
        request = scsi_pb2.WatchScsiBusEventsRequest(
            include_register_access=include_register_access
        )
        for reply in self._server_stream_bytes(
            _SERVICE, "WatchBusEvents", request.SerializeToString()
        ):
            event = scsi_pb2.ScsiBusEvent()
            event.ParseFromString(reply)
            yield event
