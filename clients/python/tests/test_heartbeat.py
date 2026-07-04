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

"""Server liveness heartbeat over the WatchServerStatus stream.

A silently-unreachable server (network partition, or the process frozen) leaves
the status stream open but quiet: no shutdown event arrives and the socket is
never reset, so a client cannot tell the server has gone away. The fix is a
periodic heartbeat on WatchServerStatus, so an idle-but-healthy connection still
produces regular events and their absence is a detectable signal.

This test asserts that cadence. It fails against a server that only emits the
initial READY and then goes silent.
"""

from __future__ import annotations

import threading
import time

import pytest

from beebium.client.system import ServerStatus


@pytest.mark.timeout(30)
def test_status_stream_emits_periodic_heartbeat(bbc):
    """An idle WatchServerStatus stream delivers regular liveness ticks."""
    events: list[tuple[float, object]] = []

    def collect() -> None:
        # Blocking generator; runs until the server stops at fixture teardown.
        for event in bbc.system.watch_status():
            events.append((time.monotonic(), event.status))

    thread = threading.Thread(target=collect, daemon=True)
    thread.start()

    # Watch an otherwise-idle server for a window spanning several heartbeats.
    window = 8.0
    time.sleep(window)

    # Without a heartbeat the server sends a single READY and then nothing, so
    # we would see exactly one event. A heartbeat every few seconds yields
    # several. Require enough to prove periodicity, not a one-off.
    assert len(events) >= 3, (
        f"expected periodic heartbeats over {window:.0f}s, got {len(events)} "
        f"event(s): {[s for _, s in events]}"
    )

    # And prove they are spread out (a genuine cadence), not a startup burst.
    stamps = [t for t, _ in events]
    gaps = [b - a for a, b in zip(stamps, stamps[1:])]
    assert max(gaps) <= 5.0, f"heartbeat gap too large: gaps={gaps}"

    # The periodic ticks are heartbeats specifically (not, say, repeated READY).
    statuses = [s for _, s in events]
    assert statuses.count(ServerStatus.HEARTBEAT) >= 2, (
        f"expected HEARTBEAT ticks, got {statuses}"
    )
