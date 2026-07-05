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

    # Conditional breakpoint on program OUTPUT. OSWRCH writes the byte in A to
    # the current output. Code reaches it through the vector WRCHV at &020E, so a
    # breakpoint on the &FFEE entry misses BASIC's own output (it calls the
    # routine via the vector). &020E is a pointer, not code -- you don't break on
    # the vector, you read it and break at the routine it points to. That catches
    # every write. PRINT 6*7 prints "42", and '4' (&34) never appears in the
    # typed line, so we stop on the printed result, not on our keystroke echoes.
    bbc.run_until_or_timeout(lambda: screen_contains(bbc, ">"), emulated_seconds=3.0)
    oswrch = bbc.memory.address.peek[0x020E] | (bbc.memory.address.peek[0x020F] << 8)
    bbc.keyboard.type("PRINT 6*7\r")

    # A breakpoint used as a context manager is removed again on exit.
    with bbc.debugger.breakpoint(oswrch, condition="A == 0x34"):
        bbc.debugger.ensure_running()
        bbc.debugger.wait_for_stop()
    char = bbc.cpu.a  # '4' -- the first digit of the printed result 42
    glyph = chr(char) if 32 <= char < 127 else "?"
    print(f"broke as PRINT printed {glyph!r} (${char:02X}) via OSWRCH at ${oswrch:04X}")

    print(f"total cycles executed: {bbc.debugger.cycle_count}")


if __name__ == "__main__":
    run(demo, description="Debugger control and CPU register access.")
