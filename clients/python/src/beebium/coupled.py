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

"""Coupled system abstraction for host-parasite debugging.

Manages both host and parasite as a single unit, hiding the complexity
of bus-stretch cancellation, pacing asymmetry, and stop ordering.
"""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Callable
    from beebium.client import Beebium


class CoupledSystem:
    """Manages a host and parasite as a single unit.

    Both processors are run and stopped together. Predicates for
    ``run_until`` are evaluated periodically via peek (side-effect-free)
    without stopping either processor.

    Usage::

        system = CoupledSystem(host, parasite)
        system.run()
        # ...
        system.stop()

    Or with auto-discovery::

        system = CoupledSystem.from_host(bbc)
    """

    def __init__(self, host: Beebium, parasite: Beebium, *, owns_parasite: bool = False):
        """Create a coupled system from existing host and parasite clients.

        Args:
            host: The host BBC Micro client.
            parasite: The parasite (second processor) client.
            owns_parasite: If True, close the parasite on ``close()``.
        """
        self._host = host
        self._parasite = parasite
        self._owns_parasite = owns_parasite

    @classmethod
    def from_host(cls, host: Beebium, timeout: float = 5.0) -> CoupledSystem:
        """Create a coupled system by discovering the parasite from the host.

        Args:
            host: The host BBC Micro client.
            timeout: Connection timeout for the parasite.
        """
        parasite = host.connect_parasite(timeout=timeout)
        return cls(host, parasite, owns_parasite=True)

    @property
    def host(self) -> Beebium:
        return self._host

    @property
    def parasite(self) -> Beebium:
        return self._parasite

    def run(self) -> None:
        """Run both processors."""
        self._host.debugger.ensure_running()
        self._parasite.debugger.ensure_running()

    def stop(self) -> None:
        """Stop both processors (bus-stretch safe).

        Stopping either side breaks the other out of any bus-stretch
        spin-wait via the bus_stretch_cancel flag in TubeShared.
        """
        self._host.debugger.ensure_stopped()
        self._parasite.debugger.ensure_stopped()

    def run_until(
        self,
        predicate: Callable[[], bool],
        emulated_seconds: float,
        *,
        poll_interval: float = 0.02,
    ) -> bool:
        """Run both processors until predicate returns True or the budget expires.

        Both processors advance at their natural rates. The predicate
        is evaluated periodically via peek (side-effect-free) without
        stopping either processor. When the predicate fires or the
        budget is exceeded, both are stopped.

        Args:
            predicate: Callable returning True when the condition is met.
            emulated_seconds: Maximum BBC-time seconds to run.
            poll_interval: Real-time seconds between predicate checks.

        Returns:
            True if the predicate was satisfied, False on timeout.
        """
        clock_hz = self._host.system.clock_speed_hz or 2_000_000
        cycle_budget = int(emulated_seconds * clock_hz)
        start_cycles = self._host.debugger.cycle_count
        target_cycles = start_cycles + cycle_budget
        min_cycles_before_check = clock_hz  # 1 emulated second

        try:
            self.run()

            while True:
                time.sleep(poll_interval)
                current = self._host.debugger.cycle_count

                if current >= target_cycles:
                    self.stop()
                    return predicate()

                if current - start_cycles >= min_cycles_before_check:
                    if predicate():
                        self.stop()
                        return True
        except Exception:
            self.stop()
            raise

    def run_for(self, emulated_seconds: float) -> None:
        """Run both processors for the given emulated time.

        Uses a full-range breakpoint with a cycle condition on the host,
        with stop_counterpart to stop the parasite too. No polling.

        Args:
            emulated_seconds: BBC-time seconds to run.
        """
        clock_hz = self._host.system.clock_speed_hz or 2_000_000
        cycle_budget = int(emulated_seconds * clock_hz)
        target_cycles = self._host.debugger.cycle_count + cycle_budget

        bp_id = self._host.debugger.add_breakpoint(
            0x0000,
            end_address=0x10000,
            condition=f"cycles >= {target_cycles}",
            stop_counterpart=True,
        )
        try:
            stream = self._host.debugger.watch_execution_state()
            next(stream)  # consume initial state
            self.run()
            for event in stream:
                if not event.state.is_running:
                    break
        finally:
            self._host.debugger.remove_breakpoint(bp_id)
            self.stop()

    def close(self) -> None:
        """Close the coupled system, stopping both processors.

        If the parasite was auto-discovered (``owns_parasite=True``),
        it is closed. The host is not closed.
        """
        self.stop()
        if self._owns_parasite:
            self._parasite.close()
