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

"""Pytest fixtures for ADFS integration tests.

These fixtures manage the full test lifecycle:
- Building basictool (session-scope, once per test run)
- Extracting blank ADFS hard disc images (per-test isolation)
- Building DFS floppy discs with tokenised BASIC test programs
- Launching the emulator with the correct ROM and extension configuration
- Waiting for boot and providing screen-reading helpers
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from beebium.screen import screen_contains, dump_screen

from adfs_test_support.basictool import build_basictool
from adfs_test_support.hard_disc_image import extract_blank_adfs_image


# ---- Session-scope fixtures ----

@pytest.fixture(scope="session")
def basictool_filepath(tmp_path_factory):
    """Clone and build basictool, return the executable path."""
    build_dirpath = tmp_path_factory.mktemp("basictool")
    return build_basictool(build_dirpath)


@pytest.fixture(scope="session")
def roms_dirpath():
    """Path to the ROM directory."""
    env = os.environ.get("BEEBIUM_ROM_DIR")
    if env:
        p = Path(env)
        if p.is_dir():
            return p
        raise FileNotFoundError(f"BEEBIUM_ROM_DIR={env} is not a directory")

    # Try common locations relative to the repo root
    repo_root = Path(__file__).parent.parent.parent.parent
    candidates = [repo_root / "roms"]
    for c in candidates:
        if c.is_dir():
            return c
    raise FileNotFoundError(
        "ROM directory not found. Set BEEBIUM_ROM_DIR or place ROMs in roms/"
    )


@pytest.fixture(scope="session")
def mos_filepath(roms_dirpath):
    """Path to the Model B+ MOS 2.0 ROM."""
    p = roms_dirpath / "acorn-mos_2_0.rom"
    if not p.exists():
        pytest.skip(f"MOS 2.0 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def basic_filepath(roms_dirpath):
    """Path to the BBC BASIC 2 ROM."""
    p = roms_dirpath / "bbc-basic_2.rom"
    if not p.exists():
        pytest.skip(f"BASIC 2 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def adfs_rom_filepath(roms_dirpath):
    """Path to the ADFS 1.30 ROM."""
    p = roms_dirpath / "acorn-adfs_1_30.rom"
    if not p.exists():
        pytest.skip(f"ADFS 1.30 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def dfs_rom_filepath(roms_dirpath):
    """Path to the DFS 2.26 ROM."""
    p = roms_dirpath / "acorn-dfs_2_26.rom"
    if not p.exists():
        pytest.skip(f"DFS 2.26 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def bplus_server_filepath():
    """Path to the beebium-model-b-plus server executable."""
    # Try BEEBIUM_SERVER_BPLUS first
    env = os.environ.get("BEEBIUM_SERVER_BPLUS")
    if env:
        p = Path(env)
        if p.exists():
            return p
        raise FileNotFoundError(f"BEEBIUM_SERVER_BPLUS={env} not found")

    # Derive from BEEBIUM_SERVER by replacing model-b with model-b-plus
    env = os.environ.get("BEEBIUM_SERVER")
    if env:
        p = Path(env.replace("beebium-model-b", "beebium-model-b-plus"))
        if p.exists():
            return p

    # Try common build locations
    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        repo_root / "build" / "src" / "server" / f"beebium-model-b-plus{exe_suffix}",
        repo_root / "cmake-build-debug" / "src" / "server" / f"beebium-model-b-plus{exe_suffix}",
    ]
    for c in candidates:
        if c.exists():
            return c

    pytest.skip(
        "beebium-model-b-plus not found. "
        "Set BEEBIUM_SERVER_BPLUS or build the server."
    )


# ---- Function-scope fixtures ----

@pytest.fixture
def blank_scsi_disc(tmp_path):
    """Extract a fresh blank 2 MB ADFS hard disc image for this test."""
    return extract_blank_adfs_image(tmp_path / "scsi0.dat", size_mb=2)


