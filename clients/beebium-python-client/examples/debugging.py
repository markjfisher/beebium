"""Debugger + CPU: stop/step, breakpoints, and register access.

The debugger drives execution (stop/run/step, breakpoints and watchpoints) and
the CPU view reads and writes the 6502 registers.
"""

from __future__ import annotations

from _demo import run
from beebium.client import Beebium
from beebium.client.screen import screen_contains


def demo(bbc: Beebium) -> None:
    bbc.debugger.stop()

    # Registers has a canonical __str__ (A/X/Y/SP/PC/P + flags), so just print it.
    print(f"stopped: {bbc.cpu.registers}")

    # Writes are atomic and return the complete resulting snapshot (no re-read).
    after = bbc.cpu.update(a=0x41, x=0x42)
    print(f"after update: A=${after.a:02X} X=${after.x:02X}  carry={after.status.carry}")

    # Single-step three instructions.
    for _ in range(3):
        bbc.debugger.step()
        print(f"  step -> PC=${bbc.cpu.pc:04X}")

    # OSWRCH (&FFEE) is the OS "write a character" vector: it writes the byte in
    # A to the current output. The MOS echoes each typed key through OSWRCH, so
    # once we queue a keystroke and resume, the next OSWRCH call is that echo --
    # a deterministic place to hit the breakpoint. We never press Return, so the
    # PRINT never runs; it is the echo of the first character we break on. A
    # breakpoint used as a context manager is removed again on exit.
    bbc.run_until_or_timeout(lambda: screen_contains(bbc, ">"), emulated_seconds=3.0)
    bbc.keyboard.type("PRINT 42")  # queued input; the MOS echoes each key via OSWRCH

    with bbc.debugger.breakpoint(0xFFEE):
        bbc.debugger.ensure_running()
        bbc.debugger.wait_for_stop()
    char = bbc.cpu.a  # the byte OSWRCH is about to write -- the echoed "P"
    glyph = chr(char) if 32 <= char < 127 else "?"
    print(f"broke in OSWRCH; character in A = ${char:02X} ({glyph!r})")

    print(f"total cycles executed: {bbc.debugger.cycle_count}")


if __name__ == "__main__":
    run(demo, description="Debugger control and CPU register access.")
