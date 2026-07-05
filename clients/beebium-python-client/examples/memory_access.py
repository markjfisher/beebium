"""Memory: bus vs peek, ranges, typed casts, and named regions.

Memory access is explicit about side effects: ``bus`` reads/writes through the
memory bus like real hardware (so it can trigger I/O), while ``peek`` is
side-effect-free -- which matters at I/O addresses.
"""

from __future__ import annotations

from _demo import run
from beebium.client import Beebium


def demo(bbc: Beebium) -> None:
    bbc.debugger.stop()
    mem = bbc.memory

    # Single-byte bus access (side-effecting) vs peek (side-effect-free).
    mem.address.bus[0x0070] = 0x42
    print(f"$0070          = ${mem.address.bus[0x0070]:02X}")
    print(f"$FE40 (peek)   = ${mem.address.peek[0xFE40]:02X}  # System VIA ORB")

    # Range read/write with slices.
    mem.address.bus[0x0080:0x0084] = bytes([0xDE, 0xAD, 0xBE, 0xEF])
    print(f"$0080..$0083   = {bytes(mem.address.bus[0x0080:0x0084]).hex()}")

    # Typed access via struct format strings (little-endian 16-bit here).
    mem.address.bus.cast("<H")[0x0070] = 0x1234
    print(f"word at $0070  = ${mem.address.bus.cast('<H')[0x0070]:04X}")

    # Named regions bypass bank switching and address by region-relative offset.
    print(f"\nmachine: {mem.machine_type}")
    for region in mem.regions:
        base = f"${region.base_address:04X}" if region.base_address is not None else "-"
        print(f"  {region.name:<12} base={base} size={region.size}")
    print(f"main_ram[$1000] = ${mem.region('main_ram').bus[0x1000]:02X}")


if __name__ == "__main__":
    run(demo, description="Memory access: bus/peek, ranges, casts, regions.")
