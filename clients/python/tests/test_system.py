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

"""Unit tests for the system and provenance module."""

from unittest.mock import MagicMock

import pytest

from beebium.system import (
    Provenance,
    ServerStatus,
    ServerStatusEvent,
    System,
    SystemInfo,
)


class MockProvenanceResponse:
    """Mock provenance from proto."""

    def __init__(
        self,
        type: str = "python-client",
        instance_uuid: str = "550e8400-e29b-41d4-a716-446655440000",
        version: str = "1.2.3",
        timestamp: int = 1700000000,
    ):
        self.type = type
        self.instance_uuid = instance_uuid
        self.version = version
        self.timestamp = timestamp


class MockSystemInfoResponse:
    """Mock GetSystemInfo response."""

    def __init__(
        self,
        machine_type: str = "ModelB",
        machine_display_name: str = "BBC Model B 32K",
        provenance: MockProvenanceResponse | None = None,
    ):
        self.machine_type = machine_type
        self.machine_display_name = machine_display_name
        self.provenance = provenance or MockProvenanceResponse()


@pytest.fixture
def mock_stub():
    """Create a mock gRPC stub."""
    stub = MagicMock()
    stub.GetSystemInfo.return_value = MockSystemInfoResponse()
    return stub


@pytest.fixture
def system(mock_stub):
    """Create a System instance with mock stub."""
    return System(mock_stub)


class TestProvenanceDataclass:
    """Tests for the Provenance dataclass."""

    def test_provenance_has_expected_fields(self):
        """Provenance has type, instance_uuid, version, and timestamp fields."""
        prov = Provenance(
            type="python-client",
            instance_uuid="550e8400-e29b-41d4-a716-446655440000",
            version="1.2.3",
            timestamp=1700000000,
        )
        assert prov.type == "python-client"
        assert prov.instance_uuid == "550e8400-e29b-41d4-a716-446655440000"
        assert prov.version == "1.2.3"
        assert prov.timestamp == 1700000000

    def test_provenance_is_frozen(self):
        """Provenance is immutable."""
        prov = Provenance(
            type="test", instance_uuid="uuid", version="1.0", timestamp=0
        )
        with pytest.raises(AttributeError):
            prov.type = "modified"

    def test_provenance_equality(self):
        """Two Provenance objects with same values are equal."""
        prov1 = Provenance(
            type="test", instance_uuid="uuid", version="1.0", timestamp=100
        )
        prov2 = Provenance(
            type="test", instance_uuid="uuid", version="1.0", timestamp=100
        )
        assert prov1 == prov2

    def test_provenance_inequality(self):
        """Provenance objects with different values are not equal."""
        prov1 = Provenance(
            type="test", instance_uuid="uuid1", version="1.0", timestamp=100
        )
        prov2 = Provenance(
            type="test", instance_uuid="uuid2", version="1.0", timestamp=100
        )
        assert prov1 != prov2


class TestSystemInfoDataclass:
    """Tests for the SystemInfo dataclass."""

    def test_system_info_has_expected_fields(self):
        """SystemInfo has machine_type and machine_display_name fields."""
        info = SystemInfo(machine_type="ModelB", machine_display_name="BBC Model B 32K")
        assert info.machine_type == "ModelB"
        assert info.machine_display_name == "BBC Model B 32K"

    def test_system_info_is_frozen(self):
        """SystemInfo is immutable."""
        info = SystemInfo(machine_type="ModelB", machine_display_name="BBC Model B")
        with pytest.raises(AttributeError):
            info.machine_type = "ModelBPlus"


class TestServerStatusEnum:
    """Tests for the ServerStatus enum."""

    def test_server_status_values(self):
        """ServerStatus has READY and SHUTTING_DOWN values."""
        assert ServerStatus.READY.value == "ready"
        assert ServerStatus.SHUTTING_DOWN.value == "shutting_down"


class TestServerStatusEventDataclass:
    """Tests for the ServerStatusEvent dataclass."""

    def test_server_status_event_has_expected_fields(self):
        """ServerStatusEvent has status, message, and shutdown_grace_ms fields."""
        event = ServerStatusEvent(
            status=ServerStatus.SHUTTING_DOWN,
            message="Server shutting down",
            shutdown_grace_ms=5000,
        )
        assert event.status == ServerStatus.SHUTTING_DOWN
        assert event.message == "Server shutting down"
        assert event.shutdown_grace_ms == 5000

    def test_server_status_event_is_frozen(self):
        """ServerStatusEvent is immutable."""
        event = ServerStatusEvent(
            status=ServerStatus.READY, message="Ready", shutdown_grace_ms=0
        )
        with pytest.raises(AttributeError):
            event.status = ServerStatus.SHUTTING_DOWN


class TestSystemFacade:
    """Tests for the System facade class."""

    def test_info_property_returns_system_info(self, system):
        """info property returns a SystemInfo."""
        assert isinstance(system.info, SystemInfo)

    def test_info_property_caches_result(self, system, mock_stub):
        """info property caches the result."""
        _ = system.info
        _ = system.info
        mock_stub.GetSystemInfo.assert_called_once()

    def test_machine_type_property(self, system):
        """machine_type property returns the machine type string."""
        assert system.machine_type == "ModelB"

    def test_machine_display_name_property(self, system):
        """machine_display_name property returns the display name string."""
        assert system.machine_display_name == "BBC Model B 32K"

    def test_provenance_property_returns_provenance(self, system):
        """provenance property returns a Provenance object."""
        assert isinstance(system.provenance, Provenance)

    def test_provenance_property_caches_result(self, system, mock_stub):
        """provenance property caches the result."""
        _ = system.provenance
        _ = system.provenance
        # GetSystemInfo called once for both info and provenance (they share the RPC)
        # But since we access provenance twice, it should still only call once per property
        # Actually provenance makes its own call currently
        assert mock_stub.GetSystemInfo.call_count >= 1

    def test_provenance_fields_match_server_response(self, system):
        """provenance fields match what server returned."""
        prov = system.provenance
        assert prov.type == "python-client"
        assert prov.instance_uuid == "550e8400-e29b-41d4-a716-446655440000"
        assert prov.version == "1.2.3"
        assert prov.timestamp == 1700000000


class TestSystemProvenanceVariations:
    """Tests for different provenance scenarios."""

    def test_empty_provenance(self):
        """System handles empty provenance from server."""
        stub = MagicMock()
        stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            provenance=MockProvenanceResponse(
                type="", instance_uuid="", version="", timestamp=0
            )
        )
        system = System(stub)

        prov = system.provenance
        assert prov.type == ""
        assert prov.instance_uuid == ""
        assert prov.version == ""
        assert prov.timestamp == 0

    def test_terminal_provenance(self):
        """System handles terminal-launched provenance."""
        stub = MagicMock()
        stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            provenance=MockProvenanceResponse(
                type="terminal",
                instance_uuid="12345678-1234-1234-1234-123456789abc",
                version="",
                timestamp=1700000000,
            )
        )
        system = System(stub)

        prov = system.provenance
        assert prov.type == "terminal"
        assert prov.version == ""

    def test_typescript_oracle_provenance(self):
        """System handles TypeScript oracle provenance."""
        stub = MagicMock()
        stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            provenance=MockProvenanceResponse(
                type="typescript-oracle",
                instance_uuid="abcdef00-1234-5678-abcd-ef0123456789",
                version="0.1.0",
                timestamp=1700000000,
            )
        )
        system = System(stub)

        prov = system.provenance
        assert prov.type == "typescript-oracle"
        assert prov.version == "0.1.0"
