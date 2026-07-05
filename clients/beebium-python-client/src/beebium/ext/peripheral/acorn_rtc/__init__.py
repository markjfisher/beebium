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

"""Client adapter for the acorn-rtc extension (real-time clock).

Reads and sets the emulated date/time, inspects the raw BCD registers, and
streams CBUS activity. Available when the server is launched with
``--acorn-rtc``.
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass
from enum import IntEnum

from beebium.client.extension import PeripheralExtensionAdapter
from beebium.ext.peripheral.acorn_rtc._proto import acorn_rtc_pb2

# The logical service name the acorn-rtc dispatcher registers
# (matches AcornRtcDispatcher::service_name()).
_SERVICE = "AcornRtcService"


@dataclass(frozen=True)
class RtcTime:
    """The decoded emulated date and time."""

    year: int
    month: int  # 1-12
    day: int  # 1-31
    hour: int  # 0-23
    minute: int  # 0-59
    iso8601: str  # e.g. "1985-06-15T14:30"


class RtcEventType(IntEnum):
    """Kind of CBUS activity on the RTC chip."""

    REGISTER_READ = acorn_rtc_pb2.RtcActivityEvent.REGISTER_READ
    REGISTER_WRITE = acorn_rtc_pb2.RtcActivityEvent.REGISTER_WRITE


@dataclass(frozen=True)
class RtcActivityEvent:
    """A single register read or write by the BBC."""

    type: RtcEventType
    register_number: int
    value: int  # BCD value read or written
    timestamp_us: int


class AcornRtc(PeripheralExtensionAdapter):
    """Real-time clock (acorn-rtc extension).

    Usage:
        rtc = bbc.extensions[AcornRtc]       # or AcornRtc.attach(bbc)
        rtc.set_time("1985-06-15T14:30")
        print(rtc.get_time().iso8601)
    """

    EXTENSION_NAME = "acorn-rtc"

    def get_time(self) -> RtcTime:
        """Read the current emulated date and time."""
        reply = self._invoke_bytes(
            _SERVICE, "GetTime", acorn_rtc_pb2.GetRtcTimeRequest().SerializeToString()
        )
        response = acorn_rtc_pb2.GetRtcTimeResponse()
        response.ParseFromString(reply)
        return RtcTime(
            year=response.year,
            month=response.month,
            day=response.day,
            hour=response.hour,
            minute=response.minute,
            iso8601=response.iso8601,
        )

    def set_time(self, iso8601: str) -> None:
        """Set the emulated date and time from an ISO-8601 string.

        Raises a gRPC error if the year is outside the representable range
        (1981-2000).
        """
        request = acorn_rtc_pb2.SetRtcTimeRequest(iso8601=iso8601)
        self._invoke_bytes(_SERVICE, "SetTime", request.SerializeToString())

    @property
    def registers(self) -> list[int]:
        """The 8 raw BCD register values."""
        reply = self._invoke_bytes(
            _SERVICE,
            "GetRegisters",
            acorn_rtc_pb2.GetRtcRegistersRequest().SerializeToString(),
        )
        response = acorn_rtc_pb2.GetRtcRegistersResponse()
        response.ParseFromString(reply)
        return list(response.registers)

    def set_register(self, index: int, bcd_value: int) -> None:
        """Write a single register (0-7) with a raw BCD byte."""
        request = acorn_rtc_pb2.SetRtcRegisterRequest(
            register_index=index, bcd_value=bcd_value
        )
        self._invoke_bytes(_SERVICE, "SetRegister", request.SerializeToString())

    def watch_activity(self) -> Iterator[RtcActivityEvent]:
        """Stream CBUS register reads and writes as the BBC accesses the chip."""
        request = acorn_rtc_pb2.WatchActivityRequest()
        for reply in self._server_stream_bytes(
            _SERVICE, "WatchActivity", request.SerializeToString()
        ):
            event = acorn_rtc_pb2.RtcActivityEvent()
            event.ParseFromString(reply)
            yield RtcActivityEvent(
                type=RtcEventType(event.type),
                register_number=event.register_number,
                value=event.value,
                timestamp_us=event.timestamp_us,
            )
