"""host-serial adapter: bridge the BBC serial port to a host pty or device.

Requires --host-serial. The scripting equivalent of the GUI panel: read the
bridge's mode / path / baud and re-point it. Launched here in pty mode, so the
BBC's serial line appears as a host pseudo-terminal.
"""

from __future__ import annotations

from _demo import run
from beebium.ext.peripheral.host_serial import HostSerial


def demo(bbc):
    host_serial = bbc.extensions[HostSerial]

    cfg = host_serial.get_config()
    print(f"mode={cfg.mode} path={cfg.path} baud={cfg.baud} open={cfg.serial_open}")
    if not cfg.serial_open and cfg.open_error:
        print(f"  open error: {cfg.open_error}")

    # Re-pointing is a partial update -- only the fields you pass change.
    # (Left commented so the example is side-effect-free by default.)
    #
    #   updated = host_serial.set_config(mode="device", path="/dev/ttyUSB0", baud=9600)
    #   print(updated)


if __name__ == "__main__":
    run(demo, description="host-serial bridge configuration.",
        extra_args=["--host-serial", "mode=pty"])
