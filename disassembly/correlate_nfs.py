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

#!/usr/bin/env python3
"""Cross-reference NFS 3.34 ROM with DNFS 3.60 Acorn source code.

Parses the original Acorn assembler source (NFS00-NFS13), extracts opcode
sequences, and correlates them against the NFS 3.34 ROM binary to identify
matching routines. Produces a human-readable report mapping reference labels
to ROM addresses.
"""

import os
import re
import struct
import sys
from collections import OrderedDict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ============================================================
# Configuration
# ============================================================

REFERENCE_DIR = Path("/Users/rjs/Code/acorn_1770_dfs_disassembly/submodules/AcornDNFSv300/src")
ROM_FILEPATH = Path("/Users/rjs/Code/beebium/roms/acorn-nfs_3_34.rom")
DISASSEMBLY_FILEPATH = Path("/Users/rjs/Code/beebium/disassembly/nfs_334_v2.py")

ROM_BASE = 0x8000
ROM_SIZE = 8192

# ROM offsets for relocated code
TUBE_ZP_ROM_OFFSET = 0x1307   # $9307 in ROM → runtime $0016
TUBE_ZP_RUNTIME    = 0x0016
TUBE_ZP_LENGTH     = 0x61     # 97 bytes

TUBE_P4_ROM_OFFSET = 0x134C   # $934C in ROM → runtime $0400
TUBE_P4_RUNTIME    = 0x0400
TUBE_P4_LENGTH     = 0x100

TUBE_P5_ROM_OFFSET = 0x144C   # $944C in ROM → runtime $0500
TUBE_P5_RUNTIME    = 0x0500
TUBE_P5_LENGTH     = 0x100

TUBE_P6_ROM_OFFSET = 0x154C   # $954C in ROM → runtime $0600
TUBE_P6_RUNTIME    = 0x0600
TUBE_P6_LENGTH     = 0x100

# ============================================================
# 6502 Opcode Length Table (256 entries)
# ============================================================
# 0 = invalid/undefined, 1 = implied/accumulator, 2 = immediate/zp/rel, 3 = absolute
OPCODE_LENGTHS = [
    # $00-$0F
    1, 2, 0, 0, 0, 2, 2, 0, 1, 2, 1, 0, 0, 3, 3, 0,
    # $10-$1F
    2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0,
    # $20-$2F
    3, 2, 0, 0, 2, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0,
    # $30-$3F
    2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0,
    # $40-$4F
    1, 2, 0, 0, 0, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0,
    # $50-$5F
    2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0,
    # $60-$6F
    1, 2, 0, 0, 0, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0,
    # $70-$7F
    2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0,
    # $80-$8F
    0, 2, 0, 0, 2, 2, 2, 0, 1, 0, 1, 0, 3, 3, 3, 0,
    # $90-$9F
    2, 2, 0, 0, 2, 2, 2, 0, 1, 3, 1, 0, 0, 3, 0, 0,
    # $A0-$AF
    2, 2, 2, 0, 2, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0,
    # $B0-$BF
    2, 2, 0, 0, 2, 2, 2, 0, 1, 3, 1, 0, 3, 3, 3, 0,
    # $C0-$CF
    2, 2, 0, 0, 2, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0,
    # $D0-$DF
    2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0,
    # $E0-$EF
    2, 2, 0, 0, 2, 2, 2, 0, 1, 2, 1, 0, 3, 3, 3, 0,
    # $F0-$FF
    2, 2, 0, 0, 0, 2, 2, 0, 1, 3, 0, 0, 0, 3, 3, 0,
]

# ============================================================
# Acorn Assembler Mnemonic → 6502 Opcode Mapping
# ============================================================
# Maps "MNEMONIC" or "MNEMONIC_SUFFIX" to (opcode, instruction_length).
# Suffixes: IM=immediate, AX=absolute,X, AY=absolute,Y, IX=(indirect,X),
#           IY=(indirect),Y, ZX=zeropage,X, A=accumulator, (none)=abs/zp
# JMI = JMP indirect

ACORN_MNEMONICS = {
    # LDA
    "LDA":   (0xAD, 3), "LDAIM": (0xA9, 2), "LDAAX": (0xBD, 3), "LDAAY": (0xB9, 3),
    "LDAIX": (0xA1, 2), "LDAIY": (0xB1, 2), "LDAZX": (0xB5, 2),
    # LDX
    "LDX":   (0xAE, 3), "LDXIM": (0xA2, 2), "LDXAY": (0xBE, 3), "LDXZY": (0xB6, 2),
    # LDY
    "LDY":   (0xAC, 3), "LDYIM": (0xA0, 2), "LDYAX": (0xBC, 3), "LDYZX": (0xB4, 2),
    # STA
    "STA":   (0x8D, 3), "STAAX": (0x9D, 3), "STAAY": (0x99, 3),
    "STAIX": (0x81, 2), "STAIY": (0x91, 2), "STAZX": (0x95, 2),
    # STX
    "STX":   (0x8E, 3), "STXZY": (0x96, 2),
    # STY
    "STY":   (0x8C, 3), "STYZX": (0x94, 2),
    # ADC
    "ADC":   (0x6D, 3), "ADCIM": (0x69, 2), "ADCAX": (0x7D, 3), "ADCAY": (0x79, 3),
    "ADCIX": (0x61, 2), "ADCIY": (0x71, 2), "ADCZX": (0x75, 2),
    # SBC
    "SBC":   (0xED, 3), "SBCIM": (0xE9, 2), "SBCAX": (0xFD, 3), "SBCAY": (0xF9, 3),
    "SBCIX": (0xE1, 2), "SBCIY": (0xF1, 2), "SBCZX": (0xF5, 2),
    # AND
    "AND":   (0x2D, 3), "ANDIM": (0x29, 2), "ANDAX": (0x3D, 3), "ANDAY": (0x39, 3),
    "ANDIX": (0x21, 2), "ANDIY": (0x31, 2), "ANDZX": (0x35, 2),
    # ORA
    "ORA":   (0x0D, 3), "ORAIM": (0x09, 2), "ORAAX": (0x1D, 3), "ORAAY": (0x19, 3),
    "ORAIX": (0x01, 2), "ORAIY": (0x11, 2), "ORAZX": (0x15, 2),
    # EOR
    "EOR":   (0x4D, 3), "EORIM": (0x49, 2), "EORAX": (0x5D, 3), "EORAY": (0x59, 3),
    "EORIX": (0x41, 2), "EORIY": (0x51, 2), "EORZX": (0x55, 2),
    # CMP
    "CMP":   (0xCD, 3), "CMPIM": (0xC9, 2), "CMPAX": (0xDD, 3), "CMPAY": (0xD9, 3),
    "CMPIX": (0xC1, 2), "CMPIY": (0xD1, 2), "CMPZX": (0xD5, 2),
    # CPX
    "CPX":   (0xEC, 3), "CPXIM": (0xE0, 2),
    # CPY
    "CPY":   (0xCC, 3), "CPYIM": (0xC0, 2),
    # BIT
    "BIT":   (0x2C, 3),
    # INC/DEC
    "INC":   (0xEE, 3), "INCAX": (0xFE, 3), "INCZX": (0xF6, 2),
    "DEC":   (0xCE, 3), "DECAX": (0xDE, 3), "DECZX": (0xD6, 2),
    # Shifts
    "ASL":   (0x0E, 3), "ASLA":  (0x0A, 1), "ASLAX": (0x1E, 3), "ASLZX": (0x16, 2),
    "LSR":   (0x4E, 3), "LSRA":  (0x4A, 1), "LSRAX": (0x5E, 3), "LSRZX": (0x56, 2),
    "ROL":   (0x2E, 3), "ROLA":  (0x2A, 1), "ROLAX": (0x3E, 3), "ROLZX": (0x36, 2),
    "ROR":   (0x6E, 3), "RORA":  (0x6A, 1), "RORAX": (0x7E, 3), "RORZX": (0x76, 2),
    # Branches
    "BCC":   (0x90, 2), "BCS":   (0xB0, 2), "BEQ":   (0xF0, 2), "BNE":   (0xD0, 2),
    "BMI":   (0x30, 2), "BPL":   (0x10, 2), "BVC":   (0x50, 2), "BVS":   (0x70, 2),
    # Jumps
    "JMP":   (0x4C, 3), "JSR":   (0x20, 3),
    "JMI":   (0x6C, 3),  # JMP indirect
    # Implied
    "BRK":   (0x00, 1), "NOP":   (0xEA, 1),
    "CLC":   (0x18, 1), "SEC":   (0x38, 1), "CLI":   (0x58, 1), "SEI":   (0x78, 1),
    "CLV":   (0xB8, 1), "CLD":   (0xD8, 1), "SED":   (0xF8, 1),
    "TAX":   (0xAA, 1), "TAY":   (0xA8, 1), "TXA":   (0x8A, 1), "TYA":   (0x98, 1),
    "TXS":   (0x9A, 1), "TSX":   (0xBA, 1),
    "PHA":   (0x48, 1), "PLA":   (0x68, 1), "PHP":   (0x08, 1), "PLP":   (0x28, 1),
    "INX":   (0xE8, 1), "INY":   (0xC8, 1), "DEX":   (0xCA, 1), "DEY":   (0x88, 1),
    "RTS":   (0x60, 1), "RTI":   (0x40, 1),
}


# ============================================================
# Data Structures
# ============================================================

@dataclass
class RefItem:
    """One item from the reference source: instruction, data, or label."""
    labels: list          # Labels at this address
    opcode: Optional[int] # 6502 opcode byte (None for data/labels)
    operand_bytes: list   # Operand bytes (may be empty or contain None for unresolved)
    size: int             # Total size in bytes (0 for label-only items)
    data_bytes: list      # Raw data bytes for '=' directives
    comments: list        # Comment strings from this line
    source_file: str      # NFS file name
    source_line: int      # Line number in source
    is_data: bool = False # True if this is a data directive

@dataclass
class CorrelationResult:
    """Result of correlating a reference label with ROM."""
    ref_label: str
    ref_address: Optional[int]  # Virtual address from reference
    rom_address: Optional[int]  # Matched ROM address
    confidence: float           # 0.0-1.0
    ref_comments: list          # Comments from reference
    existing_label: str         # Our existing label at that address
    source_file: str


# ============================================================
# Phase 1: Parse Reference Source
# ============================================================

class EquateParser:
    """Parse Acorn assembler equates (NAME * expr) and resolve expressions."""

    def __init__(self):
        self.symbols = {}
        self.unresolved = {}

    def parse_value(self, expr: str) -> Optional[int]:
        """Try to evaluate an expression to an integer value."""
        expr = expr.strip()
        if not expr:
            return None

        # Handle the high-byte operator /(expr)
        if expr.startswith("/"):
            inner = expr[1:].strip()
            if inner.startswith("(") and inner.endswith(")"):
                inner = inner[1:-1].strip()
            val = self.parse_value(inner)
            if val is not None:
                return (val >> 8) & 0xFF
            return None

        # Handle the ! (low byte) operator — e.g. !(/(expr))
        if expr.startswith("!"):
            inner = expr[1:].strip()
            if inner.startswith("(") and inner.endswith(")"):
                inner = inner[1:-1].strip()
            val = self.parse_value(inner)
            if val is not None:
                return val & 0xFF
            return None

        # Handle :SHR: operator
        if ":SHR:" in expr.upper():
            parts = re.split(r':SHR:', expr, flags=re.IGNORECASE)
            if len(parts) == 2:
                left = self.parse_value(parts[0])
                right = self.parse_value(parts[1])
                if left is not None and right is not None:
                    return (left >> right) & 0xFFFF
            return None

        # Handle parenthesised expressions
        if expr.startswith("(") and expr.endswith(")"):
            return self.parse_value(expr[1:-1])

        # Try hex literal (&xx)
        if expr.startswith("&") or expr.startswith("$"):
            try:
                return int(expr[1:], 16)
            except ValueError:
                return None

        # Try decimal literal
        try:
            return int(expr)
        except ValueError:
            pass

        # Try to split on + or - (respecting parentheses)
        # Find the rightmost + or - not inside parentheses
        depth = 0
        for i in range(len(expr) - 1, 0, -1):
            c = expr[i]
            if c == ')':
                depth += 1
            elif c == '(':
                depth -= 1
            elif depth == 0 and c in '+-':
                left = self.parse_value(expr[:i])
                right = self.parse_value(expr[i + 1:])
                if left is not None and right is not None:
                    if c == '+':
                        return (left + right) & 0xFFFF
                    else:
                        return (left - right) & 0xFFFF
                return None

        # Try * (multiply)
        depth = 0
        for i in range(len(expr) - 1, 0, -1):
            c = expr[i]
            if c == ')':
                depth += 1
            elif c == '(':
                depth -= 1
            elif depth == 0 and c == '*':
                left = self.parse_value(expr[:i])
                right = self.parse_value(expr[i + 1:])
                if left is not None and right is not None:
                    return (left * right) & 0xFFFF
                return None

        # Try string literal "x"
        if expr.startswith('"') and expr.endswith('"') and len(expr) == 3:
            return ord(expr[1])

        # Try symbol lookup
        name = expr.strip()
        if name in self.symbols:
            return self.symbols[name]

        # Handle '.' (current PC) — will be set by caller
        if name == '.':
            return self.symbols.get('.', None)

        return None

    def parse_equates_from_file(self, filepath: Path):
        """Parse all equates from a single source file."""
        try:
            with open(filepath, 'r', encoding='latin-1') as f:
                lines = f.readlines()
        except FileNotFoundError:
            print(f"Warning: {filepath} not found", file=sys.stderr)
            return

        for line_no, line in enumerate(lines, 1):
            # Strip comments
            comment_pos = line.find(';')
            if comment_pos >= 0:
                code = line[:comment_pos]
            else:
                code = line

            code = code.rstrip()
            if not code.strip():
                continue

            # Look for equate: NAME * expr (where NAME starts at column 0 or after whitespace)
            # The pattern is: identifier followed by * followed by expression
            m = re.match(r'^(\w+)\s+\*\s+(.+)$', code.strip())
            if m:
                name = m.group(1)
                expr = m.group(2).strip()
                val = self.parse_value(expr)
                if val is not None:
                    self.symbols[name] = val
                else:
                    self.unresolved[name] = expr

    def resolve_pass(self) -> int:
        """One resolution pass over unresolved symbols. Returns count resolved."""
        resolved = 0
        still_unresolved = {}
        for name, expr in self.unresolved.items():
            val = self.parse_value(expr)
            if val is not None:
                self.symbols[name] = val
                resolved += 1
            else:
                still_unresolved[name] = expr
        self.unresolved = still_unresolved
        return resolved

    def resolve_all(self, max_passes=20):
        """Iteratively resolve all equates."""
        for _ in range(max_passes):
            if self.resolve_pass() == 0:
                break


class SourceParser:
    """Parse Acorn 6502 assembler source into reference items."""

    def __init__(self, symbols: dict):
        self.symbols = symbols
        self.items = []          # List of RefItem
        self.labels_at_pc = []   # Labels accumulated for next instruction
        self.pc = None           # Current virtual PC
        self.cond_stack = []     # Stack of (include_this_branch, seen_else)
        self.source_file = ""

    def is_included(self):
        """Check if current code is in an included conditional branch."""
        for include, _ in self.cond_stack:
            if not include:
                return False
        return True

    def parse_operand_value(self, operand_str: str) -> Optional[int]:
        """Parse an operand expression to a value."""
        parser = EquateParser()
        parser.symbols = dict(self.symbols)
        if self.pc is not None:
            parser.symbols['.'] = self.pc
        return parser.parse_value(operand_str)

    def parse_data_directive(self, rest: str, line_no: int, comment: str):
        """Parse = byte, byte, ... or = "string" directives."""
        if not self.is_included():
            return

        data_bytes = []
        rest = rest.strip()

        # Tokenise: split on commas but respect quoted strings
        tokens = []
        current = ""
        in_string = False
        for ch in rest:
            if ch == '"':
                in_string = not in_string
                current += ch
            elif ch == ',' and not in_string:
                tokens.append(current.strip())
                current = ""
            else:
                current += ch
        if current.strip():
            tokens.append(current.strip())

        for token in tokens:
            token = token.strip()
            if not token:
                continue
            if token.startswith('"') and token.endswith('"'):
                # String literal
                for ch in token[1:-1]:
                    data_bytes.append(ord(ch))
            else:
                val = self.parse_operand_value(token)
                if val is not None:
                    data_bytes.append(val & 0xFF)
                else:
                    data_bytes.append(None)  # Unresolved

        if data_bytes:
            item = RefItem(
                labels=list(self.labels_at_pc),
                opcode=None,
                operand_bytes=[],
                size=len(data_bytes),
                data_bytes=data_bytes,
                comments=[comment] if comment else [],
                source_file=self.source_file,
                source_line=line_no,
                is_data=True,
            )
            self.items.append(item)
            self.labels_at_pc = []
            if self.pc is not None:
                self.pc += len(data_bytes)

    def parse_word_directive(self, rest: str, line_no: int, comment: str):
        """Parse & addr (16-bit word pointer, low-high) directives."""
        if not self.is_included():
            return

        rest = rest.strip()
        val = self.parse_operand_value(rest)
        data_bytes = []
        if val is not None:
            data_bytes = [val & 0xFF, (val >> 8) & 0xFF]
        else:
            data_bytes = [None, None]

        item = RefItem(
            labels=list(self.labels_at_pc),
            opcode=None,
            operand_bytes=[],
            size=2,
            data_bytes=data_bytes,
            comments=[comment] if comment else [],
            source_file=self.source_file,
            source_line=line_no,
            is_data=True,
        )
        self.items.append(item)
        self.labels_at_pc = []
        if self.pc is not None:
            self.pc += 2

    def parse_instruction(self, mnemonic: str, operand_str: str, line_no: int, comment: str):
        """Parse a 6502 instruction."""
        if not self.is_included():
            return

        mnemonic_upper = mnemonic.upper()
        if mnemonic_upper not in ACORN_MNEMONICS:
            return

        opcode, size = ACORN_MNEMONICS[mnemonic_upper]

        operand_bytes = []
        if size >= 2:
            val = self.parse_operand_value(operand_str) if operand_str else None
            if val is not None:
                if size == 2:
                    # For branches, compute relative offset
                    if opcode in (0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0):
                        if self.pc is not None:
                            rel = (val - (self.pc + 2)) & 0xFF
                            operand_bytes = [rel]
                        else:
                            operand_bytes = [None]
                    else:
                        operand_bytes = [val & 0xFF]
                else:  # size == 3
                    operand_bytes = [val & 0xFF, (val >> 8) & 0xFF]
            else:
                operand_bytes = [None] * (size - 1)

        item = RefItem(
            labels=list(self.labels_at_pc),
            opcode=opcode,
            operand_bytes=operand_bytes,
            size=size,
            data_bytes=[],
            comments=[comment] if comment else [],
            source_file=self.source_file,
            source_line=line_no,
        )
        self.items.append(item)
        self.labels_at_pc = []
        if self.pc is not None:
            self.pc += size

    def eval_condition(self, cond_expr: str) -> Optional[int]:
        """Evaluate a conditional expression which may contain '=' equality tests.

        In Acorn assembler, 'expr1=expr2' returns 0 if equal (TRUE),
        or non-zero ($FFFF) if not equal (FALSE).
        """
        # Check for equality comparison (not inside quotes)
        depth = 0
        for i, ch in enumerate(cond_expr):
            if ch in '(':
                depth += 1
            elif ch in ')':
                depth -= 1
            elif ch == '=' and depth == 0:
                left = self.parse_operand_value(cond_expr[:i])
                right = self.parse_operand_value(cond_expr[i + 1:])
                if left is not None and right is not None:
                    return 0 if left == right else 0xFFFF
                return None

        # No '=', evaluate as plain expression
        return self.parse_operand_value(cond_expr)

    def parse_conditional(self, line: str):
        """Handle conditional assembly [ cond / code / | / else / ]."""
        stripped = line.strip()

        if stripped.startswith("["):
            # Evaluate condition
            cond_expr = stripped[1:].strip()
            val = self.eval_condition(cond_expr)
            # Acorn assembler: condition true if value = 0
            include = (val == 0) if val is not None else True
            self.cond_stack.append((include, False))
            return True

        if stripped.startswith("|"):
            if self.cond_stack:
                include, _ = self.cond_stack[-1]
                self.cond_stack[-1] = (not include, True)
            return True

        if stripped.startswith("]"):
            if self.cond_stack:
                self.cond_stack.pop()
            return True

        return False

    def parse_file(self, filepath: Path, org: Optional[int] = None):
        """Parse a single source file."""
        try:
            with open(filepath, 'r', encoding='latin-1') as f:
                lines = f.readlines()
        except FileNotFoundError:
            print(f"Warning: {filepath} not found", file=sys.stderr)
            return

        self.source_file = filepath.name

        for line_no, raw_line in enumerate(lines, 1):
            line = raw_line.rstrip('\n\r')

            # Handle conditionals
            stripped = line.strip()
            if self.parse_conditional(stripped):
                continue

            # Extract comment
            comment = ""
            comment_pos = line.find(';')
            if comment_pos >= 0:
                comment = line[comment_pos + 1:].strip()
                code = line[:comment_pos]
            else:
                code = line

            code = code.rstrip()
            if not code.strip():
                continue

            # Skip directives: TTL, OPT, ORG (handled separately), LNK, END, <, >
            stripped_code = code.strip()
            first_word = stripped_code.split()[0].upper() if stripped_code.split() else ""
            if first_word in ('TTL', 'OPT', 'LNK', 'END', '<', '>'):
                continue

            # Handle ORG directive
            if first_word == 'ORG':
                rest = stripped_code[3:].strip()
                val = self.parse_operand_value(rest)
                if val is not None:
                    self.pc = val
                continue

            # Check for equate: NAME * expr
            m = re.match(r'^(\w+)\s+\*\s+(.+)$', stripped_code)
            if m:
                name = m.group(1)
                expr = m.group(2).strip()
                # Update PC-dependent equates
                val = self.parse_operand_value(expr)
                if val is not None:
                    self.symbols[name] = val
                continue

            # Check for label at start of line (non-indented, no instruction)
            # Labels are identifiers at column 0 (or close to it) not followed by *
            label_match = re.match(r'^([A-Za-z_]\w*)', code)
            if label_match and not code[0].isspace():
                label_name = label_match.group(1)
                # Check if the rest is an instruction or data directive
                rest_of_line = code[label_match.end():].strip()

                # Record the label
                self.labels_at_pc.append(label_name)

                # If there's no instruction after the label, just record label
                if not rest_of_line:
                    continue

                # Otherwise fall through to parse what follows
                stripped_code = rest_of_line

            # Check for instruction on indented line
            if code[0:1].isspace() or (label_match and stripped_code):
                parts = stripped_code.split(None, 1)
                if not parts:
                    continue

                mnemonic = parts[0]
                operand = parts[1].strip() if len(parts) > 1 else ""

                # Strip inline comment from operand
                opc = operand.find(';')
                if opc >= 0:
                    operand = operand[:opc].strip()

                # Data directive: = bytes
                if mnemonic == '=':
                    self.parse_data_directive(operand, line_no, comment)
                    continue

                # Word directive: & addr (16-bit pointer)
                if mnemonic == '&':
                    self.parse_word_directive(operand, line_no, comment)
                    continue

                # Skip assembler directives that aren't instructions
                if mnemonic.upper() in ('TTL', 'OPT', 'ORG', 'LNK', 'END', '<', '>', 'TUBE'):
                    continue

                # Try as a 6502 instruction
                if mnemonic.upper() in ACORN_MNEMONICS:
                    self.parse_instruction(mnemonic, operand, line_no, comment)
                # else: might be a label followed by instruction on same line, or unknown


def parse_all_reference_files(ref_dir: Path):
    """Parse all NFS00-NFS13 files and return (symbols, items_by_file)."""
    # First pass: collect all equates
    equates = EquateParser()
    for i in range(14):
        filepath = ref_dir / f"NFS{i:02d}"
        equates.parse_equates_from_file(filepath)

    equates.resolve_all()

    # Second pass: parse instructions
    # Each file parsed in order (as the assembler would LNK them)
    parser = SourceParser(equates.symbols)
    items_by_file = {}

    for i in range(14):
        filepath = ref_dir / f"NFS{i:02d}"
        start_idx = len(parser.items)
        parser.parse_file(filepath)
        end_idx = len(parser.items)
        if end_idx > start_idx:
            items_by_file[f"NFS{i:02d}"] = parser.items[start_idx:end_idx]

    return equates.symbols, parser.items, items_by_file


# ============================================================
# Phase 2: Extract ROM Opcodes
# ============================================================

def load_rom(filepath: Path) -> bytes:
    """Load the ROM binary."""
    with open(filepath, 'rb') as f:
        return f.read()


def decode_rom_instructions(rom_data: bytes, base: int, start: int, length: int):
    """Decode instruction stream from ROM, returning {runtime_addr: opcode_byte}."""
    opcodes = {}
    offset = 0
    while offset < length:
        runtime_addr = base + offset
        rom_offset = start + offset
        if rom_offset >= len(rom_data):
            break
        opcode = rom_data[rom_offset]
        opcodes[runtime_addr] = opcode
        inst_len = OPCODE_LENGTHS[opcode]
        if inst_len == 0:
            # Unknown opcode — skip 1 byte
            offset += 1
        else:
            offset += inst_len
    return opcodes


def extract_rom_bytes(rom_data: bytes, rom_offset: int, runtime_base: int, length: int):
    """Extract raw byte map from ROM: {runtime_addr: byte}."""
    result = {}
    for i in range(length):
        if rom_offset + i < len(rom_data):
            result[runtime_base + i] = rom_data[rom_offset + i]
    return result


# ============================================================
# Phase 3: Correlate and Report
# ============================================================

def build_label_address_map(items: list) -> dict:
    """Build label → virtual_address mapping from parsed items."""
    label_map = {}
    current_addr = None

    for item in items:
        # If item has labels, record them at the current address
        for lbl in item.labels:
            if current_addr is not None:
                label_map[lbl] = current_addr

        if item.size > 0 and current_addr is not None:
            current_addr += item.size
        elif item.size > 0:
            pass  # No PC tracking

    return label_map


def correlate_tube_code(items: list, rom_data: bytes,
                        rom_offset: int, runtime_base: int, length: int):
    """Correlate reference items against ROM for a Tube code region.

    Uses opcode fingerprint matching (sliding window) because the code
    differs between NFS 3.34 and DNFS 3.60 due to version patches.
    Each labeled routine is matched independently.
    """
    # Build routines: group consecutive items under their labels
    routines = []
    current_labels = []
    current_items = []

    for item in items:
        if item.labels:
            if current_items and current_labels:
                routines.append((current_labels, current_items))
            current_labels = list(item.labels)
            current_items = [item]
        elif item.size > 0:
            current_items.append(item)

    if current_items and current_labels:
        routines.append((current_labels, current_items))

    # Extract ROM byte slice for this region
    region_bytes = rom_data[rom_offset:rom_offset + length]

    results = []
    total_opcodes = 0
    total_matched = 0

    for labels, routine_items in routines:
        # Build opcode fingerprint
        fingerprint = []
        ref_comments = []
        for item in routine_items:
            if item.opcode is not None:
                fingerprint.append(item.opcode)
            ref_comments.extend(item.comments)

        if len(fingerprint) < 1:
            # Data-only item — try byte matching
            data_bytes = []
            for item in routine_items:
                if item.is_data:
                    for b in item.data_bytes:
                        if b is not None:
                            data_bytes.append(b)
            if not data_bytes:
                for lbl in labels:
                    results.append({
                        'label': lbl,
                        'rom_address': None,
                        'confidence': 0.0,
                        'matched': 0,
                        'total': 0,
                        'comments': ref_comments[:2],
                        'source_file': routine_items[0].source_file if routine_items else "",
                    })
                continue
            # Search for data byte sequence
            fingerprint = data_bytes

        # Sliding window search within this region
        best_offset = None
        best_score = 0

        for start in range(len(region_bytes)):
            match_count = 0
            pos = start

            for ref_opcode in fingerprint:
                if pos >= len(region_bytes):
                    break
                if region_bytes[pos] == ref_opcode:
                    match_count += 1
                    # Advance by instruction length for opcode matches
                    inst_len = OPCODE_LENGTHS[region_bytes[pos]]
                    if inst_len == 0:
                        pos += 1
                    else:
                        pos += inst_len
                else:
                    break

            if match_count > best_score:
                best_score = match_count
                best_offset = start

        confidence = best_score / len(fingerprint) if fingerprint else 0.0
        rom_addr = runtime_base + best_offset if best_offset is not None else None

        total_opcodes += len(fingerprint)
        total_matched += best_score

        for lbl in labels:
            results.append({
                'label': lbl,
                'rom_address': rom_addr,
                'confidence': confidence,
                'matched': best_score,
                'total': len(fingerprint),
                'comments': ref_comments[:2],
                'source_file': routine_items[0].source_file if routine_items else "",
            })

    overall_pct = (total_matched / total_opcodes * 100) if total_opcodes > 0 else 0
    return results, total_opcodes, total_matched, overall_pct


def correlate_main_rom(items: list, rom_data: bytes, symbols: dict):
    """Correlate main ROM code using opcode fingerprinting with sliding window.

    Strategy:
    1. Build opcode-only fingerprints from reference routines.
    2. Search for matching fingerprints in the ROM using a sliding window.
    3. Score matches by opcode agreement.
    """
    results = []

    # Build label → items mapping (group items by their preceding labels)
    routines = []
    current_labels = []
    current_items = []

    for item in items:
        if item.labels:
            if current_items and current_labels:
                routines.append((current_labels, current_items))
            current_labels = list(item.labels)
            current_items = [item]
        elif item.size > 0:
            current_items.append(item)

    if current_items and current_labels:
        routines.append((current_labels, current_items))

    for labels, routine_items in routines:
        # Build opcode fingerprint (sequence of opcode bytes only)
        fingerprint = []
        total_ref_bytes = 0
        ref_comments = []

        for item in routine_items:
            if item.opcode is not None:
                fingerprint.append(item.opcode)
                total_ref_bytes += item.size
            elif item.is_data:
                total_ref_bytes += item.size
            ref_comments.extend(item.comments)

        if len(fingerprint) < 3:
            continue  # Too short to match reliably

        # Search for fingerprint in ROM
        best_offset = None
        best_score = 0

        for rom_offset in range(ROM_SIZE - len(fingerprint)):
            rom_addr = ROM_BASE + rom_offset
            # Decode instructions at this offset and extract opcode sequence
            match_count = 0
            pos = rom_offset
            for ref_opcode in fingerprint:
                if pos >= ROM_SIZE:
                    break
                rom_opcode = rom_data[pos]
                if rom_opcode == ref_opcode:
                    match_count += 1
                    inst_len = OPCODE_LENGTHS[rom_opcode]
                    if inst_len == 0:
                        break
                    pos += inst_len
                else:
                    break

            if match_count >= min(5, len(fingerprint)) and match_count > best_score:
                best_score = match_count
                best_offset = rom_offset

        confidence = best_score / len(fingerprint) if fingerprint else 0.0

        for lbl in labels:
            results.append({
                'label': lbl,
                'ref_address': None,  # Virtual PC not tracked in main ROM
                'rom_address': ROM_BASE + best_offset if best_offset is not None else None,
                'confidence': confidence,
                'matched_opcodes': best_score,
                'total_opcodes': len(fingerprint),
                'comments': ref_comments[:3],  # First few comments
                'source_file': routine_items[0].source_file if routine_items else "",
            })

    return results


def parse_existing_labels(filepath: Path) -> dict:
    """Parse label() calls from nfs_334_v2.py to build addr → name map."""
    labels = {}
    try:
        with open(filepath, 'r') as f:
            for line in f:
                m = re.match(r'\s*label\(0x([0-9A-Fa-f]+),\s*"([^"]+)"\)', line)
                if m:
                    addr = int(m.group(1), 16)
                    name = m.group(2)
                    labels[addr] = name
    except FileNotFoundError:
        pass
    return labels


# ============================================================
# Report Generation
# ============================================================

def generate_report(tube_stats: list, main_results: list, existing_labels: dict):
    """Generate human-readable correlation report."""
    lines = []
    lines.append("=" * 78)
    lines.append("NFS 3.34 / DNFS 3.60 Cross-Reference Report")
    lines.append("=" * 78)
    lines.append("")

    # Tube code results
    lines.append("-" * 78)
    lines.append("TUBE CODE CORRELATION (opcode fingerprint matching)")
    lines.append("-" * 78)
    for region_name, results, total, matched, overall_pct in tube_stats:
        lines.append(f"\n  {region_name}: {matched}/{total} opcodes match ({overall_pct:.1f}%)")
        lines.append(f"  {'Label':<20s} {'ROM Addr':>8s} {'Conf':>5s} {'Match':>7s}  Comments")
        lines.append(f"  {'─'*20} {'─'*8} {'─'*5} {'─'*7}  {'─'*40}")
        for r in results:
            rom_addr = r.get('rom_address')
            existing = existing_labels.get(rom_addr, "") if rom_addr else ""
            rom_str = f"${rom_addr:04X}" if rom_addr is not None else "   ????"
            conf_str = f"{r['confidence']*100:.0f}%" if r['total'] > 0 else "  -"
            match_str = f"{r['matched']}/{r['total']}" if r['total'] > 0 else "  -"
            comment = r['comments'][0][:40] if r['comments'] else ""
            lines.append(f"  {r['label']:<20s} {rom_str:>8s} {conf_str:>5s} {match_str:>7s}  {comment}")
            if existing:
                lines.append(f"  {'':20s}          ours: {existing}")

    # Main ROM results
    lines.append("")
    lines.append("-" * 78)
    lines.append("MAIN ROM CORRELATION (opcode fingerprint matching)")
    lines.append("-" * 78)
    lines.append(f"\n  {'Label':<20s} {'ROM Addr':>8s} {'Conf':>5s} {'Match':>7s}  Comments")
    lines.append(f"  {'─'*20} {'─'*8} {'─'*5} {'─'*7}  {'─'*40}")

    # Sort by ROM address for readability
    main_with_addr = [r for r in main_results if r['rom_address'] is not None and r['confidence'] >= 0.3]
    main_sorted = sorted(main_with_addr, key=lambda r: r['rom_address'])

    for r in main_sorted:
        rom_str = f"${r['rom_address']:04X}"
        existing = existing_labels.get(r['rom_address'], "")
        conf_str = f"{r['confidence']*100:.0f}%"
        match_str = f"{r['matched_opcodes']}/{r['total_opcodes']}"
        comment = r['comments'][0][:40] if r['comments'] else ""
        lines.append(f"  {r['label']:<20s} {rom_str:>8s} {conf_str:>5s} {match_str:>7s}  {comment}")
        if existing:
            lines.append(f"  {'':20s}          ours: {existing}")

    # Summary of low-confidence matches
    low_conf = [r for r in main_results if r['confidence'] < 0.3 or r['rom_address'] is None]
    if low_conf:
        lines.append(f"\n  ({len(low_conf)} labels with confidence < 30% omitted)")

    lines.append("")
    lines.append("=" * 78)
    lines.append("END OF REPORT")
    lines.append("=" * 78)

    return "\n".join(lines)


# ============================================================
# Main
# ============================================================

def main():
    print("Phase 1: Parsing reference source files...")
    symbols, all_items, items_by_file = parse_all_reference_files(REFERENCE_DIR)
    print(f"  Parsed {len(symbols)} symbols, {len(all_items)} items from {len(items_by_file)} files")

    # Show some key symbols for verification
    verify_symbols = ['PREPLY', 'FSLOCN', 'PCMND', 'RTUBE', 'ZPTUBE', 'RXCBS']
    for name in verify_symbols:
        if name in symbols:
            print(f"  {name} = ${symbols[name]:04X}")
        else:
            print(f"  {name} = UNRESOLVED")

    print("\nPhase 2: Loading ROM binary...")
    rom_data = load_rom(ROM_FILEPATH)
    print(f"  Loaded {len(rom_data)} bytes")

    print("\nPhase 3: Correlating...")

    # Correlate Tube code using opcode fingerprinting
    tube_stats = []

    if 'NFS11' in items_by_file:
        r, t, m, pct = correlate_tube_code(
            items_by_file['NFS11'], rom_data,
            TUBE_ZP_ROM_OFFSET, TUBE_ZP_RUNTIME, TUBE_ZP_LENGTH)
        tube_stats.append(("NFS11 (Tube ZP, $0016-$0076)", r, t, m, pct))

    if 'NFS12' in items_by_file:
        r, t, m, pct = correlate_tube_code(
            items_by_file['NFS12'], rom_data,
            TUBE_P4_ROM_OFFSET, TUBE_P4_RUNTIME, TUBE_P4_LENGTH)
        tube_stats.append(("NFS12 (Tube P4, $0400-$04FF)", r, t, m, pct))

    if 'NFS13' in items_by_file:
        # Page 5 and page 6 are contiguous in ROM
        r, t, m, pct = correlate_tube_code(
            items_by_file['NFS13'], rom_data,
            TUBE_P5_ROM_OFFSET, TUBE_P5_RUNTIME, TUBE_P5_LENGTH + TUBE_P6_LENGTH)
        tube_stats.append(("NFS13 (Tube P5+P6, $0500-$06FF)", r, t, m, pct))

    # Correlate main ROM
    main_items = []
    for fname in ['NFS01', 'NFS02', 'NFS03', 'NFS04', 'NFS05', 'NFS06',
                   'NFS07', 'NFS08', 'NFS09', 'NFS10']:
        if fname in items_by_file:
            main_items.extend(items_by_file[fname])

    print(f"  Main ROM: {len(main_items)} reference items to correlate")
    main_results = correlate_main_rom(main_items, rom_data, symbols)

    # Parse existing labels
    existing_labels = parse_existing_labels(DISASSEMBLY_FILEPATH)
    print(f"  Found {len(existing_labels)} existing labels in disassembly")

    # Generate report
    print("\nGenerating report...")
    report = generate_report(tube_stats, main_results, existing_labels)
    print(report)

    # Also write to file
    report_filepath = Path(__file__).parent / "correlation_report.txt"
    with open(report_filepath, 'w') as f:
        f.write(report)
    print(f"\nReport written to {report_filepath}")


if __name__ == "__main__":
    main()
