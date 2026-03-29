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

"""Build DFS floppy disc images containing tokenised BBC BASIC programs.

Uses oaknut-dfs to create SSD (single-sided disc) images for loading
test programs into the emulator via the floppy drive.
"""

from __future__ import annotations

from oaknut_dfs import DFS, ACORN_DFS_40T_SINGLE_SIDED

# BBC BASIC file metadata for DFS
BASIC_LOAD_ADDRESS = 0xFF1900   # PAGE when DFS is active
BASIC_EXEC_ADDRESS = 0xFF8023   # BASIC ROM entry point

# 40-track single-sided: 40 * 10 * 256 = 102400 bytes
SSD_SIZE = 102400


def build_test_disc(program_name: str, program_bytes: bytes) -> bytes:
    """Create a DFS SSD containing a tokenised BASIC program.

    Args:
        program_name: DFS filename (without $. prefix), max 7 chars.
        program_bytes: Tokenised BASIC program data from basictool.

    Returns:
        Raw bytes of the SSD disc image, ready to write to a file.
    """
    buffer = bytearray(SSD_SIZE)
    dfs = DFS.from_buffer(memoryview(buffer), ACORN_DFS_40T_SINGLE_SIDED)
    dfs.save(
        f"$.{program_name}",
        program_bytes,
        load_address=BASIC_LOAD_ADDRESS,
        exec_address=BASIC_EXEC_ADDRESS,
    )
    return bytes(buffer)
