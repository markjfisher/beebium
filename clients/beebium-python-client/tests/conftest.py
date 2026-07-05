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

"""pytest configuration for beebium Python client tests.

The beebium pytest fixtures are auto-registered via the entry point in pyproject.toml.
This conftest.py adds additional test-specific fixtures shared across test files.
"""

from pathlib import Path

import pytest

from tube_test_helpers import DFS_1770_ROM_CANDIDATES, find_dfs_1770_rom


# The beebium fixtures (bbc, bbc_shared, stopped_bbc, etc.) are automatically
# available from the beebium.client.pytest_plugin module via the entry point.


@pytest.fixture(scope="module")
def dfs_1770_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to a 1770 DFS ROM file."""
    path = find_dfs_1770_rom(beebium_roms_dirpath)
    if path is None:
        pytest.skip(
            f"1770 DFS ROM not found. Expected one of: {', '.join(DFS_1770_ROM_CANDIDATES)}"
        )
    return path
