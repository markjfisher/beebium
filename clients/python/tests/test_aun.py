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

"""Unit tests for the Aun client wrapper around AunService."""

from __future__ import annotations

from unittest.mock import MagicMock

import pytest

from beebium.aun import Aun, AunStatus, PeerInfo
from beebium.exceptions import EconetError


# --- Mock proto response types ---


class MockGetStatusResponse:
    def __init__(self, connected=True, local_port=32768, peer_count=0):
        self.connected = connected
        self.local_port = local_port
        self.peer_count = peer_count


class MockSetConnectedResponse:
    def __init__(self, success=True, error=""):
        self.success = success
        self.error = error


class MockAddPeerResponse:
    def __init__(self, success=True, error=""):
        self.success = success
        self.error = error


class MockRemovePeerResponse:
    def __init__(self, success=True, error=""):
        self.success = success
        self.error = error


class MockAunPeer:
    def __init__(self, net=0, stn=254, ip_address="127.0.0.1", port=32768):
        self.net = net
        self.stn = stn
        self.ip_address = ip_address
        self.port = port


class MockListPeersResponse:
    def __init__(self, peers=None):
        self.peers = peers if peers is not None else []


# --- Fixtures ---


@pytest.fixture
def mock_stub():
    stub = MagicMock()
    stub.GetStatus.return_value = MockGetStatusResponse()
    stub.SetConnected.return_value = MockSetConnectedResponse()
    stub.AddPeer.return_value = MockAddPeerResponse()
    stub.RemovePeer.return_value = MockRemovePeerResponse()
    stub.ListPeers.return_value = MockListPeersResponse()
    return stub


@pytest.fixture
def aun(mock_stub):
    return Aun(mock_stub)


# --- Tests ---


class TestStatus:
    def test_status_returns_dataclass(self, mock_stub, aun):
        mock_stub.GetStatus.return_value = MockGetStatusResponse(
            connected=True, local_port=32768, peer_count=2
        )
        status = aun.status
        assert isinstance(status, AunStatus)
        assert status.connected is True
        assert status.local_port == 32768
        assert status.peer_count == 2


class TestSetConnected:
    def test_set_connected_true(self, mock_stub, aun):
        aun.set_connected(True)
        request = mock_stub.SetConnected.call_args[0][0]
        assert request.connected is True

    def test_set_connected_false(self, mock_stub, aun):
        aun.set_connected(False)
        request = mock_stub.SetConnected.call_args[0][0]
        assert request.connected is False

    def test_set_connected_failure_raises(self, mock_stub, aun):
        mock_stub.SetConnected.return_value = MockSetConnectedResponse(
            success=False, error="AUN backend is not active"
        )
        with pytest.raises(EconetError, match="not active"):
            aun.set_connected(True)


class TestAddPeer:
    def test_add_peer(self, mock_stub, aun):
        aun.add_peer(net=1, stn=254, ip_address="192.168.1.100", port=32768)
        request = mock_stub.AddPeer.call_args[0][0]
        assert request.net == 1
        assert request.stn == 254
        assert request.ip_address == "192.168.1.100"
        assert request.port == 32768

    def test_add_peer_default_port(self, mock_stub, aun):
        aun.add_peer(net=0, stn=1, ip_address="10.0.0.1")
        request = mock_stub.AddPeer.call_args[0][0]
        assert request.port == 0

    def test_add_peer_failure_raises(self, mock_stub, aun):
        mock_stub.AddPeer.return_value = MockAddPeerResponse(
            success=False, error="invalid ip_address"
        )
        with pytest.raises(EconetError, match="invalid ip_address"):
            aun.add_peer(net=0, stn=1, ip_address="not.an.ip")


class TestRemovePeer:
    def test_remove_peer(self, mock_stub, aun):
        aun.remove_peer(net=1, stn=254)
        request = mock_stub.RemovePeer.call_args[0][0]
        assert request.net == 1
        assert request.stn == 254

    def test_remove_peer_failure_raises(self, mock_stub, aun):
        mock_stub.RemovePeer.return_value = MockRemovePeerResponse(
            success=False, error="AUN backend is not active"
        )
        with pytest.raises(EconetError, match="not active"):
            aun.remove_peer(net=0, stn=99)


class TestPeers:
    def test_peers_empty(self, aun):
        assert aun.peers == []

    def test_peers_populated(self, mock_stub, aun):
        mock_stub.ListPeers.return_value = MockListPeersResponse(
            peers=[
                MockAunPeer(net=0, stn=1, ip_address="192.168.1.1", port=32768),
                MockAunPeer(net=1, stn=254, ip_address="10.0.0.100", port=9999),
            ]
        )
        peers = aun.peers
        assert len(peers) == 2
        assert isinstance(peers[0], PeerInfo)
        assert peers[0].net == 0
        assert peers[0].ip_address == "192.168.1.1"
        assert peers[1].port == 9999
