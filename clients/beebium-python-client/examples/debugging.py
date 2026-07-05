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

    r = bbc.cpu.registers
    flags = "".join(
        letter if bit else "-"
        for letter, bit in [
            ("N", r.negative), ("V", r.overflow), ("D", r.decimal),
            ("I", r.interrupt_disable), ("Z", r.zero), ("C", r.carry),
        ]
    )
    print(f"stopped: PC=${r.pc:04X} A=${r.a:02X} X=${r.x:02X} Y=${r.y:02X} "
          f"SP=${r.sp:02X} [{flags}]")

    # Single-step three instructions.
    for _ in range(3):
        bbc.debugger.step()
        print(f"  step -> PC=${bbc.cpu.pc:04X}")

    # Set a breakpoint at OSWRCH (&FFEE, "write a character"), then make the OS
    # produce output so we deterministically hit it. A breakpoint used as a
    # context manager is removed again on exit.
    bbc.run_until_or_timeout(lambda: screen_contains(bbc, ">"), emulated_seconds=3.0)
    bbc.keyboard.type("PRINT 42")  # each echoed character calls OSWRCH

    with bbc.debugger.breakpoint(0xFFEE):
        bbc.debugger.ensure_running()
        bbc.debugger.wait_for_stop()
    char = bbc.cpu.a
    glyph = chr(char) if 32 <= char < 127 else "?"
    print(f"broke in OSWRCH; character in A = ${char:02X} ({glyph!r})")

    print(f"total cycles executed: {bbc.debugger.cycle_count}")


if __name__ == "__main__":
    run(demo, description="Debugger control and CPU register access.")
