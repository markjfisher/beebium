"""Sideways ROM/RAM: read the slot topology and each socket's contents.

SidewaysService reports the paged-ROM sockets: their physical labels, the
logical slots each answers, whether they hold ROM or RAM, and any ROM header
found. Model B/B+ sockets are fixed; the ROM/RAM board exposes configurable ones.
"""

from __future__ import annotations

from _demo import run
from beebium.client import Beebium


def demo(bbc: Beebium) -> None:
    report = bbc.sideways.get_slot_status()
    print(f"{report.num_physical_slots} physical socket(s), "
          f"aliasing={report.has_aliasing}\n")

    for socket in report.sockets:
        slots = ",".join(str(s) for s in socket.aliased_slots)
        line = (f"  {socket.label:<6} slots[{slots:<8}] {socket.type.name:<5} "
                f"populated={socket.populated}")
        if socket.rom_header is not None:
            line += f"  header={socket.rom_header}"
        print(line)

    for link in report.motherboard_links:
        print(f"  link: {link}")


if __name__ == "__main__":
    run(demo, description="Read the sideways ROM/RAM slot topology.")
