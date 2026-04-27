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

"""Integration tests for typing under CAPS LOCK and SHIFT LOCK.

Reproduces and exercises the fix for GitHub issue #5: text typed via
the Python API must take account of CAPS LOCK and SHIFT LOCK, otherwise
``bbc.keyboard.type("hello")`` produces "HELLO" on screen because the
default boot state of the BBC Micro is CAPS LOCK on.
"""
from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.screen import screen_contains


HOST_CLOCK_HZ = 2_000_000


def _run_for_emulated_seconds(bbc: Beebium, seconds: float) -> None:
    """Run the emulator for the given number of emulated seconds."""
    clock_hz = bbc.system.clock_speed_hz or HOST_CLOCK_HZ
    target_cycles = int(seconds * clock_hz)
    start = bbc.debugger.cycle_count
    with bbc.debugger.running():
        while bbc.debugger.cycle_count - start < target_cycles:
            time.sleep(0.05)


def _boot_to_basic(bbc: Beebium) -> None:
    """Run the emulator long enough to reach the BASIC prompt, then stop."""
    _run_for_emulated_seconds(bbc, 2.0)
    bbc.debugger.stop()


def _boot_and_run(bbc: Beebium) -> None:
    """Boot to BASIC and leave the emulator running.

    Lock-key taps and ``text_input()`` require the emulator to be
    running so the MOS keyboard scan can detect the press.
    """
    _boot_to_basic(bbc)
    bbc.debugger.run()


class TestBootDefaults:
    """Sanity checks for the default lock state after MOS boot.

    Lock-LED state is read via the IndicatorService -- the same source
    of truth used by the macOS frontend -- not the debugger latch.
    """

    def test_caps_lock_on_after_boot(self, bbc: Beebium) -> None:
        """BBC Micro boots with CAPS LOCK on (LED lit)."""
        _boot_to_basic(bbc)
        assert bbc.indicators.get("caps-lock-led") == 255

    def test_shift_lock_off_after_boot(self, bbc: Beebium) -> None:
        """BBC Micro boots with SHIFT LOCK off."""
        _boot_to_basic(bbc)
        assert bbc.indicators.get("shift-lock-led") == 0


class TestCapsLockObservedBehaviour:
    """Tests that pin down the observable behaviour of CAPS LOCK
    when typing through the Python API. These document the very
    behaviour that issue #5 is about: lowercase keys mapped without
    SHIFT come out uppercase on screen because the BBC default is
    CAPS LOCK on.
    """

    def test_lowercase_letters_uppercased_when_caps_lock_on(
        self, bbc: Beebium
    ) -> None:
        """With CAPS LOCK on (default), typing lowercase letters echoes
        as uppercase on the BASIC input line.
        """
        _boot_and_run(bbc)
        assert bbc.indicators.get("caps-lock-led") == 255

        bbc.keyboard.type("abc")
        _run_for_emulated_seconds(bbc, 1.0)
        bbc.debugger.stop()
        assert screen_contains(bbc.memory, "ABC")
        assert not screen_contains(bbc.memory, "abc")


class TestTextInputContextManager:
    """Tests for ``Keyboard.text_input()`` -- the fix for issue #5.

    The context manager captures the current CAPS LOCK and SHIFT LOCK
    state, disables both for the duration of the block, and restores
    the original state on exit.
    """

    def test_disables_caps_lock_inside_block(self, bbc: Beebium) -> None:
        """CAPS LOCK is off inside the with-block when it was on entering."""
        _boot_and_run(bbc)
        assert bbc.indicators.get("caps-lock-led") == 255

        with bbc.keyboard.text_input():
            assert bbc.indicators.get("caps-lock-led") == 0

    def test_restores_caps_lock_after_block(self, bbc: Beebium) -> None:
        """CAPS LOCK is restored to its prior state after the with-block."""
        _boot_and_run(bbc)
        original = bbc.indicators.get("caps-lock-led")
        assert original == 255

        with bbc.keyboard.text_input():
            pass

        # tap_caps_lock has already polled until the LED settled to its
        # new definitive value before returning, so no extra wait is
        # needed before reading.
        assert bbc.indicators.get("caps-lock-led") == original

    def test_lowercase_letters_typed_as_lowercase(self, bbc: Beebium) -> None:
        """Inside the context manager, ``type("abc")`` echoes as 'abc'."""
        _boot_and_run(bbc)
        assert bbc.indicators.get("caps-lock-led") == 255

        with bbc.keyboard.text_input():
            bbc.keyboard.type("abc")
            # _run_for_emulated_seconds preserves the running state.
            _run_for_emulated_seconds(bbc, 1.0)
            assert screen_contains(bbc.memory, "abc")
            assert not screen_contains(bbc.memory, "ABC")

    def test_uppercase_letters_typed_as_uppercase(self, bbc: Beebium) -> None:
        """Inside the context manager, ``type("DEF")`` (which presses
        SHIFT for each letter) echoes as 'DEF', not 'def'."""
        _boot_and_run(bbc)
        assert bbc.indicators.get("caps-lock-led") == 255

        with bbc.keyboard.text_input():
            bbc.keyboard.type("DEF")
            _run_for_emulated_seconds(bbc, 1.0)
            assert screen_contains(bbc.memory, "DEF")
            assert not screen_contains(bbc.memory, "def")

    def test_no_op_when_locks_already_off(self, bbc: Beebium) -> None:
        """If caps lock is already off, the context manager doesn't
        toggle it and leaves it off."""
        _boot_and_run(bbc)
        bbc.keyboard.tap_caps_lock()
        assert bbc.indicators.get("caps-lock-led") == 0

        with bbc.keyboard.text_input():
            assert bbc.indicators.get("caps-lock-led") == 0

        assert bbc.indicators.get("caps-lock-led") == 0

    def test_raises_if_emulator_not_running(self, bbc: Beebium) -> None:
        """text_input() refuses to run with a stopped emulator."""
        _boot_to_basic(bbc)
        assert not bbc.debugger.is_running

        with pytest.raises(RuntimeError, match="running emulator"):
            with bbc.keyboard.text_input():
                pass


class TestTapLockKeys:
    """Tests for the ``tap_caps_lock`` and ``tap_shift_lock`` helpers."""

    def test_tap_caps_lock_toggles_led(self, bbc: Beebium) -> None:
        """A single tap of CAPS LOCK toggles the LED state."""
        _boot_and_run(bbc)
        before = bbc.indicators.get("caps-lock-led")

        bbc.keyboard.tap_caps_lock()

        after = bbc.indicators.get("caps-lock-led")
        assert after != before
        assert after in (0, 255)

    def test_tap_shift_lock_toggles_led(self, bbc: Beebium) -> None:
        """A single tap of SHIFT LOCK toggles the LED state."""
        _boot_and_run(bbc)
        before = bbc.indicators.get("shift-lock-led")

        bbc.keyboard.tap_shift_lock()

        after = bbc.indicators.get("shift-lock-led")
        assert after != before
        assert after in (0, 255)

    def test_tap_raises_if_emulator_not_running(self, bbc: Beebium) -> None:
        """tap_caps_lock requires the emulator to be running."""
        _boot_to_basic(bbc)
        assert not bbc.debugger.is_running

        with pytest.raises(RuntimeError, match="running emulator"):
            bbc.keyboard.tap_caps_lock()
