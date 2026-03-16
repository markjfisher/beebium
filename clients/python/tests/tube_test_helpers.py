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

"""Shared utilities for Tube integration tests.

Provides coupled stepping, diagnostic dump functions, and constants
used by multiple Tube game test suites (Elite, Chuckie Egg 2023, etc.).
"""
from __future__ import annotations

from pathlib import Path

import grpc

from beebium.client import Beebium
from beebium.disassemble import disassemble
from beebium.exceptions import BeebiumError
from beebium.screen import dump_screen, screen_contains


# Tube OSRDCH is slower than native (each keystroke traverses the R1/R2
# protocol), so use a longer per-key delay than the default.
TUBE_CYCLES_PER_KEY = 200_000

# DFS ROM for the Acorn 1770 disc controller.
# DNFS ROMs contain an 8271-only DFS and are NOT compatible with the 1770.
DFS_1770_ROM_CANDIDATES = [
    "acorn-dfs_2_26.rom",
]


def find_dfs_1770_rom(roms_dirpath: Path) -> Path | None:
    """Find a 1770 DFS ROM in the ROM directory."""
    for name in DFS_1770_ROM_CANDIDATES:
        candidate = roms_dirpath / name
        if candidate.exists():
            return candidate
    return None


def run_until_or_timeout(bbc: Beebium, predicate, emulated_seconds: float,
                         chunk_cycles: int = 20000):
    """Run the emulator until predicate() returns True or a cycle budget expires.

    Delegates to Beebium.run_until_or_timeout with coupled=True, stepping
    both host and parasite in alternating chunks. This avoids the pacing
    asymmetry that occurs when only one side is stepped synchronously.

    Args:
        bbc: The Beebium instance (must be stopped on entry).
        predicate: Callable returning True when the desired condition is met.
        emulated_seconds: Maximum BBC-time seconds to run.
        chunk_cycles: Cycles per chunk (default 20000).

    Returns:
        True if the predicate was satisfied, False on timeout.
    """
    return bbc.run_until_or_timeout(
        predicate, emulated_seconds, coupled=True, chunk_cycles=chunk_cycles,
    )


def disassemble_region(memory, start: int, length: int) -> list[str]:
    """Disassemble a region of memory, returning formatted lines."""
    data = memory.address.peek.read(start, length)
    return [f"  {line}" for line in disassemble(data, start=start, length=length)]


def dump_diagnostics(bbc: Beebium) -> None:
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
            lines = disassemble_region(bbc.memory, dis_start, 64)
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
                lines = disassemble_region(bbc.memory, addr, length)
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
            dump_parasite_diagnostics(parasite)

    print("=== END DIAGNOSTICS ===\n")


def dump_parasite_diagnostics(parasite: Beebium) -> None:
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
            lines = disassemble_region(parasite.memory, dis_start, 48)
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
