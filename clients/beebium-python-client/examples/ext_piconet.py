"""Piconet adapter: the USB-serial bridge to real Econet hardware.

Requires the server to run Piconet as its Econet transport (--piconet). Without
a Piconet adapter physically plugged in, ``serial_open`` reports False -- the
extension still loads and its status is readable.
"""

from __future__ import annotations

from _demo import run
from beebium.client import Beebium
from beebium.ext.econet.piconet import Piconet


def demo(bbc: Beebium) -> None:
    piconet = Piconet.attach(bbc)  # equivalently: bbc.transport[Piconet]
    status = piconet.status
    print(f"device path : {status.device_path or '(none configured)'}")
    print(f"serial open : {status.serial_open}")


if __name__ == "__main__":
    # A device_path is required; a nonexistent one still loads the extension
    # (serial_open then reports False).
    run(demo, description="Piconet adapter status.",
        extra_args=["--piconet", "device_path=/dev/tty.beebium-demo"])
