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

"""CLI for ADFS hard disc image tools."""

from __future__ import annotations

from pathlib import Path

import click

from adfs_disc_tools.hard_disc_image import AVAILABLE_SIZES_MB, extract_blank_adfs_image


@click.group()
def cli() -> None:
    """Create blank pre-formatted ADFS hard disc images for BBC Micro emulators."""


@cli.command("list-sizes")
def list_sizes() -> None:
    """List available blank image sizes."""
    click.echo("Available sizes (MB):")
    for size_mb in AVAILABLE_SIZES_MB:
        click.echo(f"  {size_mb:>4}")


@cli.command("create-image")
@click.argument("size_mb", type=click.Choice([str(s) for s in AVAILABLE_SIZES_MB]))
@click.argument("dest", type=click.Path(dir_okay=False, path_type=Path))
def create_image(size_mb: str, dest: Path) -> None:
    """Create a blank ADFS hard disc image.

    SIZE_MB is the image size in megabytes.
    DEST is the output filepath for the .dat image.
    A .dsc geometry sidecar is created alongside it.
    """
    dest = dest.with_suffix(".dat")
    filepath = extract_blank_adfs_image(dest, size_mb=int(size_mb))
    dsc_filepath = filepath.with_suffix(".dsc")
    click.echo(f"Created {filepath} ({int(size_mb)} MB)")
    click.echo(f"Created {dsc_filepath}")
