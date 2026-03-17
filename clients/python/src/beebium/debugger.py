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

"""Debugger control interface for the beebium client."""

from __future__ import annotations

from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import dataclass

from beebium._proto import debugger_pb2, debugger_pb2_grpc
from beebium.exceptions import DebuggerError


@dataclass(frozen=True)
class ExecutionState:
    """Current execution state of the emulator."""

    is_running: bool
    cycle_count: int
    halt_reason: str
    sequence: int


@dataclass(frozen=True)
class Breakpoint:
    """A breakpoint set in the emulator."""

    id: int
    address: int
    end_address: int = 0
    condition: str = ""
    stop_counterpart: bool = False
    hit_count: int = 0


@dataclass(frozen=True)
class WatchpointInfo:
    """A watchpoint set in the emulator."""

    id: int
    start_address: int
    end_address: int
    type: str  # "read", "write", or "both"
    condition: str = ""
    stop_counterpart: bool = False
    hit_count: int = 0


@dataclass(frozen=True)
class WatchpointHitInfo:
    """Details of a watchpoint hit."""

    watchpoint_id: int
    address: int
    value: int
    is_write: bool


@dataclass(frozen=True)
class StepResult:
    """Result of a step operation."""

    success: bool
    error: str
    instructions_executed: int
    cycles_executed: int
    state: ExecutionState


@dataclass(frozen=True)
class ExecutionStateEvent:
    """An execution state change event from the server."""

    reason: int
    state: ExecutionState
    message: str


def _to_execution_state(proto_state) -> ExecutionState:
    return ExecutionState(
        is_running=proto_state.is_running,
        cycle_count=proto_state.cycle_count,
        halt_reason=proto_state.halt_reason,
        sequence=proto_state.sequence,
    )


def _to_execution_state_event(proto_event) -> ExecutionStateEvent:
    return ExecutionStateEvent(
        reason=proto_event.reason,
        state=_to_execution_state(proto_event.state),
        message=proto_event.message,
    )


class Debugger:
    """Debugger control interface.

    Provides execution control, stepping, and breakpoint management.

    Usage:
        # Stop execution
        state = bbc.debugger.stop()

        # Step through instructions
        result = bbc.debugger.step(10)

        # Set breakpoint and run until hit
        bp_id = bbc.debugger.add_breakpoint(0xC000)
        state = bbc.debugger.run_until(0xC000)
    """

    def __init__(self, stub: debugger_pb2_grpc.DebuggerControlStub):
        """Create a debugger interface.

        Args:
            stub: The gRPC stub for the DebuggerControl service.
        """
        self._stub = stub

    # Execution control

    def get_state(self) -> ExecutionState:
        """Get current execution state."""
        response = self._stub.GetState(debugger_pb2.Empty())
        return ExecutionState(
            is_running=response.is_running,
            cycle_count=response.cycle_count,
            halt_reason=response.halt_reason,
            sequence=response.sequence,
        )

    def run(self) -> None:
        """Resume execution.

        Raises:
            DebuggerError: If the operation fails.
        """
        response = self._stub.Run(debugger_pb2.Empty())
        if not response.success:
            raise DebuggerError(f"Failed to start execution: {response.error}")

    def stop(self) -> ExecutionState:
        """Pause execution.

        Returns:
            The execution state after stopping.
        """
        response = self._stub.Stop(debugger_pb2.Empty())
        if not response.success:
            raise DebuggerError("Failed to stop execution")
        return ExecutionState(
            is_running=response.state.is_running,
            cycle_count=response.state.cycle_count,
            halt_reason=response.state.halt_reason,
            sequence=response.state.sequence,
        )

    def reset(self) -> None:
        """Reset the machine.

        Raises:
            DebuggerError: If the reset fails.
        """
        response = self._stub.Reset(debugger_pb2.Empty())
        if not response.success:
            raise DebuggerError("Failed to reset machine")

    def step(self, count: int = 1) -> StepResult:
        """Step by instruction(s).

        Machine must be stopped first.

        Args:
            count: Number of instructions to execute.

        Returns:
            The step result including state after stepping.

        Raises:
            DebuggerError: If machine is running or step fails.
        """
        request = debugger_pb2.StepRequest(count=count)
        response = self._stub.StepInstruction(request)
        if not response.success:
            raise DebuggerError(f"Step failed: {response.error}")
        return StepResult(
            success=response.success,
            error=response.error,
            instructions_executed=response.instructions_executed,
            cycles_executed=response.cycles_executed,
            state=ExecutionState(
                is_running=response.state.is_running,
                cycle_count=response.state.cycle_count,
                halt_reason=response.state.halt_reason,
                sequence=response.state.sequence,
            ),
        )

    def step_cycles(self, count: int = 1) -> StepResult:
        """Step by CPU cycle(s).

        Machine must be stopped first.

        Args:
            count: Number of cycles to execute.

        Returns:
            The step result including state after stepping.

        Raises:
            DebuggerError: If machine is running or step fails.
        """
        request = debugger_pb2.StepRequest(count=count)
        response = self._stub.StepCycle(request)
        if not response.success:
            raise DebuggerError(f"Step cycles failed: {response.error}")
        return StepResult(
            success=response.success,
            error=response.error,
            instructions_executed=response.instructions_executed,
            cycles_executed=response.cycles_executed,
            state=ExecutionState(
                is_running=response.state.is_running,
                cycle_count=response.state.cycle_count,
                halt_reason=response.state.halt_reason,
                sequence=response.state.sequence,
            ),
        )

    # Convenience properties

    @property
    def is_running(self) -> bool:
        """True if the machine is currently running."""
        return self.get_state().is_running

    @property
    def is_stopped(self) -> bool:
        """True if the machine is currently stopped/paused."""
        return not self.is_running

    @property
    def cycle_count(self) -> int:
        """Current CPU cycle count."""
        return self.get_state().cycle_count

    def ensure_running(self) -> None:
        """Resume execution if not already running."""
        if not self.is_running:
            self.run()

    def ensure_stopped(self) -> None:
        """Pause execution if not already stopped."""
        if self.is_running:
            self.stop()

    @contextmanager
    def running(self):
        """Context manager that ensures the emulator is running on entry
        and restores the previous execution state on exit.

        Usage::

            with bbc.debugger.running():
                # emulator is executing
                ...
            # emulator restored to whatever state it was in before
        """
        was_running = self.is_running
        self.ensure_running()
        try:
            yield
        finally:
            if was_running:
                self.ensure_running()
            else:
                self.ensure_stopped()

    # Breakpoints

    def add_breakpoint(
        self,
        address: int,
        *,
        end_address: int = 0,
        condition: str = "",
        stop_counterpart: bool = False,
    ) -> int:
        """Add a breakpoint on an address range.

        Args:
            address: Start address (inclusive). For a single-address breakpoint.
            end_address: End address (exclusive). 0 means address+1 (single address).
                Use 0x10000 for a full-range breakpoint that fires at every
                instruction boundary (useful with a cycle condition).
            condition: Optional expression evaluated on hit. Empty = unconditional.
                Use ``hits`` for hit-count logic, e.g. ``"hits == 5"``.
                Use ``"cycles >= N"`` with a full-range breakpoint for cycle budgets.
            stop_counterpart: Signal the other processor to stop.

        Returns:
            The breakpoint ID.

        Raises:
            DebuggerError: If the breakpoint cannot be added.
        """
        request = debugger_pb2.AddBreakpointRequest(
            start_address=address,
            end_address=end_address,
            condition=condition,
            stop_counterpart=stop_counterpart,
        )
        response = self._stub.AddBreakpoint(request)
        if not response.success:
            raise DebuggerError(f"Failed to add breakpoint at ${address:04X}")
        return response.id

    @contextmanager
    def breakpoint(
        self,
        address: int,
        *,
        end_address: int = 0,
        condition: str = "",
        stop_counterpart: bool = False,
    ):
        """Context manager that adds a breakpoint and removes it on exit.

        Usage::

            with bbc.debugger.breakpoint(0xC000, condition="A == 0x42") as bp_id:
                bbc.debugger.run()
                event = bbc.debugger.wait_for_stop()
        """
        bp_id = self.add_breakpoint(
            address, end_address=end_address,
            condition=condition, stop_counterpart=stop_counterpart,
        )
        try:
            yield bp_id
        finally:
            self.remove_breakpoint(bp_id)

    def remove_breakpoint(self, breakpoint_id: int) -> bool:
        """Remove a breakpoint by ID.

        Args:
            breakpoint_id: The breakpoint ID returned by add_breakpoint().

        Returns:
            True if removed, False if not found.
        """
        request = debugger_pb2.RemoveBreakpointRequest(id=breakpoint_id)
        response = self._stub.RemoveBreakpoint(request)
        return response.success

    def list_breakpoints(self) -> list[Breakpoint]:
        """List all active breakpoints.

        Returns:
            List of active breakpoints.
        """
        response = self._stub.ListBreakpoints(debugger_pb2.Empty())
        return [
            Breakpoint(
                id=bp.id,
                address=bp.start_address,
                end_address=bp.end_address,
                condition=bp.condition,
                stop_counterpart=bp.stop_counterpart,
                hit_count=bp.hit_count,
            ) for bp in response.breakpoints
        ]

    def clear_breakpoints(self) -> int:
        """Remove all breakpoints.

        Returns:
            The number of breakpoints removed.
        """
        response = self._stub.ClearBreakpoints(debugger_pb2.Empty())
        return response.count_removed

    # Watchpoints

    def add_watchpoint(
        self,
        start_address: int,
        end_address: int,
        type: str = "both",
        *,
        condition: str = "",
        stop_counterpart: bool = False,
    ) -> int:
        """Add a watchpoint on an address range.

        Args:
            start_address: Start of range (inclusive).
            end_address: End of range (exclusive).
            type: "read", "write", or "both".
            condition: Optional expression evaluated on hit. Empty = unconditional.
                Use ``hits`` for hit-count logic, e.g. ``"hits == 5"``.
                Use ``"false"`` for recording-only (never stops).
            stop_counterpart: Signal the other processor to stop.

        Returns:
            The watchpoint ID.
        """
        type_map = {
            "read": debugger_pb2.WATCHPOINT_READ,
            "write": debugger_pb2.WATCHPOINT_WRITE,
            "both": debugger_pb2.WATCHPOINT_BOTH,
        }
        request = debugger_pb2.AddWatchpointRequest(
            start_address=start_address,
            end_address=end_address,
            type=type_map.get(type, debugger_pb2.WATCHPOINT_BOTH),
            condition=condition,
            stop_counterpart=stop_counterpart,
        )
        response = self._stub.AddWatchpoint(request)
        if not response.success:
            raise DebuggerError(
                f"Failed to add watchpoint at ${start_address:04X}-${end_address:04X}"
            )
        return response.id

    @contextmanager
    def watchpoint(
        self,
        start_address: int,
        end_address: int,
        type: str = "both",
        *,
        condition: str = "",
        stop_counterpart: bool = False,
    ):
        """Context manager that adds a watchpoint and removes it on exit.

        Usage::

            with bbc.debugger.watchpoint(0xFE00, 0xFF00, "write") as wp_id:
                bbc.debugger.run()
                event = bbc.debugger.wait_for_stop()
        """
        wp_id = self.add_watchpoint(
            start_address, end_address, type,
            condition=condition, stop_counterpart=stop_counterpart,
        )
        try:
            yield wp_id
        finally:
            self.remove_watchpoint(wp_id)

    def remove_watchpoint(self, watchpoint_id: int) -> bool:
        """Remove a watchpoint by ID."""
        request = debugger_pb2.RemoveWatchpointRequest(id=watchpoint_id)
        response = self._stub.RemoveWatchpoint(request)
        return response.success

    def list_watchpoints(self) -> list[WatchpointInfo]:
        """List all active watchpoints."""
        response = self._stub.ListWatchpoints(debugger_pb2.Empty())
        type_map = {
            debugger_pb2.WATCHPOINT_READ: "read",
            debugger_pb2.WATCHPOINT_WRITE: "write",
            debugger_pb2.WATCHPOINT_BOTH: "both",
        }
        return [
            WatchpointInfo(
                id=wp.id,
                start_address=wp.start_address,
                end_address=wp.end_address,
                type=type_map.get(wp.type, "both"),
                condition=wp.condition,
                stop_counterpart=wp.stop_counterpart,
                hit_count=wp.hit_count,
            )
            for wp in response.watchpoints
        ]

    def clear_watchpoints(self) -> int:
        """Remove all watchpoints. Returns the count removed."""
        response = self._stub.ClearWatchpoints(debugger_pb2.Empty())
        return response.count_removed

    # Event streaming

    def watch_execution_state(self) -> Iterator[ExecutionStateEvent]:
        """Stream execution state change events from the server.

        The server sends the current state immediately, then pushes events
        whenever the execution state changes (breakpoint hit, manual stop,
        run resumed, etc.).

        Yields:
            ExecutionStateEvent for each state change.
        """
        request = debugger_pb2.WatchExecutionStateRequest()
        for response in self._stub.WatchExecutionState(request):
            yield _to_execution_state_event(response)

    def wait_for_stop(self) -> ExecutionStateEvent:
        """Wait for the machine to stop executing.

        Subscribes to the execution state stream and waits for a transition
        to the stopped state. If the machine is already stopped when this is
        called, it waits until the machine runs and then stops again.

        Returns:
            The event that caused the machine to stop.

        Raises:
            DebuggerError: If the stream ends without a stop event.
        """
        saw_running = False
        for event in self.watch_execution_state():
            if event.state.is_running:
                saw_running = True
            elif saw_running:
                return event
        raise DebuggerError("Execution state stream ended without a stop event")

    # Run-until helpers

    def run_until(
        self, address: int, timeout_cycles: int | None = None
    ) -> ExecutionState:
        """Run until the PC reaches the given address.

        Sets a temporary breakpoint, subscribes to execution state events,
        starts execution, and waits for the machine to stop.

        Args:
            address: The address to run until.
            timeout_cycles: Maximum cycles to run (not currently implemented).

        Returns:
            The execution state after hitting the address.

        Raises:
            DebuggerError: If the breakpoint cannot be set.
        """
        bp_id = self.add_breakpoint(address)
        try:
            stream = self._stub.WatchExecutionState(
                debugger_pb2.WatchExecutionStateRequest()
            )
            # Consume initial state
            next(stream)
            # Start execution
            self.run()
            # Wait for running -> stopped transition (skip stale stopped events)
            saw_running = False
            for event in stream:
                if event.state.is_running:
                    saw_running = True
                elif saw_running:
                    stream.cancel()
                    return _to_execution_state(event.state)
            raise DebuggerError("Stream ended without stop")
        finally:
            self.remove_breakpoint(bp_id)
