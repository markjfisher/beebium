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
    MachineIdentity,
    Provenance,
    ServerStatus,
    ServerStatusEvent,
    System,
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


class MockIdentityResponse:
    """Mock identity from proto."""

    def __init__(
        self,
        uuid: str = "12345678-1234-1234-1234-123456789abc",
        name: str = "BBC Model B",
        model_type: str = "ModelB",
        model_name: str = "BBC Model B 32K",
    ):
        self.uuid = uuid
        self.name = name
        self.model_type = model_type
        self.model_name = model_name


class MockSystemInfoResponse:
    """Mock GetSystemInfo response."""

    def __init__(
        self,
        provenance: MockProvenanceResponse | None = None,
        identity: MockIdentityResponse | None = None,
    ):
        self.provenance = provenance or MockProvenanceResponse()
        self.identity = identity or MockIdentityResponse()


class MockSetMachineNameResponse:
    """Mock SetMachineName response."""

    def __init__(self, name: str = "New Name"):
        self.identity = MockIdentityResponse(name=name)


@pytest.fixture
def mock_stub():
    """Create a mock gRPC stub."""
    stub = MagicMock()
    stub.GetSystemInfo.return_value = MockSystemInfoResponse()
    stub.SetMachineName.return_value = MockSetMachineNameResponse()
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


class TestMachineIdentityClass:
    """Tests for the MachineIdentity class."""

    def test_machine_identity_has_expected_properties(self, mock_stub):
        """MachineIdentity has uuid, name, model_type, model_name, system properties."""
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="550e8400-e29b-41d4-a716-446655440000",
            name="My BBC Micro",
            model_type="ModelB",
            model_name="BBC Model B 32K",
            system=system,
        )
        assert identity.uuid == "550e8400-e29b-41d4-a716-446655440000"
        assert identity.name == "My BBC Micro"
        assert identity.model_type == "ModelB"
        assert identity.model_name == "BBC Model B 32K"
        assert identity.system is system

    def test_uuid_is_readonly(self, mock_stub):
        """uuid property cannot be assigned."""
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="u", name="n", model_type="t", model_name="m", system=system
        )
        with pytest.raises(AttributeError):
            identity.uuid = "new-uuid"

    def test_model_type_is_readonly(self, mock_stub):
        """model_type property cannot be assigned."""
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="u", name="n", model_type="t", model_name="m", system=system
        )
        with pytest.raises(AttributeError):
            identity.model_type = "new-type"

    def test_model_name_is_readonly(self, mock_stub):
        """model_name property cannot be assigned."""
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="u", name="n", model_type="t", model_name="m", system=system
        )
        with pytest.raises(AttributeError):
            identity.model_name = "new-name"

    def test_name_setter_calls_grpc(self, mock_stub):
        """Setting name calls SetMachineName gRPC."""
        mock_stub.SetMachineName.return_value = MockSetMachineNameResponse(name="New Name")
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="u", name="Old", model_type="t", model_name="m", system=system
        )
        identity.name = "New Name"
        mock_stub.SetMachineName.assert_called_once()
        assert identity.name == "New Name"

    def test_repr(self, mock_stub):
        """__repr__ returns informative string."""
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="uuid", name="name", model_type="type", model_name="model", system=system
        )
        r = repr(identity)
        assert "uuid" in r
        assert "name" in r
        assert "type" in r
        assert "model" in r

    def test_equality(self, mock_stub):
        """Two MachineIdentity objects with same values are equal."""
        system = System(mock_stub)
        id1 = MachineIdentity(uuid="u", name="n", model_type="t", model_name="m", system=system)
        id2 = MachineIdentity(uuid="u", name="n", model_type="t", model_name="m", system=system)
        assert id1 == id2

    def test_inequality(self, mock_stub):
        """MachineIdentity objects with different values are not equal."""
        system = System(mock_stub)
        id1 = MachineIdentity(uuid="u1", name="n", model_type="t", model_name="m", system=system)
        id2 = MachineIdentity(uuid="u2", name="n", model_type="t", model_name="m", system=system)
        assert id1 != id2


class TestServerStatusEnum:
    """Tests for the ServerStatus enum."""

    def test_server_status_values(self):
        """ServerStatus has READY, SHUTTING_DOWN, and IDENTITY_CHANGED values."""
        assert ServerStatus.READY.value == "ready"
        assert ServerStatus.SHUTTING_DOWN.value == "shutting_down"
        assert ServerStatus.IDENTITY_CHANGED.value == "identity_changed"


class TestServerStatusEventDataclass:
    """Tests for the ServerStatusEvent dataclass."""

    def test_server_status_event_has_expected_fields(self):
        """ServerStatusEvent has status, message, shutdown_grace_ms, and identity fields."""
        event = ServerStatusEvent(
            status=ServerStatus.SHUTTING_DOWN,
            message="Server shutting down",
            shutdown_grace_ms=5000,
        )
        assert event.status == ServerStatus.SHUTTING_DOWN
        assert event.message == "Server shutting down"
        assert event.shutdown_grace_ms == 5000
        assert event.identity is None

    def test_server_status_event_with_identity(self, mock_stub):
        """ServerStatusEvent can include identity for IDENTITY_CHANGED events."""
        system = System(mock_stub)
        identity = MachineIdentity(
            uuid="u", name="n", model_type="t", model_name="m", system=system
        )
        event = ServerStatusEvent(
            status=ServerStatus.IDENTITY_CHANGED,
            message="Identity changed",
            shutdown_grace_ms=0,
            identity=identity,
        )
        assert event.status == ServerStatus.IDENTITY_CHANGED
        assert event.identity is identity

    def test_server_status_event_is_frozen(self):
        """ServerStatusEvent is immutable."""
        event = ServerStatusEvent(
            status=ServerStatus.READY, message="Ready", shutdown_grace_ms=0
        )
        with pytest.raises(AttributeError):
            event.status = ServerStatus.SHUTTING_DOWN


class TestSystemIdentityProperty:
    """Tests for the System.identity property."""

    def test_identity_property_returns_machine_identity(self, system, mock_stub):
        """identity property returns a MachineIdentity object."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        identity = system.identity
        assert isinstance(identity, MachineIdentity)

    def test_identity_caches_result(self, system, mock_stub):
        """identity property caches the result."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        _ = system.identity
        _ = system.identity
        mock_stub.GetSystemInfo.assert_called_once()

    def test_identity_has_system_reference(self, system, mock_stub):
        """identity has reference back to the system."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        identity = system.identity
        assert identity.system is system

    def test_identity_fields_match_server_response(self, system, mock_stub):
        """identity fields match what server returned."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse(
            identity=MockIdentityResponse(
                uuid="test-uuid",
                name="Test Server",
                model_type="ModelBPlus",
                model_name="BBC Model B+ 64K",
            )
        )
        identity = system.identity
        assert identity.uuid == "test-uuid"
        assert identity.name == "Test Server"
        assert identity.model_type == "ModelBPlus"
        assert identity.model_name == "BBC Model B+ 64K"


class TestSystemProvenanceProperty:
    """Tests for the System.provenance property."""

    def test_provenance_property_returns_provenance(self, system, mock_stub):
        """provenance property returns a Provenance object."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        assert isinstance(system.provenance, Provenance)

    def test_provenance_property_caches_result(self, system, mock_stub):
        """provenance property caches the result."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
        _ = system.provenance
        _ = system.provenance
        # provenance makes its own GetSystemInfo call
        assert mock_stub.GetSystemInfo.call_count >= 1

    def test_provenance_fields_match_server_response(self, system, mock_stub):
        """provenance fields match what server returned."""
        mock_stub.GetSystemInfo.return_value = MockSystemInfoResponse()
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


class MockAdvertisementState:
    """Mock advertisement state from proto."""

    def __init__(
        self,
        enabled: bool = False,
        available: bool = True,
        advertised_name: str = "",
    ):
        self.enabled = enabled
        self.available = available
        self.advertised_name = advertised_name


class MockGetAdvertisementStateResponse:
    """Mock GetAdvertisementState response."""

    def __init__(self, state: MockAdvertisementState | None = None):
        self.state = state or MockAdvertisementState()


class MockSetAdvertisementResponse:
    """Mock SetAdvertisement response."""

    def __init__(self, state: MockAdvertisementState | None = None):
        self.state = state or MockAdvertisementState()


class TestAdvertisementState:
    """Tests for the AdvertisementState dataclass."""

    def test_advertisement_state_has_expected_fields(self):
        """AdvertisementState has enabled, available, and advertised_name fields."""
        from beebium.system import AdvertisementState

        state = AdvertisementState(
            enabled=True,
            available=True,
            advertised_name="BBC Model B",
        )
        assert state.enabled is True
        assert state.available is True
        assert state.advertised_name == "BBC Model B"

    def test_advertisement_state_is_frozen(self):
        """AdvertisementState is immutable."""
        from beebium.system import AdvertisementState

        state = AdvertisementState(enabled=False, available=True, advertised_name="")
        with pytest.raises(AttributeError):
            state.enabled = True


class TestSystemAdvertisementMethods:
    """Tests for advertisement methods on System."""

    def test_get_advertisement_state(self, mock_stub):
        """get_advertisement_state returns AdvertisementState."""
        from beebium.system import AdvertisementState

        mock_stub.GetAdvertisementState.return_value = MockGetAdvertisementStateResponse(
            MockAdvertisementState(enabled=True, available=True, advertised_name="Test")
        )
        system = System(mock_stub)

        state = system.get_advertisement_state()
        assert isinstance(state, AdvertisementState)
        assert state.enabled is True
        assert state.available is True
        assert state.advertised_name == "Test"

    def test_get_advertisement_state_unavailable(self, mock_stub):
        """get_advertisement_state handles unavailable mDNS."""
        mock_stub.GetAdvertisementState.return_value = MockGetAdvertisementStateResponse(
            MockAdvertisementState(enabled=False, available=False, advertised_name="")
        )
        system = System(mock_stub)

        state = system.get_advertisement_state()
        assert state.enabled is False
        assert state.available is False
        assert state.advertised_name == ""

    def test_set_advertisement_enable(self, mock_stub):
        """set_advertisement enables advertising."""
        mock_stub.SetAdvertisement.return_value = MockSetAdvertisementResponse(
            MockAdvertisementState(enabled=True, available=True, advertised_name="My BBC")
        )
        system = System(mock_stub)

        state = system.set_advertisement(enabled=True)
        mock_stub.SetAdvertisement.assert_called_once()
        assert state.enabled is True
        assert state.advertised_name == "My BBC"

    def test_set_advertisement_disable(self, mock_stub):
        """set_advertisement disables advertising."""
        mock_stub.SetAdvertisement.return_value = MockSetAdvertisementResponse(
            MockAdvertisementState(enabled=False, available=True, advertised_name="")
        )
        system = System(mock_stub)

        state = system.set_advertisement(enabled=False)
        mock_stub.SetAdvertisement.assert_called_once()
        assert state.enabled is False

    def test_set_advertisement_returns_unavailable_state(self, mock_stub):
        """set_advertisement returns unavailable state when mDNS not supported."""
        mock_stub.SetAdvertisement.return_value = MockSetAdvertisementResponse(
            MockAdvertisementState(enabled=False, available=False, advertised_name="")
        )
        system = System(mock_stub)

        state = system.set_advertisement(enabled=True)
        # Even though we requested enabled=True, it returns False because unavailable
        assert state.enabled is False
        assert state.available is False
