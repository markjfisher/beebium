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

"""Integration tests for booting Chuckie Egg 2023 (40th Anniversary Edition) via the Tube.

Chuckie Egg 2023 is a BBC Micro game that uses a 6502 second processor.
The boot sequence is:

    1. *EXEC !BOOT
    2. MODE 7 loading screen: "Chuckie Egg 2023", "40th Anniversary Edition",
       "For the BBC Micro with 65x co-pro"
    3. MODE 7 initialising screen: "Chuckie Egg 2023", "Initialising"
    4. Initialising changes to "Loading" with a Mode 7 progress bar
    5. Game starts

Known issue: Both B2 and Beebium hang at the "Initialising" stage.
B-Em progresses to "Loading". These tests help diagnose the hang.

Requirements:
    - Beebium server executable (auto-detected or via BEEBIUM_SERVER)
    - MOS 1.20 ROM and BASIC 2 ROM (via BEEBIUM_ROM_DIR)
    - Chuckie Egg 2023 disc image
"""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.exceptions import ServerNotFoundError
from beebium.screen import screen_contains, read_mode7_screen

from tube_test_helpers import (
    TUBE_CYCLES_PER_KEY,
    run_until_or_timeout,
    dump_diagnostics,
)


CHUCKIE_EGG_DISC_FILENAME = "chuckieEgg2023.ssd"


def _find_chuckie_egg_disc() -> Path | None:
    """Find the Chuckie Egg 2023 disc image.

    Search order:
    1. Test assets directory (tests/assets/discs/)
    2. Development disc collection (discs/games/)
    """
    repo_root = Path(__file__).parent.parent.parent.parent
    candidates = [
        repo_root / "tests" / "assets" / "discs" / CHUCKIE_EGG_DISC_FILENAME,
        repo_root / "discs" / "games" / CHUCKIE_EGG_DISC_FILENAME,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def _boot_to_text(bbc: Beebium, disc_filepath: Path, target_text: str,
                  emulated_seconds: float = 15.0) -> bool:
    """Insert disc, *EXEC !BOOT, and run until target_text appears on screen."""
    bbc.disc.drive(0).insert(disc_filepath)
    bbc.keyboard.type("*EXEC !BOOT\r", cycles_per_key=TUBE_CYCLES_PER_KEY)
    return run_until_or_timeout(
        bbc,
        lambda: screen_contains(bbc.memory, target_text),
        emulated_seconds=emulated_seconds,
    )


@pytest.fixture(scope="module")
def chuckie_egg_disc_filepath() -> Path:
    """Path to the Chuckie Egg 2023 disc image."""
    path = _find_chuckie_egg_disc()
    if path is None:
        pytest.skip(f"Chuckie Egg disc image not found: {CHUCKIE_EGG_DISC_FILENAME}")
    return path


@pytest.fixture
def bbc_tube(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
) -> Beebium:
    """A fresh BBC Micro with Tube, booted and ready at the BASIC prompt.

    Each test gets its own emulator instance, booted from cold.
    The fixture waits for the Tube banner before yielding.

    Configures:
    - Tube with 65C02 3MHz parasite
    - Acorn 1770 disc controller
    - 1770 DFS ROM in sideways slot 14
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--tube", "65C02-3MHz",
                "--fdc", "acorn-1770",
                "--sideways", f"14:rom:{dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            found = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc.memory, "Acorn TUBE"),
                emulated_seconds=10.0,
            )
            if not found:
                dump_diagnostics(bbc)
                pytest.fail("Tube banner not visible after boot")
            with bbc.debugger.running():
                yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.xfail(reason="CE2023 investigation paused -- see docs/discussion/chuckie-egg-2023-tube-hang.md")
class TestTubeChuckieEggBoot:
    """Test booting Chuckie Egg 2023 (40th Anniversary Edition) via the Tube."""

    def test_tube_enabled(self, bbc_tube: Beebium) -> None:
        """Verify Tube hardware is enabled and parasite is connected."""
        status = bbc_tube.tube.status
        assert status.has_tube_socket, "Machine should have a Tube socket"
        assert status.enabled, "Tube should be enabled"
        assert status.parasite_connected, "Parasite should be connected"
        assert "65C02" in status.parasite_type, (
            f"Expected 65C02 parasite, got {status.parasite_type!r}"
        )
        assert status.parasite_grpc_address, "Parasite should have registered gRPC address"

    def test_boot_loading_screen(
        self, bbc_tube: Beebium, chuckie_egg_disc_filepath: Path
    ) -> None:
        """*EXEC !BOOT produces the initial loading screen."""
        found = _boot_to_text(bbc_tube, chuckie_egg_disc_filepath,
                              "Chuckie Egg 2023", emulated_seconds=15.0)
        if not found:
            rows = read_mode7_screen(bbc_tube.memory)
            print("\nScreen after *EXEC !BOOT:")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            dump_diagnostics(bbc_tube)
            pytest.fail("Expected 'Chuckie Egg 2023' on screen after *EXEC !BOOT")

    def test_initialising_screen(
        self, bbc_tube: Beebium, chuckie_egg_disc_filepath: Path
    ) -> None:
        """Boot progresses to the Initialising screen."""
        found = _boot_to_text(bbc_tube, chuckie_egg_disc_filepath,
                              "Chuckie Egg 2023", emulated_seconds=15.0)
        assert found, "Failed to reach initial loading screen"

        # Continue running and wait for "Initialising"
        found = run_until_or_timeout(
            bbc_tube,
            lambda: screen_contains(bbc_tube.memory, "Initialising"),
            emulated_seconds=30.0,
        )
        if not found:
            rows = read_mode7_screen(bbc_tube.memory)
            print("\nScreen while waiting for Initialising:")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            dump_diagnostics(bbc_tube)
            pytest.fail("Expected 'Initialising' on screen")

    def test_loading_progresses(
        self, bbc_tube: Beebium, chuckie_egg_disc_filepath: Path
    ) -> None:
        """Initialising changes to Loading (known hang point)."""
        found = _boot_to_text(bbc_tube, chuckie_egg_disc_filepath,
                              "Chuckie Egg 2023", emulated_seconds=15.0)
        assert found, "Failed to reach initial loading screen"

        # Wait for "Initialising"
        found = run_until_or_timeout(
            bbc_tube,
            lambda: screen_contains(bbc_tube.memory, "Initialising"),
            emulated_seconds=30.0,
        )
        assert found, "Failed to reach Initialising screen"

        # This is the known hang point. Wait for "Loading" to appear.
        found = run_until_or_timeout(
            bbc_tube,
            lambda: screen_contains(bbc_tube.memory, "Loading"),
            emulated_seconds=60.0,
        )
        if not found:
            rows = read_mode7_screen(bbc_tube.memory)
            print("\nScreen while waiting for Loading (HUNG):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            dump_diagnostics(bbc_tube)
            pytest.fail(
                "Emulator hung at 'Initialising' -- expected 'Loading' to appear"
            )
