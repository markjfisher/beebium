"""Simulate a faulty RAM chip by randomly flipping a bit in screen memory.

  Connects to an already-running Beebium emulator and corrupts a single byte
  in screen memory five times a second, as if one of the DRAM ICs has a
  stuck or flaky bit.
  """

import argparse
import random
import time

from beebium.client import Beebium

ADDRESS = 0x5000
FLIPS_PER_SECOND = 5

parser = argparse.ArgumentParser(description="Simulate a faulty RAM chip.")
parser.add_argument("--port", type=int, default=None, help="gRPC port number")
args = parser.parse_args()

target = f"localhost:{args.port}" if args.port else None

with Beebium.connect(target=target) as bbc:
    bus = bbc.memory.address.bus

    print(f"Simulating faulty RAM at ${ADDRESS:04X}")
    print("Press Ctrl-C to stop.")

    try:
        while True:
            bit = 1 << random.randint(0, 7)
            original = bus[ADDRESS]
            corrupted = original ^ bit
            bus[ADDRESS] = corrupted
            print(f"  ${ADDRESS:04X}: ${original:02X} -> ${corrupted:02X}(bit {bit.bit_length() - 1} flipped)")
            time.sleep(1.0 / FLIPS_PER_SECOND)
    except KeyboardInterrupt:
        print("\nStopped.")
