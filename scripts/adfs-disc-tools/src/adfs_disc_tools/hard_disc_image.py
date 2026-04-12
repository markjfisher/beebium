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

"""Extract blank pre-formatted ADFS hard disc images.

The images come from Jon Ripley's BBC Micro Hard Drives page:
https://jonripley.com/8bit/HardDrives/

The archive BBCHDDs.zip contains nested zip files, each holding a single
.adl image. This module extracts a specific size, writes it as .dat,
and synthesises a .dsc geometry sidecar file.
"""

from __future__ import annotations

import importlib.resources
import io
import zipfile
from pathlib import Path

SECTORS_PER_TRACK = 33
SECTOR_SIZE = 256

# Available sizes in the archive (MB)
AVAILABLE_SIZES_MB = [2, 4, 8, 16, 20, 24, 32, 40, 48, 56, 64, 80, 96,
                      100, 128, 192, 200, 256, 300, 320, 400, 512]


def _blank_images_zip() -> Path:
    """Return the path to the bundled BBCHDDs.zip archive."""
    return importlib.resources.files("adfs_disc_tools.data").joinpath("BBCHDDs.zip")


def extract_blank_adfs_image(dest_filepath: Path, size_mb: int = 2) -> Path:
    """Extract a blank ADFS hard disc image of the given size.

    Creates both the .dat image file and a .dsc geometry sidecar file.

    The archive contains nested zips: BBCHDDs.zip -> NMbHDD.zip -> NMbHDD.adl.
    This function handles the double extraction transparently.

    Args:
        dest_filepath: Path for the .dat file.
        size_mb: Image size in megabytes (default 2). Must be one of
                 the sizes available in BBCHDDs.zip.

    Returns:
        Path to the .dat file.
    """
    if size_mb not in AVAILABLE_SIZES_MB:
        raise ValueError(
            f"No {size_mb} MB image available. "
            f"Choose from: {AVAILABLE_SIZES_MB}"
        )

    inner_zip_name = f"{size_mb}MbHDD.zip"
    adl_name = f"{size_mb}MbHDD.adl"

    blank_images_zip_filepath = _blank_images_zip()

    # Extract inner zip from outer archive, then extract .adl from inner zip
    with zipfile.ZipFile(blank_images_zip_filepath) as outer_zf:
        inner_zip_data = outer_zf.read(inner_zip_name)

    with zipfile.ZipFile(io.BytesIO(inner_zip_data)) as inner_zf:
        data = inner_zf.read(adl_name)

    # Write as .dat
    dest_filepath = Path(dest_filepath)
    dest_filepath.write_bytes(data)

    # Generate .dsc sidecar with geometry
    total_sectors = len(data) // SECTOR_SIZE
    heads = 4
    cylinders = total_sectors // (SECTORS_PER_TRACK * heads)
    if cylinders * SECTORS_PER_TRACK * heads < total_sectors:
        cylinders += 1

    dsc = bytearray(22)
    dsc[3] = 0x80                             # sector size flag (256 bytes)
    dsc[10] = 1                               # interleave
    dsc[12] = 1                               # reserved
    dsc[13] = (cylinders >> 8) & 0xFF         # cylinders high byte
    dsc[14] = cylinders & 0xFF                # cylinders low byte
    dsc[15] = heads                           # heads
    dsc[17] = 0x80                            # write precomp high
    dsc[19] = 0x80                            # reduced write current high

    dsc_filepath = dest_filepath.with_suffix(".dsc")
    dsc_filepath.write_bytes(dsc)

    return dest_filepath
