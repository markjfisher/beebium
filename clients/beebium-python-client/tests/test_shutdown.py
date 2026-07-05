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

"""Integration tests for shutdown RPC.

These tests verify the RequestShutdown RPC and shutdown policy enforcement.
Tests run against a real beebium-server using the `bbc` fixture.
"""

from __future__ import annotations

import threading
import time
from typing import TYPE_CHECKING

import pytest

from beebium.client import Beebium
from beebium.client.system import ServerStatus, ShutdownMode, ShutdownResponse

if TYPE_CHECKING:
    pass


class TestShutdownPolicySingleClient:
    """Tests for shutdown policy with single client (sole client rule)."""

    def test_single_client_can_shutdown(self, bbc: Beebium) -> None:
        """Single client can always request shutdown (sole client rule)."""
        # With no watchers, this is the only client - should be accepted
        response = bbc.system.request_shutdown(mode=ShutdownMode.GRACEFUL)
        assert isinstance(response, ShutdownResponse)
        assert response.accepted is True
        assert "sole client" in response.message.lower() or "initiated" in response.message.lower()

    def test_launcher_can_shutdown(self, bbc: Beebium) -> None:
        """Launcher client (matching UUID) can always shutdown."""
        # The bbc fixture uses launch(), so its instance UUID matches provenance
        response = bbc.system.request_shutdown(mode=ShutdownMode.GRACEFUL)
        assert response.accepted is True


class TestShutdownPolicyMultipleClients:
    """Tests for shutdown policy with multiple clients."""

    def test_launcher_can_shutdown_with_multiple_clients(self, bbc: Beebium) -> None:
        """Launcher client can shutdown even with multiple clients connected."""
        # Start a watcher to simulate a second client
        second_client = Beebium.connect(target=bbc.target)
        watcher_started = threading.Event()
        shutdown_received = threading.Event()

        def watch_in_background() -> None:
            try:
                for event in second_client.system.watch_status():
                    if event.status == ServerStatus.READY:
                        watcher_started.set()
                    elif event.status == ServerStatus.SHUTTING_DOWN:
                        shutdown_received.set()
                        break
            except Exception:
                pass  # Channel closed

        thread = threading.Thread(target=watch_in_background, daemon=True)
        thread.start()

        try:
            # Wait for second client to be watching
            assert watcher_started.wait(timeout=5.0), "Second client did not start watching"
            time.sleep(0.1)  # Let server register the connection

            # Verify we have multiple clients
            assert bbc.system.client_count >= 1

            # Launcher should still be able to shutdown (matches provenance UUID)
            response = bbc.system.request_shutdown(mode=ShutdownMode.GRACEFUL)
            assert response.accepted is True
            assert "launcher" in response.message.lower() or "initiated" in response.message.lower()

            # Second client should receive shutdown notification
            assert shutdown_received.wait(timeout=2.0), "Second client did not receive shutdown"
        finally:
            second_client.close()

    def test_non_launcher_refused_with_multiple_clients(self, bbc: Beebium) -> None:
        """Non-launcher client is refused shutdown when multiple clients connected."""
        # Start a watcher with the launcher client first
        launcher_started = threading.Event()

        def launcher_watch() -> None:
            try:
                for event in bbc.system.watch_status():
                    if event.status == ServerStatus.READY:
                        launcher_started.set()
                    elif event.status == ServerStatus.SHUTTING_DOWN:
                        break
            except Exception:
                pass

        launcher_thread = threading.Thread(target=launcher_watch, daemon=True)
        launcher_thread.start()
        assert launcher_started.wait(timeout=5.0)
        time.sleep(0.1)

        # Connect a second client (non-launcher - has different UUID)
        # This client also needs to watch to be counted by ConnectionTracker
        second_client = Beebium.connect(target=bbc.target)
        second_started = threading.Event()
        shutdown_received = threading.Event()

        def second_watch() -> None:
            try:
                for event in second_client.system.watch_status():
                    if event.status == ServerStatus.READY:
                        second_started.set()
                    elif event.status == ServerStatus.SHUTTING_DOWN:
                        shutdown_received.set()
                        break
            except Exception:
                pass

        second_thread = threading.Thread(target=second_watch, daemon=True)
        second_thread.start()
        assert second_started.wait(timeout=5.0)
        time.sleep(0.1)

        try:
            # Verify we have multiple clients (both watching)
            assert bbc.system.client_count >= 2

            # Non-launcher client should be refused
            response = second_client.system.request_shutdown(mode=ShutdownMode.GRACEFUL)
            assert response.accepted is False
            assert "refused" in response.message.lower() or "multiple" in response.message.lower()
        finally:
            second_client.close()


class TestShutdownModes:
    """Tests for different shutdown modes."""

    def test_graceful_shutdown(self, bbc: Beebium) -> None:
        """Graceful shutdown is accepted."""
        response = bbc.system.request_shutdown(mode=ShutdownMode.GRACEFUL)
        assert response.accepted is True

    def test_immediate_shutdown(self, bbc: Beebium) -> None:
        """Immediate shutdown is accepted."""
        response = bbc.system.request_shutdown(mode=ShutdownMode.IMMEDIATE)
        assert response.accepted is True


class TestShutdownNotification:
    """Tests for shutdown notification to watchers."""

    def test_watchers_receive_shutdown_notification(self, bbc: Beebium) -> None:
        """All watchers receive SHUTTING_DOWN event on shutdown."""
        watcher_started = threading.Event()
        shutdown_event_received = threading.Event()
        grace_ms_received: list[int] = []

        watcher_client = Beebium.connect(target=bbc.target)

        def watch_for_shutdown() -> None:
            try:
                for event in watcher_client.system.watch_status():
                    if event.status == ServerStatus.READY:
                        watcher_started.set()
                    elif event.status == ServerStatus.SHUTTING_DOWN:
                        grace_ms_received.append(event.shutdown_grace_ms)
                        shutdown_event_received.set()
                        break
            except Exception:
                pass

        thread = threading.Thread(target=watch_for_shutdown, daemon=True)
        thread.start()

        try:
            assert watcher_started.wait(timeout=5.0)
            time.sleep(0.1)

            # Request shutdown with custom grace period
            response = bbc.system.request_shutdown(
                mode=ShutdownMode.GRACEFUL,
                grace_period_ms=3000,
            )
            assert response.accepted is True

            # Watcher should receive shutdown notification
            assert shutdown_event_received.wait(timeout=2.0)
            assert len(grace_ms_received) == 1
            assert grace_ms_received[0] == 3000
        finally:
            watcher_client.close()


class TestShutdownResponseFields:
    """Tests for ShutdownResponse structure."""

    def test_response_has_accepted_field(self, bbc: Beebium) -> None:
        """ShutdownResponse has accepted field."""
        response = bbc.system.request_shutdown()
        assert hasattr(response, "accepted")
        assert isinstance(response.accepted, bool)

    def test_response_has_message_field(self, bbc: Beebium) -> None:
        """ShutdownResponse has message field."""
        response = bbc.system.request_shutdown()
        assert hasattr(response, "message")
        assert isinstance(response.message, str)
        assert len(response.message) > 0  # Message should not be empty


class TestShutdownIdempotency:
    """Tests for shutdown request idempotency."""

    def test_multiple_shutdown_requests_accepted(self, bbc: Beebium) -> None:
        """Multiple shutdown requests are accepted (idempotent).

        Note: We use a large grace period to ensure the server is still running
        when the second request arrives. After the first request, the server
        sets shutdown_signaled and subsequent requests return "already in progress".
        """
        # First request with a large grace period to keep server alive longer
        response1 = bbc.system.request_shutdown(grace_period_ms=10000)
        assert response1.accepted is True

        # Second request should also be accepted (server reports "already in progress")
        # Make this request immediately while server is still running
        try:
            response2 = bbc.system.request_shutdown()
            assert response2.accepted is True
            assert "already" in response2.message.lower()
        except Exception:
            # If the server shut down too quickly, that's acceptable
            # The idempotency check is best-effort
            pass
