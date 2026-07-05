"""Disc: inspect the controller and drives, and mount an image if given.

DiscService reports the controller and per-drive state (SSD/DSD/HFE images,
motor, current track). Pass ``--image PATH`` to insert a disc into drive 0.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from _demo import run

from beebium.client import Beebium


def demo(bbc: Beebium) -> None:
    disc = bbc.disc
    print(f"controller: present={disc.has_controller} type={disc.controller_type!r} socketed={disc.is_socketed}")
    for info in disc.available_controllers:
        print(f"  installable: {info}")

    # Optional: mount an image passed after `--` on the command line.
    image = _image_arg()
    if image is not None:
        meta = disc.drive0.insert(image)
        print(f"inserted {meta.name!r} ({meta.format}, {meta.sides} side(s))")

    for num in (0, 1):
        d = disc.drive(num)
        title = d.disc.name if d.disc else None
        print(
            f"drive {num} ({d.name}): state={d.state.name} loaded={d.is_loaded} track={d.current_track} disc={title!r}"
        )


def _image_arg() -> Path | None:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--image", type=Path, default=None)
    args, _ = parser.parse_known_args()
    return args.image


if __name__ == "__main__":
    run(demo, description="Inspect disc controller and drives (--image to mount).")
