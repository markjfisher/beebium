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

"""Wrapper for basictool -- BBC BASIC text-to-token converter.

basictool (https://github.com/ZornsLemma/basictool) is a C99 program
that tokenises BBC BASIC source text into the binary format expected by
BBC BASIC on the BBC Micro. This module provides a Python interface and
a pytest fixture that clones and builds it on demand.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

BASICTOOL_REPO = "https://github.com/ZornsLemma/basictool.git"
# Pin to a known-good commit on main. The v0.08 tag has a Clang build
# error (-Warray-parameter in lib6502.c) fixed on main but not yet released.
BASICTOOL_COMMIT = "ef652eb14ee0afcfe24479f0bf9cee1012733f15"


def build_basictool(build_dirpath: Path) -> Path:
    """Clone and build basictool, returning the path to the executable.

    Args:
        build_dirpath: Directory to clone into (must exist).

    Returns:
        Path to the built basictool executable.
    """
    # Clone and checkout the pinned commit. We can't use --branch with a
    # commit hash, so clone then checkout.
    subprocess.run(
        ["git", "clone", BASICTOOL_REPO, str(build_dirpath)],
        check=True, capture_output=True, text=True,
    )
    subprocess.run(
        ["git", "checkout", BASICTOOL_COMMIT],
        cwd=build_dirpath, check=True, capture_output=True, text=True,
    )
    src_dirpath = build_dirpath / "src"
    exe_name = "basictool.exe" if sys.platform == "win32" else "basictool"

    if sys.platform == "win32":
        subprocess.run(
            ["cmd", "/c", "make.bat"],
            cwd=src_dirpath, check=True, capture_output=True, text=True,
        )
    else:
        subprocess.run(
            ["make"],
            cwd=src_dirpath, check=True, capture_output=True, text=True,
        )

    # The Makefile builds the executable in the repo root (parent of src/)
    exe_filepath = build_dirpath / exe_name
    if not exe_filepath.exists():
        raise RuntimeError(f"basictool build failed: {exe_filepath} not found")
    return exe_filepath


def tokenise(source: str, basictool_filepath: Path) -> bytes:
    """Tokenise BBC BASIC text source into binary format.

    Args:
        source: Human-readable BBC BASIC program text
                (numbered lines, e.g. '10 PRINT "HELLO"').
        basictool_filepath: Path to the basictool executable.

    Returns:
        Tokenised binary data suitable for saving to a DFS disc image
        with load address &1900 and exec address &8023.
    """
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".bas", delete=False
    ) as src_file:
        src_file.write(source)
        src_filepath = Path(src_file.name)

    tok_filepath = src_filepath.with_suffix(".tok")
    try:
        result = subprocess.run(
            [str(basictool_filepath), "-t", str(src_filepath), str(tok_filepath)],
            check=True, capture_output=True, text=True,
        )
        return tok_filepath.read_bytes()
    except subprocess.CalledProcessError as e:
        raise RuntimeError(
            f"basictool tokenisation failed:\n"
            f"stdout: {e.stdout}\nstderr: {e.stderr}"
        ) from e
    finally:
        src_filepath.unlink(missing_ok=True)
        tok_filepath.unlink(missing_ok=True)
