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

"""6502/65C02 disassembler.

Provides simple disassembly of 6502 and 65C02 machine code, suitable for
debugger output and diagnostic dumps.

Usage::

    from beebium.client.disassemble import disassemble

    # Disassemble raw bytes at a given start address
    for line in disassemble(data, start=0xF800, length=32):
        print(line)
"""
from __future__ import annotations


# Opcode table: opcode -> (mnemonic_template, byte_count).
#
# Mnemonic templates use placeholders for operand formatting:
#   abs  -- 16-bit absolute address
#   zp   -- 8-bit zero-page address
#   imm  -- 8-bit immediate value
#   rel  -- 8-bit relative branch offset (formatted as target address)
#
# Includes 65C02 extensions (BRA, PHX, PHY, PLX, PLY, STZ, INC A, DEC A,
# (zp) indirect without index, JMP (abs,X), BIT #imm, BIT abs,X, etc.).

_OPCODES = {
    # 0x
    0x00: ("BRK", 1), 0x01: ("ORA (zp,X)", 2), 0x04: ("TSB zp", 2),
    0x05: ("ORA zp", 2), 0x06: ("ASL zp", 2), 0x08: ("PHP", 1),
    0x09: ("ORA #imm", 2), 0x0A: ("ASL A", 1), 0x0C: ("TSB abs", 3),
    0x0D: ("ORA abs", 3), 0x0E: ("ASL abs", 3),
    # 1x
    0x10: ("BPL rel", 2), 0x11: ("ORA (zp),Y", 2), 0x12: ("ORA (zp)", 2),
    0x14: ("TRB zp", 2), 0x15: ("ORA zp,X", 2), 0x16: ("ASL zp,X", 2),
    0x18: ("CLC", 1), 0x19: ("ORA abs,Y", 3), 0x1A: ("INC A", 1),
    0x1D: ("ORA abs,X", 3), 0x1E: ("ASL abs,X", 3),
    # 2x
    0x20: ("JSR abs", 3), 0x21: ("AND (zp,X)", 2), 0x24: ("BIT zp", 2),
    0x25: ("AND zp", 2), 0x26: ("ROL zp", 2), 0x28: ("PLP", 1),
    0x29: ("AND #imm", 2), 0x2A: ("ROL A", 1), 0x2C: ("BIT abs", 3),
    0x2D: ("AND abs", 3), 0x2E: ("ROL abs", 3),
    # 3x
    0x30: ("BMI rel", 2), 0x31: ("AND (zp),Y", 2), 0x32: ("AND (zp)", 2),
    0x34: ("BIT zp,X", 2), 0x35: ("AND zp,X", 2), 0x36: ("ROL zp,X", 2),
    0x38: ("SEC", 1), 0x39: ("AND abs,Y", 3), 0x3A: ("DEC A", 1),
    0x3C: ("BIT abs,X", 3), 0x3D: ("AND abs,X", 3), 0x3E: ("ROL abs,X", 3),
    # 4x
    0x40: ("RTI", 1), 0x41: ("EOR (zp,X)", 2), 0x45: ("EOR zp", 2),
    0x46: ("LSR zp", 2), 0x48: ("PHA", 1), 0x49: ("EOR #imm", 2),
    0x4A: ("LSR A", 1), 0x4C: ("JMP abs", 3), 0x4D: ("EOR abs", 3),
    0x4E: ("LSR abs", 3),
    # 5x
    0x50: ("BVC rel", 2), 0x51: ("EOR (zp),Y", 2), 0x52: ("EOR (zp)", 2),
    0x55: ("EOR zp,X", 2), 0x56: ("LSR zp,X", 2),
    0x58: ("CLI", 1), 0x59: ("EOR abs,Y", 3), 0x5A: ("PHY", 1),
    0x5D: ("EOR abs,X", 3), 0x5E: ("LSR abs,X", 3),
    # 6x
    0x60: ("RTS", 1), 0x61: ("ADC (zp,X)", 2), 0x64: ("STZ zp", 2),
    0x65: ("ADC zp", 2), 0x66: ("ROR zp", 2), 0x68: ("PLA", 1),
    0x69: ("ADC #imm", 2), 0x6A: ("ROR A", 1), 0x6C: ("JMP (abs)", 3),
    0x6D: ("ADC abs", 3), 0x6E: ("ROR abs", 3),
    # 7x
    0x70: ("BVS rel", 2), 0x71: ("ADC (zp),Y", 2), 0x72: ("ADC (zp)", 2),
    0x74: ("STZ zp,X", 2), 0x75: ("ADC zp,X", 2), 0x76: ("ROR zp,X", 2),
    0x78: ("SEI", 1), 0x79: ("ADC abs,Y", 3), 0x7A: ("PLY", 1),
    0x7C: ("JMP (abs,X)", 3), 0x7D: ("ADC abs,X", 3), 0x7E: ("ROR abs,X", 3),
    # 8x
    0x80: ("BRA rel", 2), 0x81: ("STA (zp,X)", 2), 0x84: ("STY zp", 2),
    0x85: ("STA zp", 2), 0x86: ("STX zp", 2), 0x88: ("DEY", 1),
    0x89: ("BIT #imm", 2), 0x8A: ("TXA", 1), 0x8C: ("STY abs", 3),
    0x8D: ("STA abs", 3), 0x8E: ("STX abs", 3),
    # 9x
    0x90: ("BCC rel", 2), 0x91: ("STA (zp),Y", 2), 0x92: ("STA (zp)", 2),
    0x94: ("STY zp,X", 2), 0x95: ("STA zp,X", 2), 0x96: ("STX zp,Y", 2),
    0x98: ("TYA", 1), 0x99: ("STA abs,Y", 3), 0x9A: ("TXS", 1),
    0x9C: ("STZ abs", 3), 0x9D: ("STA abs,X", 3), 0x9E: ("STZ abs,X", 3),
    # Ax
    0xA0: ("LDY #imm", 2), 0xA1: ("LDA (zp,X)", 2), 0xA2: ("LDX #imm", 2),
    0xA4: ("LDY zp", 2), 0xA5: ("LDA zp", 2), 0xA6: ("LDX zp", 2),
    0xA8: ("TAY", 1), 0xA9: ("LDA #imm", 2), 0xAA: ("TAX", 1),
    0xAC: ("LDY abs", 3), 0xAD: ("LDA abs", 3), 0xAE: ("LDX abs", 3),
    # Bx
    0xB0: ("BCS rel", 2), 0xB1: ("LDA (zp),Y", 2), 0xB2: ("LDA (zp)", 2),
    0xB4: ("LDY zp,X", 2), 0xB5: ("LDA zp,X", 2), 0xB6: ("LDX zp,Y", 2),
    0xB8: ("CLV", 1), 0xB9: ("LDA abs,Y", 3), 0xBA: ("TSX", 1),
    0xBC: ("LDY abs,X", 3), 0xBD: ("LDA abs,X", 3), 0xBE: ("LDX abs,Y", 3),
    # Cx
    0xC0: ("CPY #imm", 2), 0xC1: ("CMP (zp,X)", 2), 0xC4: ("CPY zp", 2),
    0xC5: ("CMP zp", 2), 0xC6: ("DEC zp", 2), 0xC8: ("INY", 1),
    0xC9: ("CMP #imm", 2), 0xCA: ("DEX", 1), 0xCB: ("WAI", 1),
    0xCC: ("CPY abs", 3), 0xCD: ("CMP abs", 3), 0xCE: ("DEC abs", 3),
    # Dx
    0xD0: ("BNE rel", 2), 0xD1: ("CMP (zp),Y", 2), 0xD2: ("CMP (zp)", 2),
    0xD5: ("CMP zp,X", 2), 0xD6: ("DEC zp,X", 2),
    0xD8: ("CLD", 1), 0xD9: ("CMP abs,Y", 3), 0xDA: ("PHX", 1),
    0xDB: ("STP", 1), 0xDD: ("CMP abs,X", 3), 0xDE: ("DEC abs,X", 3),
    # Ex
    0xE0: ("CPX #imm", 2), 0xE1: ("SBC (zp,X)", 2), 0xE4: ("CPX zp", 2),
    0xE5: ("SBC zp", 2), 0xE6: ("INC zp", 2), 0xE8: ("INX", 1),
    0xE9: ("SBC #imm", 2), 0xEA: ("NOP", 1), 0xEC: ("CPX abs", 3),
    0xED: ("SBC abs", 3), 0xEE: ("INC abs", 3),
    # Fx
    0xF0: ("BEQ rel", 2), 0xF1: ("SBC (zp),Y", 2), 0xF2: ("SBC (zp)", 2),
    0xF5: ("SBC zp,X", 2), 0xF6: ("INC zp,X", 2),
    0xF8: ("SED", 1), 0xF9: ("SBC abs,Y", 3), 0xFA: ("PLX", 1),
    0xFD: ("SBC abs,X", 3), 0xFE: ("INC abs,X", 3),
}


def _format_operand(template: str, data: bytes, offset: int, addr: int) -> str:
    """Format an instruction mnemonic, substituting operand values."""
    size = len(data)
    if "abs" in template and offset + 2 < size:
        operand = data[offset + 1] | (data[offset + 2] << 8)
        return template.replace("abs", f"${operand:04X}")
    if "rel" in template and offset + 1 < size:
        rel = data[offset + 1]
        target = addr + 2 + (rel if rel < 128 else rel - 256)
        return template.replace("rel", f"${target:04X}")
    if "zp" in template and offset + 1 < size:
        return template.replace("zp", f"${data[offset + 1]:02X}")
    if "imm" in template and offset + 1 < size:
        return template.replace("imm", f"${data[offset + 1]:02X}")
    return template


def disassemble(data: bytes | list[int], start: int = 0,
                length: int | None = None) -> list[str]:
    """Disassemble 6502/65C02 machine code.

    Args:
        data: Raw bytes to disassemble.
        start: Address of the first byte in *data*.
        length: Number of bytes to disassemble (default: all of *data*).

    Returns:
        List of formatted disassembly lines, e.g.
        ``"$F800: 20 3B F3  JSR $F33B"``.
    """
    if not isinstance(data, (bytes, bytearray)):
        data = bytes(data)
    if length is None:
        length = len(data)
    length = min(length, len(data))

    lines: list[str] = []
    offset = 0
    while offset < length:
        addr = start + offset
        opcode = data[offset]
        entry = _OPCODES.get(opcode)
        if entry is None:
            lines.append(f"${addr:04X}: {opcode:02X}          ???")
            offset += 1
            continue
        template, size = entry
        if offset + size > length:
            break
        raw = " ".join(f"{data[offset + i]:02X}" for i in range(size))
        detail = _format_operand(template, data, offset, addr)
        lines.append(f"${addr:04X}: {raw:<8s}  {detail}")
        offset += size
    return lines
