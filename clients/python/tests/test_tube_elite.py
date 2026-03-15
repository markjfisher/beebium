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

"""Integration tests for booting 6502 Second Processor Elite via the Tube.

These tests launch a Beebium server with --tube 65C02-3MHz, insert the
6502 Second Processor Elite disc, and attempt to boot it. The goal is to
exercise the full Tube data transfer pipeline under real workload.

Requirements:
    - Beebium server executable (auto-detected or via BEEBIUM_SERVER)
    - MOS 1.20 ROM and BASIC 2 ROM (via BEEBIUM_ROM_DIR)
    - Elite disc image at discs/games/Disc999-EliteSNG45.ssd
"""

from __future__ import annotations

import time
from pathlib import Path

import grpc
import pytest

from beebium.client import Beebium
from beebium.disassemble import disassemble
from beebium.exceptions import BeebiumError, ServerNotFoundError
from beebium.screen import dump_screen, screen_contains
from beebium.tube_ula import TubeUlaInspection


ELITE_DISC_FILENAME = "Disc999-EliteSNG45.ssd"


def _run_until_or_timeout(bbc: Beebium, predicate, emulated_seconds: float,
                          poll_interval: float = 0.1):
    """Run the emulator until predicate() returns True or a cycle budget expires.

    Uses the emulator's own cycle counter so the timeout is independent of
    host machine speed.  The emulator is left in whatever execution state
    it was in on entry.

    Args:
        bbc: The Beebium instance (may be running or stopped on entry).
        predicate: Callable returning True when the desired condition is met.
        emulated_seconds: Maximum BBC-time seconds to run.
        poll_interval: Real-time seconds between cycle-count polls.

    Returns:
        True if the predicate was satisfied, False on timeout.
    """
    clock_hz = bbc.system.clock_speed_hz or 2_000_000
    cycle_budget = int(emulated_seconds * clock_hz)
    start_cycles = bbc.debugger.cycle_count
    target_cycles = start_cycles + cycle_budget

    with bbc.debugger.running():
        while True:
            time.sleep(poll_interval)
            current_cycles = bbc.debugger.cycle_count
            if current_cycles >= target_cycles:
                return predicate()
            # Only check the predicate once enough emulated time has passed
            # for the BBC to have processed keyboard input and produced output.
            # Stopping and restarting too early can disrupt the Tube protocol.
            if current_cycles - start_cycles >= clock_hz:
                bbc.debugger.stop()
                if predicate():
                    return True
                bbc.debugger.run()

# DFS ROM for the Acorn 1770 disc controller.
# DNFS ROMs contain an 8271-only DFS and are NOT compatible with the 1770.
DFS_1770_ROM_CANDIDATES = [
    "acorn-dfs_2_26.rom",
]


def _find_elite_disc() -> Path | None:
    """Find the Elite disc image.

    Search order:
    1. Test assets directory (tests/assets/discs/)
    2. Development disc collection (discs/games/)
    """
    repo_root = Path(__file__).parent.parent.parent.parent
    candidates = [
        repo_root / "tests" / "assets" / "discs" / ELITE_DISC_FILENAME,
        repo_root / "discs" / "games" / ELITE_DISC_FILENAME,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def _find_dfs_1770_rom(roms_dirpath: Path) -> Path | None:
    """Find a 1770 DFS ROM in the ROM directory."""
    for name in DFS_1770_ROM_CANDIDATES:
        candidate = roms_dirpath / name
        if candidate.exists():
            return candidate
    return None


def _disassemble_region(memory: Memory, start: int, length: int) -> list[str]:
    """Disassemble a region of memory, returning formatted lines."""
    data = memory.address.peek.read(start, length)
    return [f"  {line}" for line in disassemble(data, start=start, length=length)]


def _dump_diagnostics(bbc: Beebium) -> None:
    """Print comprehensive diagnostics for debugging boot failures.

    Attempts to connect to the parasite for additional diagnostics.
    """
    print("\n=== DIAGNOSTICS ===")

    # Host CPU state
    host_pc = None
    try:
        regs = bbc.cpu.registers
        host_pc = regs.pc
        print(f"Host CPU: {regs}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Host CPU: error reading - {e}")

    # Host execution state
    try:
        state = bbc.debugger.get_state()
        print(f"Host execution: running={state.is_running}, cycles={state.cycle_count}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Host execution: error reading - {e}")

    # Disassemble around host PC + key MOS routines
    if host_pc is not None:
        try:
            dis_start = max(0, host_pc - 16)
            lines = _disassemble_region(bbc.memory, dis_start, 64)
            print(f"Host code around PC=${host_pc:04X}:")
            for line in lines:
                addr_str = line.strip().split(":")[0]
                addr_val = int(addr_str.lstrip("$"), 16)
                marker = " >>>" if addr_val == host_pc else ""
                print(f"{line}{marker}")
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Host disassembly: error - {e}")

        # Disassemble key MOS Tube routines
        for label, addr, length in [
            ("$FB50 (Tube OSRDCH setup)", 0xFB50, 64),
            ("$F720 (Tube OSRDCH handler)", 0xF720, 48),
            ("$DC93 (IRQ1V handler)", 0xDC93, 64),
        ]:
            try:
                lines = _disassemble_region(bbc.memory, addr, length)
                print(f"Host code at {label}:")
                for line in lines:
                    print(f"{line}")
            except (BeebiumError, grpc.RpcError) as e:
                print(f"Host code at {label}: error - {e}")

        # Read host ZP $C2 (state variable from OSRDCH loop)
        try:
            c2 = bbc.memory.address.peek[0x00C2]
            print(f"Host ZP $C2 (OSRDCH state): ${c2:02X}")
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Host ZP $C2: error - {e}")

    # Tube status
    try:
        tube_status = bbc.tube.status
        print(f"Tube: enabled={tube_status.enabled}, "
              f"connected={tube_status.parasite_connected}, "
              f"type={tube_status.parasite_type}, "
              f"clock={tube_status.parasite_clock_hz}Hz, "
              f"parasite_addr={tube_status.parasite_grpc_address}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Tube: error reading - {e}")

    # Tube ULA device inspection (side-effect-free)
    try:
        tube_ula_state = bbc.tube_ula.state
        print(tube_ula_state)
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Tube ULA inspection: error - {e}")

    # Disc drive status
    try:
        disc_status = bbc.disc.status
        print(f"Disc controller: {disc_status.controller_type}")
        for drive in disc_status.drives:
            print(f"  Drive {drive.drive}: state={drive.state.value}, "
                  f"motor={'on' if drive.motor_on else 'off'}, "
                  f"track={drive.current_track}, "
                  f"disc={drive.disc_name}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Disc: error reading - {e}")

    # MOS workspace variables
    try:
        tube_flag = bbc.memory.address.peek[0x027A]
        fs_byte = bbc.memory.address.peek[0x028C]
        exec_handle = bbc.memory.address.peek[0x0257]
        spool_handle = bbc.memory.address.peek[0x0256]
        zp_eb = bbc.memory.address.peek[0x00EB]
        zp_ff = bbc.memory.address.peek[0x00FF]
        print(f"MOS Tube flag (&027A): ${tube_flag:02X}")
        print(f"MOS filing system (&028C): ${fs_byte:02X}")
        print(f"MOS exec handle (&0257): ${exec_handle:02X}")
        print(f"MOS spool handle (&0256): ${spool_handle:02X}")
        print(f"MOS ZP $EB (exec check): ${zp_eb:02X}")
        print(f"MOS ZP $FF (escape flag): ${zp_ff:02X}")
        # OSRDCH vector
        rdch_lo = bbc.memory.address.peek[0x0238]
        rdch_hi = bbc.memory.address.peek[0x0239]
        print(f"OSRDCH vector (&0238): ${rdch_hi:02X}{rdch_lo:02X}")
        # IRQ1V
        irq1v_lo = bbc.memory.address.peek[0x0204]
        irq1v_hi = bbc.memory.address.peek[0x0205]
        print(f"IRQ1V (&0204): ${irq1v_hi:02X}{irq1v_lo:02X}")
        # IRQ2V
        irq2v_lo = bbc.memory.address.peek[0x0206]
        irq2v_hi = bbc.memory.address.peek[0x0207]
        print(f"IRQ2V (&0206): ${irq2v_hi:02X}{irq2v_lo:02X}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"MOS workspace: error reading - {e}")

    # Host stack
    if host_pc is not None:
        try:
            sp = bbc.cpu.registers.sp
            stack = bbc.memory.address.peek.read(0x0100, 256)
            stack_top = sp + 1
            if stack_top < 256:
                stack_bytes = stack[stack_top:min(stack_top + 32, 256)]
                hex_str = " ".join(f"{b:02X}" for b in stack_bytes)
                print(f"Host stack (${0x100 + stack_top:04X}+): {hex_str}")
                # Decode return addresses
                i = 0
                while i + 1 < len(stack_bytes):
                    addr = stack_bytes[i] | (stack_bytes[i+1] << 8)
                    print(f"  Stack ${0x100 + stack_top + i:04X}: ${addr:04X} (return to ${addr+1:04X}?)")
                    i += 2
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Host stack: error reading - {e}")

    # Host screen
    try:
        print("Host screen:")
        print(dump_screen(bbc.memory))
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Host screen: error reading - {e}")

    # Parasite diagnostics
    if bbc.tube.parasite_connected:
        with bbc.parasite() as parasite:
            _dump_parasite_diagnostics(parasite)

    print("=== END DIAGNOSTICS ===\n")


def _dump_parasite_diagnostics(parasite: Beebium) -> None:
    """Print parasite-side diagnostics."""
    parasite_pc = None
    try:
        p_regs = parasite.cpu.registers
        parasite_pc = p_regs.pc
        print(f"Parasite CPU: {p_regs}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Parasite CPU: error reading - {e}")

    try:
        p_state = parasite.debugger.get_state()
        print(f"Parasite execution: running={p_state.is_running}, "
              f"cycles={p_state.cycle_count}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Parasite execution: error reading - {e}")

    # Disassemble around parasite PC
    if parasite_pc is not None:
        try:
            dis_start = max(0, parasite_pc - 16)
            lines = _disassemble_region(parasite.memory, dis_start, 48)
            print(f"Parasite code around PC=${parasite_pc:04X}:")
            for line in lines:
                addr_str = line.strip().split(":")[0]
                addr_val = int(addr_str.lstrip("$"), 16)
                marker = " >>>" if addr_val == parasite_pc else ""
                print(f"{line}{marker}")
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Parasite disassembly: error - {e}")

    # Parasite Tube register status (parasite view)
    try:
        pr1s = parasite.memory.address.peek[0xFEF8]
        pr1d = parasite.memory.address.peek[0xFEF9]
        pr2s = parasite.memory.address.peek[0xFEFA]
        pr2d = parasite.memory.address.peek[0xFEFB]
        pr3s = parasite.memory.address.peek[0xFEFC]
        pr3d = parasite.memory.address.peek[0xFEFD]
        pr4s = parasite.memory.address.peek[0xFEFE]
        pr4d = parasite.memory.address.peek[0xFEFF]
        print(f"Parasite Tube regs (parasite view):")
        print(f"  R1: status=${pr1s:02X} data=${pr1d:02X}  "
              f"[b7={'DATA' if pr1s & 0x80 else 'empty'}]")
        print(f"  R2: status=${pr2s:02X} data=${pr2d:02X}")
        print(f"  R3: status=${pr3s:02X} data=${pr3d:02X}  "
              f"[b7={'DATA' if pr3s & 0x80 else 'empty'}]")
        print(f"  R4: status=${pr4s:02X} data=${pr4d:02X}  "
              f"[b7={'DATA' if pr4s & 0x80 else 'empty'}]")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Parasite Tube regs: error reading - {e}")

    # Parasite MOS workspace
    try:
        p_exec = parasite.memory.address.peek[0x0257]
        p_spool = parasite.memory.address.peek[0x0256]
        p_tube = parasite.memory.address.peek[0x027A]
        p_fs = parasite.memory.address.peek[0x028C]
        p_eb = parasite.memory.address.peek[0x00EB]
        print(f"Parasite exec handle (&0257): ${p_exec:02X}")
        print(f"Parasite spool handle (&0256): ${p_spool:02X}")
        print(f"Parasite Tube flag (&027A): ${p_tube:02X}")
        print(f"Parasite FS (&028C): ${p_fs:02X}")
        print(f"Parasite ZP $EB (exec check): ${p_eb:02X}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Parasite MOS workspace: error reading - {e}")

    # Full parasite zero page
    try:
        zp = parasite.memory.address.peek.read(0x0000, 256)
        print("Parasite zero page:")
        for row in range(16):
            offset = row * 16
            hex_str = " ".join(f"{zp[offset + i]:02X}" for i in range(16))
            print(f"  ${offset:02X}: {hex_str}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Parasite ZP: error reading - {e}")

    # Parasite vectors and NMIV
    try:
        nmiv = parasite.memory.address.peek.read(0x0200, 2)
        nmiv_addr = nmiv[0] | (nmiv[1] << 8)
        irqv = parasite.memory.address.peek.read(0x0202, 2)
        irqv_addr = irqv[0] | (irqv[1] << 8)
        nmi_vec = parasite.memory.address.peek.read(0xFFFA, 2)
        nmi_addr = nmi_vec[0] | (nmi_vec[1] << 8)
        irq_vec = parasite.memory.address.peek.read(0xFFFE, 2)
        irq_addr = irq_vec[0] | (irq_vec[1] << 8)
        print(f"Parasite vectors: NMIV=$0200={nmiv_addr:04X}, "
              f"IRQ1V=$0202={irqv_addr:04X}")
        print(f"Parasite HW vectors: NMI=$FFFA={nmi_addr:04X}, "
              f"IRQ=$FFFE={irq_addr:04X}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Parasite vectors: error reading - {e}")

    # Parasite stack
    try:
        stack = parasite.memory.address.peek.read(0x0100, 256)
        sp = p_regs.sp if parasite_pc is not None else 0xFF
        stack_top = sp + 1
        if stack_top < 256:
            stack_bytes = stack[stack_top:min(stack_top + 16, 256)]
            hex_str = " ".join(f"{b:02X}" for b in stack_bytes)
            print(f"Parasite stack (${0x100 + stack_top:04X}+): {hex_str}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Parasite stack: error reading - {e}")


@pytest.fixture(scope="module")
def elite_disc_filepath() -> Path:
    """Path to the Elite disc image."""
    path = _find_elite_disc()
    if path is None:
        pytest.skip(f"Elite disc image not found: {ELITE_DISC_FILENAME}")
    return path


@pytest.fixture(scope="module")
def dfs_1770_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to a 1770 DFS ROM file."""
    path = _find_dfs_1770_rom(beebium_roms_dirpath)
    if path is None:
        pytest.skip(
            f"1770 DFS ROM not found. Expected one of: {', '.join(DFS_1770_ROM_CANDIDATES)}"
        )
    return path


@pytest.fixture(scope="module")
def bbc_tube(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
) -> Beebium:
    """A BBC Micro instance with Tube and disc controller enabled.

    Configures:
    - Tube with 65C02 3MHz parasite
    - Acorn 1770 disc controller
    - 1770 DFS ROM in sideways slot 14

    Uses a longer startup timeout to allow the parasite process to connect.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--tube", "65C02-3MHz",
                "--fdc", "acorn-1770",
                "--sideways", f"14:rom:{dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


class TestTubeEliteBoot:
    """Test booting 6502 Second Processor Elite via the Tube."""

    def test_tube_enabled(self, bbc_tube: Beebium) -> None:
        """Verify Tube hardware is enabled and parasite is connected."""
        status = bbc_tube.tube.status
        assert status.has_tube_socket, "Machine should have a Tube socket"
        assert status.enabled, "Tube should be enabled"
        assert status.parasite_connected, "Parasite should be connected"
        assert "65C02" in status.parasite_type, (
            f"Expected 65C02 parasite, got {status.parasite_type!r}"
        )
        assert status.parasite_grpc_address, "Parasite should have registered gRPC address"

    def test_tube_banner(self, bbc_tube: Beebium) -> None:
        """After boot, screen should show the Tube banner."""
        found = _run_until_or_timeout(
            bbc_tube,
            lambda: screen_contains(bbc_tube.memory, "Acorn TUBE"),
            emulated_seconds=10.0,
        )
        if not found:
            _dump_diagnostics(bbc_tube)
            pytest.fail("Expected 'Acorn TUBE' banner on screen after boot")

    def test_insert_elite_disc(
        self, bbc_tube: Beebium, elite_disc_filepath: Path
    ) -> None:
        """Insert the Elite disc into drive 0."""
        metadata = bbc_tube.disc.drive(0).insert(elite_disc_filepath)
        assert metadata.name, "Disc should have a name"

        drive_status = bbc_tube.disc.drive(0).status
        assert drive_status.state.value == "loaded", (
            f"Drive 0 should be loaded, got {drive_status.state.value}"
        )

    def test_cat_disc(self, bbc_tube: Beebium, elite_disc_filepath: Path) -> None:
        """Type *. to catalog the disc and check output appears."""
        def _catalog_visible():
            from beebium.screen import read_mode7_screen
            rows = read_mode7_screen(bbc_tube.memory)
            for row in rows:
                stripped = row.strip()
                if stripped and stripped != ">" and "BASIC" not in stripped \
                        and "Acorn TUBE" not in stripped and "Acorn 1770" not in stripped \
                        and "*." not in stripped:
                    return True
            return False

        bbc_tube.debugger.ensure_running()
        bbc_tube.keyboard.type("*.")
        bbc_tube.keyboard.press_return()

        found = _run_until_or_timeout(
            bbc_tube, _catalog_visible, emulated_seconds=10.0,
        )

        if not found:
            from beebium.screen import read_mode7_screen
            rows = read_mode7_screen(bbc_tube.memory)
            print("\nScreen after *. command:")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            _dump_diagnostics(bbc_tube)
            pytest.fail("Expected disc catalog output after *. command")

    def test_run_boot(self, bbc_tube: Beebium, elite_disc_filepath: Path) -> None:
        """Type *RUN !BOOT and check for Elite loading screen."""
        bbc_tube.debugger.ensure_running()
        bbc_tube.keyboard.type("*RUN !BOOT")
        bbc_tube.keyboard.press_return()

        # Poll for Elite loading text. The !BOOT loader briefly displays
        # "6502 Second Processor ELITE" in Mode 7 before switching to a
        # graphics mode for the game.
        elite_banner = "6502 Second Processor ELITE"
        found = _run_until_or_timeout(
            bbc_tube,
            lambda: screen_contains(bbc_tube.memory, elite_banner),
            emulated_seconds=15.0,
        )

        if not found:
            _dump_diagnostics(bbc_tube)
            pytest.fail(
                f"Expected '{elite_banner}' on screen after *RUN !BOOT"
            )
