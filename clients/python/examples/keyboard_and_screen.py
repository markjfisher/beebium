"""Keyboard + screen: type into BASIC, then read the MODE 7 display back.

Keyboard input goes through the hardware key matrix (type-ahead paced like real
typing). The screen helpers read what is actually displayed in MODE 7, undoing
hardware scrolling so a fixed read of screen memory isn't rotated.
"""

from __future__ import annotations

from _demo import run
from beebium.client.screen import dump_screen, find, screen_contains


def demo(bbc):
    # Wait for the BASIC prompt, then type a one-liner and run it.
    bbc.run_until_or_timeout(lambda: screen_contains(bbc, ">"), emulated_seconds=3.0)

    bbc.keyboard.type('PRINT "HELLO, BEEB"')
    bbc.keyboard.press_return()

    printed = bbc.run_until_or_timeout(
        lambda: screen_contains(bbc, "HELLO, BEEB"), emulated_seconds=3.0
    )
    print(f"output appeared: {printed}\n")

    # The whole teletext screen as text, and a cell lookup.
    print(dump_screen(bbc))
    print(f"\n'HELLO' is at (row, col) = {find(bbc, 'HELLO')}")


if __name__ == "__main__":
    run(demo, description="Type into BASIC and read the MODE 7 screen.")
