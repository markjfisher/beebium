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

"""ADFS integration test: *CAT and *EX commands on a blank disc."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.screen import screen_contains, dump_screen

from adfs_test_support.basictool import tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_test_support.helpers import load_and_run, parse_results

PROGRAM_FILEPATH = Path(__file__).parent.parent / "programs" / "test_cat.bas"


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_cat.bas and build a DFS SSD."""
    source = PROGRAM_FILEPATH.read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


@pytest.fixture
def bbc_adfs(bplus_server_filepath, mos_filepath, basic_filepath,
             adfs_rom_filepath, dfs_rom_filepath,
             blank_scsi_disc, test_disc_ssd, tmp_path):
    """Launch Model B+ with ADFS + SCSI + test program on floppy."""
    ssd_filepath = tmp_path / "test.ssd"
    ssd_filepath.write_bytes(test_disc_ssd)

    extra_args = [
        "--sideways", f"9:rom:{adfs_rom_filepath}",
        "--sideways", f"11:rom:{dfs_rom_filepath}",
        "--floppy", f"0:{ssd_filepath}",
        "--acorn-scsi",
        "--scsi-hdd", f"0:{blank_scsi_disc}",
    ]

    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=bplus_server_filepath,
        extra_args=extra_args,
        startup_timeout=15.0,
    ) as bbc:
        # Stop the machine (it starts running on launch) so we can use
        # run_until_or_timeout which manages execution in chunks.
        bbc.debugger.stop()

        # Wait for boot to BASIC prompt
        ok = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc.memory, ">"),
            emulated_seconds=10.0,
        )
        assert ok, f"Boot failed:\n{dump_screen(bbc.memory)}"
        yield bbc


def test_adfs_cat_on_blank_disc(bbc_adfs):
    """Run *CAT and *EX on a blank ADFS hard disc."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs.memory)}"

    results = parse_results(bbc_adfs)
    assert results.get("CAT-ROOT") == "PASS", \
        f"*CAT failed:\n{dump_screen(bbc_adfs.memory)}"
    assert results.get("EX-ROOT") == "PASS", \
        f"*EX failed:\n{dump_screen(bbc_adfs.memory)}"

    # Verify the root directory marker appears on screen
    assert screen_contains(bbc_adfs.memory, "$"), \
        f"Root directory '$' not found on screen:\n{dump_screen(bbc_adfs.memory)}"
