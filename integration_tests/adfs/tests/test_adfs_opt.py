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

"""ADFS integration test: *OPT 4 boot option commands."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client.screen import screen_contains, read_mode7_screen, dump_screen

from adfs_test_support.basictool import tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_test_support.helpers import load_and_run, parse_results

PROGRAM_FILEPATH = Path(__file__).parent.parent / "programs" / "test_opt.bas"


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_opt.bas and build a DFS SSD."""
    source = PROGRAM_FILEPATH.read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


def test_adfs_opt_off(bbc_adfs):
    """*OPT 4,0 sets boot option to Off."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("OPT-OFF") == "PASS", \
        f"*OPT 4,0 failed:\n{dump_screen(bbc_adfs)}"


def test_adfs_opt_exec(bbc_adfs):
    """*OPT 4,3 sets boot option to Exec."""
    ok = load_and_run(bbc_adfs)
    assert ok, f"Test program did not complete:\n{dump_screen(bbc_adfs)}"
    results = parse_results(bbc_adfs)
    assert results.get("OPT-EXEC") == "PASS", \
        f"*OPT 4,3 failed:\n{dump_screen(bbc_adfs)}"

    rows = read_mode7_screen(bbc_adfs)
    screen_text = "\n".join(rows)
    assert "Exec" in screen_text or "EXEC" in screen_text, \
        f"Boot option 'Exec' not found on screen:\n{dump_screen(bbc_adfs)}"
