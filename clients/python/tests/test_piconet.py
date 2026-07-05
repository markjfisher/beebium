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

"""Unit tests for the Piconet client wrapper around PiconetService."""

from __future__ import annotations

from unittest.mock import MagicMock

import pytest

from beebium.ext.econet.piconet import Piconet, PiconetStatus


from beebium.ext.econet.piconet._proto import piconet_service_pb2


def _channel_returning(device_path="/dev/tty.usbmodem101", serial_open=True):
    """An ExtensionChannel mock whose invoke() returns serialized status bytes,
    exactly as the real channel carries the dispatcher's reply."""
    response = piconet_service_pb2.PiconetGetStatusResponse(
        device_path=device_path, serial_open=serial_open
    )
    channel = MagicMock()
    channel.invoke.return_value = response.SerializeToString()
    return channel


@pytest.fixture
def mock_channel():
    return _channel_returning()


@pytest.fixture
def piconet(mock_channel):
    return Piconet("piconet", mock_channel)


def test_status_returns_dataclass(mock_channel, piconet):
    status = piconet.status
    assert isinstance(status, PiconetStatus)
    assert status.device_path == "/dev/tty.usbmodem101"
    assert status.serial_open is True
    # The request is tunnelled to the PiconetService dispatcher's GetStatus.
    service, method = mock_channel.invoke.call_args.args[:2]
    assert service == "PiconetService"
    assert method == "GetStatus"


def test_status_reports_disconnected(piconet):
    piconet = Piconet("piconet", _channel_returning(device_path="/dev/nonexistent", serial_open=False))
    status = piconet.status
    assert status.device_path == "/dev/nonexistent"
    assert status.serial_open is False
