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

from beebium.piconet import Piconet, PiconetStatus


class MockGetStatusResponse:
    def __init__(self, device_path="/dev/tty.usbmodem101", serial_open=True):
        self.device_path = device_path
        self.serial_open = serial_open


@pytest.fixture
def mock_stub():
    stub = MagicMock()
    stub.GetStatus.return_value = MockGetStatusResponse()
    return stub


@pytest.fixture
def piconet(mock_stub):
    return Piconet(mock_stub)


def test_status_returns_dataclass(mock_stub, piconet):
    status = piconet.status
    assert isinstance(status, PiconetStatus)
    assert status.device_path == "/dev/tty.usbmodem101"
    assert status.serial_open is True


def test_status_reports_disconnected(mock_stub, piconet):
    mock_stub.GetStatus.return_value = MockGetStatusResponse(
        device_path="/dev/nonexistent", serial_open=False
    )
    status = piconet.status
    assert status.device_path == "/dev/nonexistent"
    assert status.serial_open is False
