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

"""Packaging assertions that must hold against the *installed wheel*.

These tests are run by scripts/test-wheel.sh, which builds the wheel, installs
it into a fresh virtualenv, and runs this module from a working directory
outside the source tree. They deliberately import nothing from the server and
need no running emulator: their job is to catch packaging defects -- missing
package data, an unshipped py.typed marker, an unregistered entry point, or the
package accidentally resolving from src/ instead of site-packages.

They live outside tests/ so the server-fixture conftest there does not apply.
"""

from __future__ import annotations

import importlib
import importlib.metadata
import importlib.resources

import pytest


def test_package_imports_from_site_packages_not_source_tree():
    """The client must import from the installed wheel, not the checkout.

    If this fails the whole wheel test is meaningless -- it would be exercising
    src/ and would miss every packaging bug.
    """
    import beebium

    assert beebium.__file__ is not None
    parts = beebium.__file__.replace("\\", "/").split("/")
    assert "src" not in parts, (
        f"beebium imported from a source tree ({beebium.__file__}); the wheel "
        f"test must run from outside src/ so imports resolve from site-packages."
    )


def test_public_api_surface():
    """The documented top-level names are importable from the installed wheel."""
    from beebium import DEFAULT_GRPC_PORT, Beebium, __version__

    assert Beebium is not None
    assert DEFAULT_GRPC_PORT == 0xBEEB
    assert isinstance(__version__, str) and __version__


def test_version_matches_distribution_metadata():
    """__version__ agrees with the installed distribution's metadata version."""
    import beebium

    assert beebium.__version__ == importlib.metadata.version("beebium")


@pytest.mark.parametrize(
    "module_name",
    [
        # A spread across core services and the newly-generated discovery stub,
        # to prove the generated _proto package data is shipped in the wheel.
        "beebium._proto.video_pb2",
        "beebium._proto.debugger_pb2_grpc",
        "beebium._proto.peripheral_extension_pb2",
        "beebium._proto.peripheral_extension_pb2_grpc",
        "beebium._proto.audio_pb2_grpc",
        "beebium._proto.tube_pb2_grpc",
        "beebium._proto.acorn_rtc_pb2_grpc",
        "beebium._proto.scsi_host_adapter_pb2_grpc",
    ],
)
def test_generated_proto_stubs_are_shipped(module_name: str):
    """Every generated stub the client relies on is present in the wheel."""
    module = importlib.import_module(module_name)
    assert module is not None


def test_peripheral_extension_service_stub_present():
    """The discovery service stub (new in this branch) is importable."""
    from beebium._proto import peripheral_extension_pb2_grpc as grpc_mod

    assert hasattr(grpc_mod, "PeripheralExtensionServiceStub")


def test_py_typed_marker_is_shipped():
    """PEP 561: the py.typed marker must be packaged so downstream type
    checkers honour the client's inline annotations."""
    marker = importlib.resources.files("beebium").joinpath("py.typed")
    assert marker.is_file(), "py.typed marker missing from the installed package"


def test_pytest_plugin_entry_point_registered():
    """The pytest11 entry point is registered so `pip install beebium` makes
    the fixtures available to a downstream test suite."""
    entry_points = importlib.metadata.entry_points(group="pytest11")
    names = {ep.name: ep.value for ep in entry_points}
    assert names.get("beebium") == "beebium.pytest_plugin"
