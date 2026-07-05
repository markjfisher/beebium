"""BBC BASIC: run a multi-line program and read its output.

The Basic helper wraps the type/run/wait cycle so you can hand it source and
get back the screen text, without poking the keyboard matrix yourself.
"""

from __future__ import annotations

from _demo import run
from beebium.client import Beebium


def demo(bbc: Beebium) -> None:
    bbc.basic.run_program(
        "10 FOR I%=1 TO 3\n"
        '20 PRINT "LINE "; I%\n'
        "30 NEXT\n"
    )

    print("--- screen after RUN ---")
    print(bbc.basic.read_screen_text())

    print(f"\nPAGE=${bbc.basic.get_page():04X}  "
          f"TOP=${bbc.basic.get_top():04X}  "
          f"HIMEM=${bbc.basic.get_himem():04X}")


if __name__ == "__main__":
    run(demo, description="Run a BBC BASIC program and read its output.")
