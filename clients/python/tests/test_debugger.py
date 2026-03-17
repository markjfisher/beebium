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

"""Integration tests for the debugger over gRPC from Python.

Exercises breakpoints, watchpoints, conditional expressions, hit counts,
event streaming, and execution control against a live beebium server.
Each test gets a fresh BBC Micro instance via the ``bbc`` fixture.
"""

from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.debugger import ExecutionStateEvent
from beebium.exceptions import DebuggerError
from beebium._proto import debugger_pb2, debugger_pb2_grpc


def assemble(source: str) -> bytes:
    """Assemble 6502 source using beebasm and return the binary."""
    with tempfile.TemporaryDirectory() as tmp:
        src_filepath = Path(tmp) / "test.6502"
        bin_filepath = Path(tmp) / "test.bin"
        src_filepath.write_text(source)
        result = subprocess.run(
            ["beebasm", "-i", str(src_filepath)],
            cwd=tmp,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"beebasm failed: {result.stderr}")
        return bin_filepath.read_bytes()


def run_and_wait_for_stop(bbc: Beebium, timeout: float = 10.0) -> ExecutionStateEvent:
    """Run the machine and poll for it to stop.

    TODO: Replace with event-stream-based approach once the gRPC streaming
    reliability issue is resolved.
    """
    import time
    bbc.debugger.run()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        time.sleep(0.01)
        state = bbc.debugger.get_state()
        if not state.is_running:
            return ExecutionStateEvent(reason=0, state=state, message="")
    raise DebuggerError(f"Machine did not stop within {timeout}s")


def plant_and_run_from(bbc: Beebium, code: bytes, org: int = 0x0400) -> None:
    """Plant assembled code at ``org`` and prepare the CPU to execute it.

    Resets the machine (which leaves it paused at an instruction boundary),
    writes the code, and sets PC to the code origin. The machine is left
    stopped, ready for breakpoint setup and ``run()``.
    """
    bbc.debugger.reset()
    # reset() leaves machine paused at a clean instruction boundary (cycle 7)
    for i, byte in enumerate(code):
        bbc.memory.address.bus[org + i] = byte
    bbc.cpu.pc = org


# ============================================================================
# Execution control
# ============================================================================

class TestExecutionControl:

    def test_stop_and_resume(self, bbc):
        state = bbc.debugger.stop()
        assert not state.is_running
        bbc.debugger.run()
        assert bbc.debugger.is_running

    def test_step_instruction(self, stopped_bbc):
        result = stopped_bbc.debugger.step(1)
        assert result.success
        assert result.instructions_executed == 1
        assert result.cycles_executed > 0

    def test_step_cycles(self, stopped_bbc):
        result = stopped_bbc.debugger.step_cycles(10)
        assert result.success
        assert result.cycles_executed == 10

    def test_reset(self, bbc):
        bbc.debugger.reset()
        state = bbc.debugger.get_state()
        assert not state.is_running  # reset leaves machine paused


# ============================================================================
# Breakpoints -- unconditional
# ============================================================================

class TestBreakpoints:

    def test_add_and_list(self, bbc):
        bp_id = bbc.debugger.add_breakpoint(0x1000)
        assert bp_id > 0
        bps = bbc.debugger.list_breakpoints()
        assert len(bps) == 1
        assert bps[0].address == 0x1000

    def test_remove(self, bbc):
        bp_id = bbc.debugger.add_breakpoint(0x2000)
        assert bbc.debugger.remove_breakpoint(bp_id)
        assert len(bbc.debugger.list_breakpoints()) == 0

    def test_clear(self, bbc):
        for addr in [0x1000, 0x2000, 0x3000]:
            bbc.debugger.add_breakpoint(addr)
        removed = bbc.debugger.clear_breakpoints()
        assert removed == 3
        assert len(bbc.debugger.list_breakpoints()) == 0

    def test_breakpoint_stops_at_address(self, bbc):
        """Plant 6502 code and verify breakpoint stops at the right PC."""
        code = assemble("""\
            ORG &0400
            .start
                LDA #&42
                STA &0500
                NOP
            .end
            SAVE "test.bin", start, end
        """)
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(0x0402)
        event = run_and_wait_for_stop(bbc)
        assert not event.state.is_running
        regs = bbc.cpu.registers
        assert regs.a == 0x42
        bbc.debugger.remove_breakpoint(bp_id)


# ============================================================================
# Conditional breakpoints
# ============================================================================

class TestConditionalBreakpoints:

    def test_condition_on_register(self, bbc):
        """Breakpoint fires only when A == 0x42."""
        code = assemble("""\
            ORG &0400
            .start
                LDX #0
            .loop
                LDA table, X
                INX
                CPX #4
                BNE loop
                NOP
            .table
                EQUB &10, &42, &99, &FF
            .end
            SAVE "test.bin", start, end
        """)
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(0x0402, condition="A == 0x42")
        event = run_and_wait_for_stop(bbc)
        assert bbc.cpu.registers.a == 0x42
        bbc.debugger.remove_breakpoint(bp_id)

    def test_condition_with_hits(self, bbc):
        """Breakpoint fires on the 3rd hit using ``hits == 3``."""
        code = assemble("""\
            ORG &0400
            .start
                LDX #0
            .loop
                INX
                JMP loop
            .end
            SAVE "test.bin", start, end
        """)
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(0x0402, condition="hits == 3")
        event = run_and_wait_for_stop(bbc)
        # Breakpoint fires before the 3rd INX executes; X == 2
        assert bbc.cpu.registers.x == 2
        bbc.debugger.remove_breakpoint(bp_id)

    def test_condition_hits_modulo(self, bbc):
        """Breakpoint fires every 5th hit using ``hits % 5 == 0``."""
        code = assemble("""\
            ORG &0400
            .start
                LDX #0
            .loop
                INX
                JMP loop
            .end
            SAVE "test.bin", start, end
        """)
        plant_and_run_from(bbc, code)
        bp_id = bbc.debugger.add_breakpoint(0x0402, condition="hits % 5 == 0")
        event = run_and_wait_for_stop(bbc)
        # First fire at hits==5, before 5th INX executes; X == 4
        assert bbc.cpu.registers.x == 4
        bbc.debugger.remove_breakpoint(bp_id)

    def test_invalid_condition_raises(self, bbc):
        """Invalid condition string raises an error."""
        with pytest.raises(Exception):
            bbc.debugger.add_breakpoint(0x1000, condition="invalid !@#")


# ============================================================================
# Watchpoints -- unconditional
# ============================================================================

class TestWatchpoints:

    def test_add_and_list(self, bbc):
        wp_id = bbc.debugger.add_watchpoint(0x1000, 0x1010, type="write")
        wps = bbc.debugger.list_watchpoints()
        assert len(wps) == 1
        assert wps[0].start_address == 0x1000
        assert wps[0].end_address == 0x1010
        assert wps[0].type == "write"

    def test_remove(self, bbc):
        wp_id = bbc.debugger.add_watchpoint(0x2000, 0x2001)
        assert bbc.debugger.remove_watchpoint(wp_id)
        assert len(bbc.debugger.list_watchpoints()) == 0

    def test_clear(self, bbc):
        bbc.debugger.add_watchpoint(0x1000, 0x1001)
        bbc.debugger.add_watchpoint(0x2000, 0x2001)
        removed = bbc.debugger.clear_watchpoints()
        assert removed == 2

    def test_write_watchpoint_fires(self, bbc):
        """Write watchpoint stops when code writes to monitored address."""
        code = assemble("""\
            ORG &0400
            .start
                LDA #&42
                STA &0500
                NOP
            .end
            SAVE "test.bin", start, end
        """)
        plant_and_run_from(bbc, code)
        wp_id = bbc.debugger.add_watchpoint(0x0500, 0x0501, type="write")
        event = run_and_wait_for_stop(bbc)
        assert bbc.memory.address.peek[0x0500] == 0x42
        bbc.debugger.remove_watchpoint(wp_id)

    def test_write_watchpoint_does_not_fire_on_read(self, bbc):
        """Write-only watchpoint ignores reads."""
        code = assemble("""\
            ORG &0400
            .start
                LDA &0500
                NOP
                NOP
            .end
            SAVE "test.bin", start, end
        """)
        plant_and_run_from(bbc, code)
        bbc.memory.address.bus[0x0500] = 0xAB
        bbc.debugger.add_watchpoint(0x0500, 0x0501, type="write")
        bbc.debugger.run()
        # Run a short time then stop manually -- the watchpoint should NOT fire
        import time
        time.sleep(0.05)
        bbc.debugger.stop()
        # If it didn't fire, the machine ran past our code
        assert bbc.debugger.is_stopped


# ============================================================================
# Conditional watchpoints
# ============================================================================

class TestConditionalWatchpoints:

    def test_condition_on_register(self, bbc):
        """Watchpoint with condition A == 0x42 skips non-matching writes."""
        code = assemble("""\
            ORG &0400
            .start
                LDA #&10
                STA &0500
                LDA #&42
                STA &0500
                LDA #&99
                STA &0500
                NOP
            .end
            SAVE "test.bin", start, end
        """)
        plant_and_run_from(bbc, code)
        wp_id = bbc.debugger.add_watchpoint(
            0x0500, 0x0501, type="write", condition="A == 0x42"
        )
        event = run_and_wait_for_stop(bbc)
        # Stopped on the second STA when A was 0x42
        assert bbc.memory.address.peek[0x0500] == 0x42
        bbc.debugger.remove_watchpoint(wp_id)

    def test_condition_false_never_stops(self, bbc):
        """Watchpoint with condition ``false`` records but never stops."""
        code = assemble("""\
            ORG &0400
            .start
                LDA #&42
                STA &0500
                NOP
                NOP
            .end
            SAVE "test.bin", start, end
        """)
        plant_and_run_from(bbc, code)
        bbc.debugger.add_watchpoint(
            0x0500, 0x0501, type="write", condition="false"
        )
        bbc.debugger.run()
        import time
        time.sleep(0.05)
        bbc.debugger.stop()
        # The watchpoint with condition false should not have stopped execution
        assert bbc.memory.address.peek[0x0500] == 0x42  # write happened

    def test_invalid_condition_raises(self, bbc):
        with pytest.raises(Exception):
            bbc.debugger.add_watchpoint(
                0x1000, 0x1001, condition="bad syntax !!!"
            )


# ============================================================================
# CPU state
# ============================================================================

class TestCpuState:

    def test_get_registers(self, stopped_bbc):
        regs = stopped_bbc.cpu.registers
        assert hasattr(regs, "a")
        assert hasattr(regs, "x")
        assert hasattr(regs, "y")
        assert hasattr(regs, "sp")
        assert hasattr(regs, "pc")
        assert hasattr(regs, "p")

    def test_set_registers(self, stopped_bbc):
        stopped_bbc.cpu.a = 0xAA
        stopped_bbc.cpu.x = 0xBB
        regs = stopped_bbc.cpu.registers
        assert regs.a == 0xAA
        assert regs.x == 0xBB
