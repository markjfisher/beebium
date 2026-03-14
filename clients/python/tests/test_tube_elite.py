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
from beebium.exceptions import BeebiumError, ConnectionError as BeebiumConnectionError, ServerNotFoundError
from beebium.screen import dump_screen, screen_contains


ELITE_DISC_FILENAME = "Disc999-EliteSNG45.ssd"

# DNFS ROM provides DFS + NFS + Tube Host Code
DNFS_ROM_CANDIDATES = [
    "acorn-dnfs_3_02.rom",
    "acorn-dnfs_3_34.rom",
    "acorn-dfs_2_26.rom",
]


def _find_elite_disc() -> Path | None:
    """Find the Elite disc image relative to the repo root."""
    repo_root = Path(__file__).parent.parent.parent.parent
    candidate = repo_root / "discs" / "games" / ELITE_DISC_FILENAME
    if candidate.exists():
        return candidate
    return None


def _find_dnfs_rom(roms_dirpath: Path) -> Path | None:
    """Find a DNFS/DFS ROM in the ROM directory."""
    for name in DNFS_ROM_CANDIDATES:
        candidate = roms_dirpath / name
        if candidate.exists():
            return candidate
    return None


_OPCODE_NAMES = {
    0x00: ("BRK", 1), 0x01: ("ORA (zp,X)", 2), 0x05: ("ORA zp", 2),
    0x06: ("ASL zp", 2), 0x08: ("PHP", 1), 0x09: ("ORA #imm", 2),
    0x0A: ("ASL A", 1), 0x0D: ("ORA abs", 3), 0x0E: ("ASL abs", 3),
    0x10: ("BPL rel", 2), 0x11: ("ORA (zp),Y", 2), 0x15: ("ORA zp,X", 2),
    0x18: ("CLC", 1), 0x19: ("ORA abs,Y", 3), 0x1A: ("INC A", 1),
    0x20: ("JSR abs", 3), 0x21: ("AND (zp,X)", 2), 0x24: ("BIT zp", 2),
    0x25: ("AND zp", 2), 0x26: ("ROL zp", 2), 0x28: ("PLP", 1),
    0x29: ("AND #imm", 2), 0x2A: ("ROL A", 1), 0x2C: ("BIT abs", 3),
    0x2D: ("AND abs", 3), 0x2E: ("ROL abs", 3), 0x30: ("BMI rel", 2),
    0x38: ("SEC", 1), 0x3D: ("AND abs,X", 3),
    0x40: ("RTI", 1), 0x41: ("EOR (zp,X)", 2), 0x45: ("EOR zp", 2),
    0x46: ("LSR zp", 2), 0x48: ("PHA", 1), 0x49: ("EOR #imm", 2),
    0x4A: ("LSR A", 1), 0x4C: ("JMP abs", 3), 0x4D: ("EOR abs", 3),
    0x50: ("BVC rel", 2), 0x51: ("EOR (zp),Y", 2),
    0x58: ("CLI", 1), 0x59: ("EOR abs,Y", 3), 0x5A: ("PHY", 1),
    0x60: ("RTS", 1), 0x61: ("ADC (zp,X)", 2), 0x64: ("STZ zp", 2),
    0x65: ("ADC zp", 2), 0x66: ("ROR zp", 2), 0x68: ("PLA", 1),
    0x69: ("ADC #imm", 2), 0x6A: ("ROR A", 1), 0x6C: ("JMP (abs)", 3),
    0x6D: ("ADC abs", 3), 0x70: ("BVS rel", 2),
    0x78: ("SEI", 1), 0x7A: ("PLY", 1), 0x7C: ("JMP (abs,X)", 3),
    0x80: ("BRA rel", 2), 0x81: ("STA (zp,X)", 2), 0x84: ("STY zp", 2),
    0x85: ("STA zp", 2), 0x86: ("STX zp", 2), 0x88: ("DEY", 1),
    0x89: ("BIT #imm", 2), 0x8A: ("TXA", 1), 0x8C: ("STY abs", 3),
    0x8D: ("STA abs", 3), 0x8E: ("STX abs", 3), 0x90: ("BCC rel", 2),
    0x91: ("STA (zp),Y", 2), 0x92: ("STA (zp)", 2), 0x95: ("STA zp,X", 2),
    0x98: ("TYA", 1), 0x99: ("STA abs,Y", 3), 0x9A: ("TXS", 1),
    0x9C: ("STZ abs", 3), 0x9D: ("STA abs,X", 3),
    0xA0: ("LDY #imm", 2), 0xA1: ("LDA (zp,X)", 2), 0xA2: ("LDX #imm", 2),
    0xA4: ("LDY zp", 2), 0xA5: ("LDA zp", 2), 0xA6: ("LDX zp", 2),
    0xA8: ("TAY", 1), 0xA9: ("LDA #imm", 2), 0xAA: ("TAX", 1),
    0xAC: ("LDY abs", 3), 0xAD: ("LDA abs", 3), 0xAE: ("LDX abs", 3),
    0xB0: ("BCS rel", 2), 0xB1: ("LDA (zp),Y", 2), 0xB2: ("LDA (zp)", 2),
    0xB5: ("LDA zp,X", 2), 0xB9: ("LDA abs,Y", 3), 0xBA: ("TSX", 1),
    0xBD: ("LDA abs,X", 3),
    0xC0: ("CPY #imm", 2), 0xC1: ("CMP (zp,X)", 2), 0xC4: ("CPY zp", 2),
    0xC5: ("CMP zp", 2), 0xC6: ("DEC zp", 2), 0xC8: ("INY", 1),
    0xC9: ("CMP #imm", 2), 0xCA: ("DEX", 1), 0xCC: ("CPY abs", 3),
    0xCD: ("CMP abs", 3), 0xCE: ("DEC abs", 3), 0xD0: ("BNE rel", 2),
    0xD1: ("CMP (zp),Y", 2), 0xD5: ("CMP zp,X", 2), 0xD8: ("CLD", 1),
    0xD9: ("CMP abs,Y", 3), 0xDA: ("PHX", 1),
    0xE0: ("CPX #imm", 2), 0xE1: ("SBC (zp,X)", 2), 0xE4: ("CPX zp", 2),
    0xE5: ("SBC zp", 2), 0xE6: ("INC zp", 2), 0xE8: ("INX", 1),
    0xE9: ("SBC #imm", 2), 0xEA: ("NOP", 1), 0xEC: ("CPX abs", 3),
    0xED: ("SBC abs", 3), 0xEE: ("INC abs", 3), 0xF0: ("BEQ rel", 2),
    0xF1: ("SBC (zp),Y", 2), 0xF5: ("SBC zp,X", 2), 0xF8: ("SED", 1),
    0xF9: ("SBC abs,Y", 3), 0xFA: ("PLX", 1), 0xFD: ("SBC abs,X", 3),
}


def _disassemble_region(memory: Memory, start: int, length: int) -> list[str]:
    """Disassemble a region of memory, returning formatted lines."""
    data = memory.address.peek.read(start, length)
    lines = []
    offset = 0
    while offset < length:
        addr = start + offset
        opcode = data[offset]
        entry = _OPCODE_NAMES.get(opcode)
        if entry is None:
            lines.append(f"  ${addr:04X}: {opcode:02X}          ???")
            offset += 1
            continue
        name, size = entry
        if offset + size > length:
            break
        raw = " ".join(f"{data[offset + i]:02X}" for i in range(size))
        if size == 3:
            operand = data[offset + 1] | (data[offset + 2] << 8)
            detail = name.replace("abs", f"${operand:04X}").replace("imm", f"${data[offset+1]:02X}")
        elif size == 2:
            if "rel" in name:
                rel = data[offset + 1]
                target = addr + 2 + (rel if rel < 128 else rel - 256)
                detail = name.replace("rel", f"${target:04X}")
            else:
                detail = name.replace("zp", f"${data[offset+1]:02X}").replace("imm", f"${data[offset+1]:02X}")
        else:
            detail = name
        lines.append(f"  ${addr:04X}: {raw:<8s}  {detail}")
        offset += size
    return lines


def _dump_diagnostics(bbc: Beebium, parasite: Beebium | None = None) -> None:
    """Print comprehensive diagnostics for debugging boot failures."""
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

    # Disassemble around host PC
    if host_pc is not None:
        try:
            dis_start = max(0, host_pc - 16)
            lines = _disassemble_region(bbc.memory, dis_start, 48)
            print(f"Host code around PC=${host_pc:04X}:")
            for line in lines:
                addr_str = line.strip().split(":")[0]
                addr_val = int(addr_str.lstrip("$"), 16)
                marker = " >>>" if addr_val == host_pc else ""
                print(f"{line}{marker}")
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Host disassembly: error - {e}")

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

    # Host-side Tube register status bytes
    # $FEE0=R1 status, $FEE2=R3 status, $FEE4=R3 status, $FEE6=R4 status
    try:
        r1s = bbc.memory.address.peek[0xFEE0]
        r1d = bbc.memory.address.peek[0xFEE1]
        r2s = bbc.memory.address.peek[0xFEE2]
        r2d = bbc.memory.address.peek[0xFEE3]
        r3s = bbc.memory.address.peek[0xFEE4]
        r3d = bbc.memory.address.peek[0xFEE5]
        r4s = bbc.memory.address.peek[0xFEE6]
        r4d = bbc.memory.address.peek[0xFEE7]
        print(f"Host Tube regs (host view):")
        print(f"  R1: status=${r1s:02X} data=${r1d:02X}  "
              f"[b7={'DATA' if r1s & 0x80 else 'empty'}, b6={'SPACE' if r1s & 0x40 else 'full'}]")
        print(f"  R2: status=${r2s:02X} data=${r2d:02X}")
        print(f"  R3: status=${r3s:02X} data=${r3d:02X}  "
              f"[b7={'DATA' if r3s & 0x80 else 'empty'}, b6={'SPACE' if r3s & 0x40 else 'full'}]")
        print(f"  R4: status=${r4s:02X} data=${r4d:02X}  "
              f"[b7={'DATA' if r4s & 0x80 else 'empty'}, b6={'SPACE' if r4s & 0x40 else 'full'}]")
        # Decode control flags from R1 status
        print(f"  R1 status flags: S={int(bool(r1s & 0x80))}, Q={int(bool(r1s & 0x40))}, "
              f"I={int(bool(r1s & 0x20))}, J={int(bool(r1s & 0x10))}, "
              f"M={int(bool(r1s & 0x08))}, V={int(bool(r1s & 0x04))}, "
              f"P={int(bool(r1s & 0x02))}, T={int(bool(r1s & 0x01))}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Host Tube regs: error reading - {e}")

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

    # MOS Tube flag at &027A
    try:
        tube_flag = bbc.memory.address.peek[0x027A]
        print(f"MOS Tube flag (&027A): ${tube_flag:02X}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"MOS Tube flag: error reading - {e}")

    # MOS filing system at &028C
    try:
        fs_byte = bbc.memory.address.peek[0x028C]
        print(f"MOS filing system (&028C): ${fs_byte:02X}")
    except (BeebiumError, grpc.RpcError) as e:
        print(f"MOS filing system: error reading - {e}")

    # Host screen
    try:
        print("Host screen:")
        print(dump_screen(bbc.memory))
    except (BeebiumError, grpc.RpcError) as e:
        print(f"Host screen: error reading - {e}")

    # Parasite diagnostics
    if parasite is not None:
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
            # Show stack from SP+1 upwards (what's been pushed)
            stack_top = sp + 1
            if stack_top < 256:
                stack_bytes = stack[stack_top:min(stack_top + 16, 256)]
                hex_str = " ".join(f"{b:02X}" for b in stack_bytes)
                print(f"Parasite stack (${0x100 + stack_top:04X}+): {hex_str}")
        except (BeebiumError, grpc.RpcError) as e:
            print(f"Parasite stack: error reading - {e}")

    print("=== END DIAGNOSTICS ===\n")


@pytest.fixture(scope="module")
def elite_disc_filepath() -> Path:
    """Path to the Elite disc image."""
    path = _find_elite_disc()
    if path is None:
        pytest.skip(f"Elite disc image not found: {ELITE_DISC_FILENAME}")
    return path


@pytest.fixture(scope="module")
def dnfs_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to a DNFS/DFS ROM file."""
    path = _find_dnfs_rom(beebium_roms_dirpath)
    if path is None:
        pytest.skip(
            f"DNFS/DFS ROM not found. Expected one of: {', '.join(DNFS_ROM_CANDIDATES)}"
        )
    return path


@pytest.fixture(scope="module")
def bbc_tube(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dnfs_rom_filepath: Path,
) -> Beebium:
    """A BBC Micro instance with Tube and disc controller enabled.

    Configures:
    - Tube with 65C02 3MHz parasite
    - Acorn 1770 disc controller
    - DNFS ROM in sideways slot 14 (provides DFS + Tube Host Code)

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
                "--sideways", f"14:rom:{dnfs_rom_filepath}",
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
        # Let the machine run for a few seconds (may already be running)
        if not bbc_tube.debugger.is_running:
            bbc_tube.debugger.run()
        time.sleep(3.0)
        bbc_tube.debugger.stop()

        if not screen_contains(bbc_tube.memory, "Acorn TUBE"):
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

    def test_exec_boot(self, bbc_tube: Beebium) -> None:
        """Type *EXEC !BOOT and check for Elite loading screen."""
        parasite = None
        try:
            # Connect to the parasite for diagnostics
            try:
                parasite = bbc_tube.connect_parasite()
            except (BeebiumConnectionError, grpc.RpcError):
                pass  # Diagnostics are optional

            # Resume execution, type the boot command
            if not bbc_tube.debugger.is_running:
                bbc_tube.debugger.run()
            time.sleep(0.5)

            bbc_tube.keyboard.type("*EXEC !BOOT")
            bbc_tube.keyboard.press_return()

            # Wait for loading -- Elite takes a while
            time.sleep(15.0)

            bbc_tube.debugger.stop()

            # Check for the Elite loading screen text
            found_acornsoft = screen_contains(bbc_tube.memory, "ACORNSOFT")
            found_elite = screen_contains(bbc_tube.memory, "ELITE")

            if not (found_acornsoft or found_elite):
                _dump_diagnostics(bbc_tube, parasite)
                pytest.fail(
                    "Expected 'ACORNSOFT' or 'ELITE' on screen after *EXEC !BOOT"
                )
        finally:
            if parasite is not None:
                parasite.close()
