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

"""Tests for the peripheral-extension discovery client (bbc.extensions).

The conversion tests are server-free: they build peripheral_extension_pb2
messages directly and assert the dataclass mapping. The integration tests
launch a real server with an extension loaded and assert it is discovered.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.exceptions import ServerNotFoundError
from beebium._proto import peripheral_extension_pb2 as pe_pb2
from beebium.extensions import (
    ExtensionInfo,
    ParameterSchemaInfo,
    StorageDevice,
    StorageKind,
    _extension_info_from_proto,
)


@pytest.fixture(scope="module")
def bbc_with_rpc_serial(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
):
    """A server launched with the rpc-serial extension loaded.

    Gives the discovery tests a known extension to find. Shared across the
    module -- discovery is read-only, so a fresh server per test is wasteful.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=["--rpc-serial"],
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


# --------------------------------------------------------------------------
# Server-free conversion tests
# --------------------------------------------------------------------------

def test_extension_info_from_proto_maps_all_scalar_fields():
    proto = pe_pb2.ExtensionInfo(
        name="rpc-serial",
        id="rpc-serial",
        label="RPC Serial",
        attaches_to=["serial-port"],
        provides=["serial"],
        description="Client-driven serial peer",
        has_ui=False,
    )
    info = _extension_info_from_proto(proto)

    assert isinstance(info, ExtensionInfo)
    assert info.name == "rpc-serial"
    assert info.id == "rpc-serial"
    assert info.label == "RPC Serial"
    assert info.attaches_to == ("serial-port",)
    assert info.provides == ("serial",)
    assert info.description == "Client-driven serial peer"
    assert info.has_ui is False


def test_extension_info_from_proto_maps_config_map():
    proto = pe_pb2.ExtensionInfo(name="rpc-serial", id="rpc-serial")
    proto.config["tx_buffer"] = "4096"
    proto.config["mode"] = "pty"

    info = _extension_info_from_proto(proto)

    assert info.config == {"tx_buffer": "4096", "mode": "pty"}


def test_extension_info_from_proto_maps_parameter_schema():
    proto = pe_pb2.ExtensionInfo(name="rpc-serial", id="rpc-serial")
    proto.parameters.add(
        key="tx_buffer",
        type="integer",
        description="transmit buffer size in bytes",
        position=-1,
        required=False,
        default_value="4096",
    )
    info = _extension_info_from_proto(proto)

    assert len(info.parameters) == 1
    param = info.parameters[0]
    assert isinstance(param, ParameterSchemaInfo)
    assert param.key == "tx_buffer"
    assert param.type == "integer"
    assert param.position == -1
    assert param.required is False
    assert param.default_value == "4096"


def test_extension_info_from_proto_maps_storage_devices():
    proto = pe_pb2.ExtensionInfo(name="acorn-scsi", id="acorn-scsi")
    proto.storage_devices.add(
        id="acorn-scsi",
        name="Hard Disc Drive 0",
        kind=pe_pb2.StorageDevice.FIXED,
        media_type="hard-disc",
        backing_path="/tmp/hd0.dat",
        activity_indicator_name="hdd-0-activity-led",
    )
    info = _extension_info_from_proto(proto)

    assert len(info.storage_devices) == 1
    device = info.storage_devices[0]
    assert isinstance(device, StorageDevice)
    assert device.id == "acorn-scsi"
    assert device.name == "Hard Disc Drive 0"
    assert device.kind is StorageKind.FIXED
    assert device.media_type == "hard-disc"
    assert device.backing_path == "/tmp/hd0.dat"
    assert device.activity_indicator_name == "hdd-0-activity-led"


def test_extension_info_dataclass_is_frozen():
    proto = pe_pb2.ExtensionInfo(name="rpc-serial", id="rpc-serial")
    info = _extension_info_from_proto(proto)
    with pytest.raises(Exception):
        info.name = "changed"  # type: ignore[misc]


# --------------------------------------------------------------------------
# Integration tests (launch a real server with an extension loaded)
# --------------------------------------------------------------------------

def test_loaded_reports_a_launched_extension(bbc_with_rpc_serial):
    loaded = bbc_with_rpc_serial.extensions.loaded
    names = {info.name for info in loaded}
    assert "rpc-serial" in names, f"rpc-serial not among loaded extensions: {names}"


def test_info_looks_up_by_name(bbc_with_rpc_serial):
    info = bbc_with_rpc_serial.extensions.info("rpc-serial")
    assert info is not None
    assert info.name == "rpc-serial"
    assert "serial-port" in info.attaches_to


def test_info_returns_none_for_unknown_extension(bbc_with_rpc_serial):
    assert bbc_with_rpc_serial.extensions.info("no-such-extension") is None


def test_extensions_membership_and_iteration(bbc_with_rpc_serial):
    exts = bbc_with_rpc_serial.extensions
    assert "rpc-serial" in exts
    assert len(exts) >= 1
    assert any(info.name == "rpc-serial" for info in exts)
