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

"""Tests for the extension-adapter framework (beebium.extension) and the
typed/generic bridge (bbc.extensions[...]).

The registry tests are server-free (they only read entry points and classes).
The bridge tests launch a real server with the rpc-serial extension loaded.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.acorn_rtc import AcornRtc
from beebium.acorn_scsi import AcornScsi
from beebium.aun import Aun
from beebium.client import Beebium
from beebium.exceptions import (
    ExtensionAdapterNotInstalledError,
    ExtensionNotLoadedError,
)
from beebium.extension import (
    ExtensionAdapter,
    adapter_type,
    describe_adapter,
    installed_adapter_names,
)
from beebium.host_serial import HostSerial
from beebium.piconet import Piconet
from beebium.rpc_serial import RpcSerial
from beebium.exceptions import ServerNotFoundError


_FIRST_PARTY = {
    "aun": Aun,
    "piconet": Piconet,
    "rpc-serial": RpcSerial,
    "host-serial": HostSerial,
    "acorn-rtc": AcornRtc,
    "acorn-scsi": AcornScsi,
}


# --------------------------------------------------------------------------
# Server-free registry tests
# --------------------------------------------------------------------------

def test_first_party_adapters_are_registered():
    names = installed_adapter_names()
    for name in _FIRST_PARTY:
        assert name in names, f"{name} adapter not registered in beebium.ext"


@pytest.mark.parametrize("name,cls", list(_FIRST_PARTY.items()))
def test_adapter_type_resolves_to_the_concrete_class(name, cls):
    resolved = adapter_type(name)
    assert resolved is cls
    assert issubclass(resolved, ExtensionAdapter)


@pytest.mark.parametrize("name,cls", list(_FIRST_PARTY.items()))
def test_extension_name_matches_entry_point_key(name, cls):
    assert cls.EXTENSION_NAME == name


def test_adapter_type_raises_for_unknown_name():
    with pytest.raises(ExtensionAdapterNotInstalledError):
        adapter_type("no-such-adapter")


def test_describe_adapter_reads_the_docstring():
    text = describe_adapter("rpc-serial", single_line=True)
    assert text
    assert "\n" not in text
    # Full description contains more than the first line.
    assert len(describe_adapter("rpc-serial")) >= len(text)


# --------------------------------------------------------------------------
# Integration tests: the bbc.extensions bridge
# --------------------------------------------------------------------------

@pytest.fixture(scope="module")
def bbc_rpc_serial(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
):
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


def test_subscript_by_class_returns_concrete_adapter(bbc_rpc_serial):
    rpc = bbc_rpc_serial.extensions[RpcSerial]
    assert isinstance(rpc, RpcSerial)
    assert rpc.name == "rpc-serial"
    # It is bound to the server's instance id from ListExtensions.
    assert rpc.extension_id == bbc_rpc_serial.extensions.info("rpc-serial").id


def test_subscript_by_string_returns_a_working_adapter(bbc_rpc_serial):
    rpc = bbc_rpc_serial.extensions["rpc-serial"]
    assert isinstance(rpc, ExtensionAdapter)
    assert isinstance(rpc, RpcSerial)  # concrete class from the registry


def test_attach_classmethod_is_equivalent_to_subscript(bbc_rpc_serial):
    rpc = RpcSerial.attach(bbc_rpc_serial)
    assert isinstance(rpc, RpcSerial)
    assert rpc.extension_id == bbc_rpc_serial.extensions[RpcSerial].extension_id


def test_bound_adapter_actually_drives_the_extension(bbc_rpc_serial):
    rpc = bbc_rpc_serial.extensions[RpcSerial]
    # send returns the accepted count -- proves the round trip through the
    # ExtensionRpc channel works end to end.
    assert rpc.send(b"AB") == 2
    status = rpc.status
    assert status.rx_pending >= 0


def test_subscript_for_unloaded_extension_raises(bbc_rpc_serial):
    # aun is a registered adapter but not loaded on this server.
    with pytest.raises(ExtensionNotLoadedError):
        _ = bbc_rpc_serial.extensions[Aun]


def test_get_returns_none_for_unloaded_extension(bbc_rpc_serial):
    assert bbc_rpc_serial.extensions.get(Aun) is None
    assert bbc_rpc_serial.extensions.get("aun") is None


def test_get_returns_the_adapter_when_loaded(bbc_rpc_serial):
    assert isinstance(bbc_rpc_serial.extensions.get(RpcSerial), RpcSerial)
