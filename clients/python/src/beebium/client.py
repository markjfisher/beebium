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

"""Main Beebium client class."""

from __future__ import annotations

import contextlib
import uuid
from collections.abc import Iterator
from pathlib import Path

import grpc

import beebium
from beebium.basic import Basic
from beebium.connection import Connection
from beebium.exceptions import BeebiumError, ConnectionError as BeebiumConnectionError
from beebium.cpu import CPU
from beebium.crtc import Crtc
from beebium.debugger import Debugger
from beebium.disc import Disc
from beebium.econet import Econet
from beebium.keyboard import Keyboard
from beebium.latch import AddressableLatch
from beebium.memory import Memory
from beebium.server import ServerProcess
from beebium.sound import Sound
from beebium.system import System
from beebium.tube import Tube
from beebium.tube_ula import TubeUlaInspection
from beebium.via import Via, ViaId
from beebium.video import Video
from beebium.video_ula import VideoUla


class Beebium:
    """Main client for interacting with a beebium emulator instance.

    Can either connect to an existing server or manage its own server process.

    Usage:
        # Connect to existing server
        with Beebium.connect() as bbc:
            bbc.debugger.stop()
            bbc.memory[0x1000] = 0x42

        # Start and manage server
        with Beebium.launch(mos_filepath="/path/to/acorn-mos_1_20.rom") as bbc:
            bbc.keyboard.type("PRINT 42")
            bbc.keyboard.press_return()
    """

    def __init__(
        self,
        connection: Connection,
        server: ServerProcess | None = None,
        instance_uuid: str | None = None,
    ):
        """Create a Beebium client.

        Use the class methods connect() or launch() instead of this constructor.

        Args:
            connection: The gRPC connection to the server.
            server: Optional server process being managed.
            instance_uuid: Optional client instance UUID for shutdown authorization.
                For clients created via launch(), this is set to the server's
                provenance UUID, enabling automatic shutdown authorization.
        """
        self._connection = connection
        self._server = server
        self._instance_uuid = instance_uuid
        self._debugger: Debugger | None = None
        self._cpu: CPU | None = None
        self._keyboard: Keyboard | None = None
        self._video: Video | None = None
        self._memory: Memory | None = None
        self._basic: Basic | None = None
        self._system_via: Via | None = None
        self._user_via: Via | None = None
        self._crtc: Crtc | None = None
        self._video_ula: VideoUla | None = None
        self._addressable_latch: AddressableLatch | None = None
        self._sound: Sound | None = None
        self._system: System | None = None
        self._disc: Disc | None = None
        self._econet: Econet | None = None
        self._tube: Tube | None = None
        self._tube_ula: TubeUlaInspection | None = None

    @classmethod
    def connect(cls, target: str | None = None, timeout: float = 5.0) -> Beebium:
        """Connect to an already-running beebium-server.

        Args:
            target: The gRPC target string (e.g., "localhost:48875").
                   Defaults to localhost on DEFAULT_GRPC_PORT (48875).
            timeout: Connection timeout in seconds.

        Returns:
            A connected Beebium client.

        Raises:
            ConnectionError: If the connection cannot be established.

        Note:
            Clients created via connect() have a unique instance UUID that
            generally won't match the server's launch provenance (unless the
            server was started with --allow-shutdown or only one client connects).
        """
        if target is None:
            target = f"localhost:{beebium.DEFAULT_GRPC_PORT}"
        connection = Connection(target, timeout=timeout)
        # Generate a unique instance UUID for this connection.
        # This won't match the server's provenance unless the server was started
        # with --allow-shutdown or this is the sole client.
        instance_uuid = str(uuid.uuid4())
        return cls(connection, instance_uuid=instance_uuid)

    @classmethod
    @contextlib.contextmanager
    def launch(
        cls,
        mos_filepath: str | Path,
        basic_filepath: str | Path | None = None,
        server_filepath: str | Path | None = None,
        port: int = 0,
        startup_timeout: float = 10.0,
        connection_timeout: float = 5.0,
        extra_args: list[str] | None = None,
    ) -> Iterator[Beebium]:
        """Start a beebium-server process and connect to it.

        The server is automatically stopped when the context manager exits.

        Args:
            mos_filepath: Path to the MOS ROM file (required).
            basic_filepath: Path to the BASIC ROM file (optional).
            server_filepath: Path to the beebium-server executable (optional).
            port: Port to listen on. If 0 (default), a free port is allocated.
            startup_timeout: Maximum time to wait for server to start (seconds).
            connection_timeout: Maximum time to wait for connection (seconds).
            extra_args: Additional command-line arguments to pass to the server
                (e.g., ["--tube", "65C02-3MHz"]).

        Yields:
            A connected Beebium client.

        Raises:
            ServerStartupError: If the server fails to start.
            ConnectionError: If the connection cannot be established.
        """
        server = ServerProcess(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            port=port,
            extra_args=extra_args,
        )

        try:
            server.start(timeout=startup_timeout)
            connection = Connection(server.target, timeout=connection_timeout)
            # Use the server's provenance UUID so this client is authorized
            # to request shutdown (matches launch provenance).
            client = cls(
                connection,
                server=server,
                instance_uuid=server.provenance_instance_uuid,
            )
            yield client
        finally:
            server.stop()

    @property
    def target(self) -> str:
        """The gRPC target string."""
        return self._connection.target

    @property
    def debugger(self) -> Debugger:
        """Access debugger control (execution, breakpoints)."""
        if self._debugger is None:
            self._debugger = Debugger(self._connection.debugger_stub)
        return self._debugger

    @property
    def cpu(self) -> CPU:
        """Access 6502 CPU registers."""
        if self._cpu is None:
            self._cpu = CPU(self._connection.debugger_stub)
        return self._cpu

    @property
    def keyboard(self) -> Keyboard:
        """Access keyboard input."""
        if self._keyboard is None:
            self._keyboard = Keyboard(self._connection.keyboard_stub)
        return self._keyboard

    @property
    def video(self) -> Video:
        """Access video frame streaming."""
        if self._video is None:
            self._video = Video(self._connection.video_stub)
        return self._video

    @property
    def memory(self) -> Memory:
        """Access memory read/write with subscript notation."""
        if self._memory is None:
            self._memory = Memory(self._connection.debugger_stub)
        return self._memory

    @property
    def basic(self) -> Basic:
        """Access BBC BASIC workflow helpers."""
        if self._basic is None:
            self._basic = Basic(self)
        return self._basic

    @property
    def system_via(self) -> Via:
        """Access System VIA (6522) state."""
        if self._system_via is None:
            self._system_via = Via(self._connection.device_inspection_stub, ViaId.SYSTEM)
        return self._system_via

    @property
    def user_via(self) -> Via:
        """Access User VIA (6522) state."""
        if self._user_via is None:
            self._user_via = Via(self._connection.device_inspection_stub, ViaId.USER)
        return self._user_via

    @property
    def crtc(self) -> Crtc:
        """Access CRTC (6845) state."""
        if self._crtc is None:
            self._crtc = Crtc(self._connection.device_inspection_stub)
        return self._crtc

    @property
    def video_ula(self) -> VideoUla:
        """Access Video ULA state."""
        if self._video_ula is None:
            self._video_ula = VideoUla(self._connection.device_inspection_stub)
        return self._video_ula

    @property
    def addressable_latch(self) -> AddressableLatch:
        """Access addressable latch state."""
        if self._addressable_latch is None:
            self._addressable_latch = AddressableLatch(self._connection.device_inspection_stub)
        return self._addressable_latch

    @property
    def sound(self) -> Sound:
        """Access SN76489 sound chip state."""
        if self._sound is None:
            self._sound = Sound(self._connection.device_inspection_stub)
        return self._sound

    @property
    def tube_ula(self) -> TubeUlaInspection:
        """Access Tube ULA device state (side-effect-free inspection)."""
        if self._tube_ula is None:
            self._tube_ula = TubeUlaInspection(self._connection.device_inspection_stub)
        return self._tube_ula

    @property
    def system(self) -> System:
        """Access system information and server status."""
        if self._system is None:
            self._system = System(
                self._connection.system_stub,
                instance_uuid=self._instance_uuid,
            )
        return self._system

    @property
    def disc(self) -> Disc:
        """Access floppy disc drive management."""
        if self._disc is None:
            self._disc = Disc(self._connection.disc_stub)
        return self._disc

    @property
    def econet(self) -> Econet:
        """Access Econet networking management."""
        if self._econet is None:
            self._econet = Econet(self._connection.econet_stub)
        return self._econet

    @property
    def tube(self) -> Tube:
        """Access Tube coprocessor management."""
        if self._tube is None:
            self._tube = Tube(self._connection.tube_stub)
        return self._tube

    def connect_parasite(self, timeout: float = 5.0) -> Beebium:
        """Connect to the parasite's gRPC server.

        Queries the host's Tube status for the parasite's gRPC address,
        then returns a new Beebium instance connected to the parasite.
        This gives access to the parasite's debugger, CPU, and memory.

        Args:
            timeout: Connection timeout in seconds.

        Returns:
            A Beebium client connected to the parasite's gRPC server.

        Raises:
            BeebiumConnectionError: If the parasite is not connected or the
                connection cannot be established.
        """
        status = self.tube.status
        if not status.parasite_connected:
            raise BeebiumConnectionError("No parasite is connected")
        address = status.parasite_grpc_address
        if not address:
            raise BeebiumConnectionError("Parasite has not registered its gRPC endpoint")
        return Beebium.connect(target=address, timeout=timeout)

    @contextlib.contextmanager
    def parasite(self, timeout: float = 5.0) -> Iterator[Beebium]:
        """Context manager that connects to the parasite and closes on exit.

        Usage::

            with bbc.parasite() as parasite:
                print(parasite.cpu.registers)

        Raises:
            BeebiumConnectionError: If the parasite is not connected or the
                connection cannot be established.
        """
        parasite = self.connect_parasite(timeout=timeout)
        try:
            yield parasite
        finally:
            parasite.close()

    # =========================================================================
    # Emulated time helpers
    # =========================================================================

    def run_for_emulated_seconds(self, seconds: float) -> None:
        """Run the emulator for the specified number of emulated seconds.

        Computes the cycle count from the clock speed and steps that many
        cycles synchronously. The emulator must be stopped on entry and
        is left stopped on return.

        Args:
            seconds: Number of emulated BBC-time seconds to run.
        """
        clock_hz = self.system.clock_speed_hz or 2_000_000
        cycles = int(seconds * clock_hz)
        self.debugger.ensure_stopped()
        self.debugger.step_cycles(cycles)

    def run_until_or_timeout(
        self, predicate, emulated_seconds: float, *,
        chunk_seconds: float = 0.1,
    ) -> bool:
        """Run until predicate() returns True or the emulated time budget expires.

        Execution proceeds in chunks of emulated time. At the end of each
        chunk, the machine stops (via a server-side cycle-budget breakpoint),
        the predicate is evaluated via peek, and if false, the next chunk
        starts. No wall-clock polling -- all timing is in emulated time.

        Args:
            predicate: Callable returning True when the condition is met.
                Evaluated via peek (side-effect-free) while the machine is
                stopped between chunks.
            emulated_seconds: Maximum emulated BBC-time seconds to run.
            chunk_seconds: Emulated time per chunk between predicate checks.
                Smaller values check the predicate more often but add
                overhead from stopping and restarting.

        Returns:
            True if the predicate was satisfied, False on timeout.
        """
        clock_hz = self.system.clock_speed_hz or 2_000_000
        total_budget = int(emulated_seconds * clock_hz)
        chunk_cycles = int(chunk_seconds * clock_hz)
        start_cycles = self.debugger.cycle_count
        deadline_cycles = start_cycles + total_budget

        try:
            while self.debugger.cycle_count < deadline_cycles:
                chunk_target = min(
                    self.debugger.cycle_count + chunk_cycles,
                    deadline_cycles,
                )
                with self.debugger.breakpoint(
                    0x0000, end_address=0x10000,
                    condition=f"cycles >= {chunk_target}",
                ):
                    self.debugger.run()
                    self.debugger.wait_for_stop()

                if predicate():
                    return True

            return predicate()
        finally:
            self.debugger.ensure_stopped()

    def close(self) -> None:
        """Close the connection and stop any managed server.

        For managed servers (created via launch()), this first requests
        graceful shutdown via RPC before falling back to SIGTERM.
        """
        # Try graceful RPC shutdown first if we're managing a server
        if self._server is not None and self._server.is_running:
            try:
                # Import here to avoid circular dependency
                from beebium.system import ShutdownMode
                response = self.system.request_shutdown(mode=ShutdownMode.GRACEFUL)
                # If accepted, give it a brief moment to take effect
                if response.accepted:
                    import time
                    time.sleep(0.1)  # Brief wait for graceful shutdown to start
            except (BeebiumError, grpc.RpcError):
                # If RPC fails, fall back to SIGTERM
                pass

        self._connection.close()
        if self._server is not None:
            self._server.stop()
            self._server = None

    def __enter__(self) -> Beebium:
        return self

    def __exit__(self, *args: object) -> None:
        self.close()
