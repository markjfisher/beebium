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

"""Integration tests for client connection tracking.

These tests run against a real beebium-server using the `bbc` fixture.
Multi-client tests connect additional clients using Beebium.connect(target=bbc.target).
"""

from __future__ import annotations

import threading
import time
from concurrent.futures import ThreadPoolExecutor
from typing import TYPE_CHECKING

import pytest

from beebium.client import Beebium
from beebium.client.system import ServerStatus

if TYPE_CHECKING:
    pass


class TestClientCountWithoutWatchers:
    """Tests for client count when no watch_status streams are active."""

    def test_client_count_zero_without_watchers(self, bbc: Beebium) -> None:
        """Client count is 0 when no active watch_status streams."""
        # Just connecting does not increment the count - only active watchers do
        assert bbc.system.client_count == 0


class TestSingleClientWatching:
    """Tests for single client watch_status stream."""

    def test_watching_increments_count(self, bbc: Beebium) -> None:
        """Starting a watch_status stream increments the client count."""
        assert bbc.system.client_count == 0

        # Use a separate client for watching, so we can close it to cancel the stream
        watcher_client = Beebium.connect(target=bbc.target)
        watcher_started = threading.Event()

        def watch_in_background() -> None:
            try:
                for event in watcher_client.system.watch_status():
                    if event.status == ServerStatus.READY:
                        watcher_started.set()
                    # Loop will exit when channel is closed
            except Exception:
                pass  # Channel closed - expected

        thread = threading.Thread(target=watch_in_background, daemon=True)
        thread.start()

        try:
            # Wait for watcher to receive READY event
            assert watcher_started.wait(timeout=5.0), "Watcher did not start in time"

            # Give server time to register the connection
            time.sleep(0.1)

            # Verify count is now 1
            assert bbc.system.client_count == 1
        finally:
            # Close the watcher client to cancel the stream
            watcher_client.close()

        # Give server time to notice the disconnection
        # Server polls every 100ms for client cancellation
        time.sleep(0.3)

        # Count should be back to 0
        assert bbc.system.client_count == 0


class TestMultipleClients:
    """Tests for multiple client connections."""

    def test_multiple_clients_increment_count(self, bbc: Beebium) -> None:
        """Multiple clients with watch_status streams increment the count."""
        assert bbc.system.client_count == 0

        watchers_started = [threading.Event() for _ in range(2)]
        watcher_clients = [
            Beebium.connect(target=bbc.target),
            Beebium.connect(target=bbc.target),
        ]

        def watch_for_client(client: Beebium, index: int) -> None:
            try:
                for event in client.system.watch_status():
                    if event.status == ServerStatus.READY:
                        watchers_started[index].set()
            except Exception:
                pass  # Channel closed - expected

        threads = []
        try:
            # Start first watcher
            t1 = threading.Thread(
                target=watch_for_client, args=(watcher_clients[0], 0), daemon=True
            )
            t1.start()
            threads.append(t1)
            assert watchers_started[0].wait(timeout=5.0)
            time.sleep(0.1)
            assert bbc.system.client_count == 1

            # Start second watcher
            t2 = threading.Thread(
                target=watch_for_client, args=(watcher_clients[1], 1), daemon=True
            )
            t2.start()
            threads.append(t2)
            assert watchers_started[1].wait(timeout=5.0)
            time.sleep(0.1)

            # Both watching - count should be 2
            assert bbc.system.client_count == 2
        finally:
            # Close all watcher clients to cancel the streams
            for client in watcher_clients:
                client.close()

        # Server polls every 100ms for client cancellation
        time.sleep(0.3)

        # All watchers stopped - count should be 0
        assert bbc.system.client_count == 0


class TestRapidConnectDisconnect:
    """Tests for rapid connection/disconnection cycles."""

    def test_rapid_connect_disconnect_maintains_accuracy(self, bbc: Beebium) -> None:
        """Rapid connect/watch/disconnect cycles maintain accurate count."""
        assert bbc.system.client_count == 0

        num_cycles = 10
        num_threads = 3
        errors: list[str] = []

        def cycle_connect_disconnect(thread_id: int) -> None:
            for i in range(num_cycles):
                try:
                    client = Beebium.connect(target=bbc.target, timeout=5.0)
                    try:
                        # Start watching
                        ready_received = False
                        for event in client.system.watch_status():
                            if event.status == ServerStatus.READY:
                                ready_received = True
                                break  # Stop watching immediately after READY
                        if not ready_received:
                            errors.append(f"Thread {thread_id} cycle {i}: No READY event")
                    finally:
                        client.close()
                except Exception as e:
                    errors.append(f"Thread {thread_id} cycle {i}: {e}")

        # Run cycles concurrently
        with ThreadPoolExecutor(max_workers=num_threads) as executor:
            futures = [executor.submit(cycle_connect_disconnect, i) for i in range(num_threads)]
            for f in futures:
                f.result()  # Wait and raise any exceptions

        # Give time for all connections to fully close
        # Server polls every 100ms for client cancellation
        time.sleep(0.3)

        # Verify no errors occurred
        assert not errors, f"Errors during rapid cycles: {errors}"

        # Final count should be 0
        assert bbc.system.client_count == 0


class TestCountNeverNegative:
    """Tests to ensure count never goes negative."""

    def test_count_never_negative(self, bbc: Beebium) -> None:
        """Client count is always >= 0."""
        # Without any watchers
        assert bbc.system.client_count >= 0

        # After various operations
        for _ in range(5):
            count = bbc.system.client_count
            assert count >= 0, f"Count went negative: {count}"

    def test_count_stays_zero_with_no_watchers(self, bbc: Beebium) -> None:
        """Client count remains 0 when only connecting without watching."""
        # Multiple connect/close cycles without watching
        for _ in range(5):
            client = Beebium.connect(target=bbc.target)
            # Access system info but don't watch
            _ = client.system.identity
            client.close()

        # Count should still be 0 (connecting alone doesn't increment)
        assert bbc.system.client_count == 0
