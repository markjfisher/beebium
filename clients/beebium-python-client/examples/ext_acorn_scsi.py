"""acorn-scsi adapter: the Acorn SCSI host adapter (1 MHz bus).

Requires --acorn-scsi. Enumerate the targets on the SCSI bus and read the
current bus phase / status register.
"""

from __future__ import annotations

from _demo import run
from beebium.client import Beebium
from beebium.ext.peripheral.acorn_scsi import AcornScsi


def demo(bbc: Beebium) -> None:
    scsi = bbc.extensions[AcornScsi]  # equivalently: AcornScsi.attach(bbc)

    print("targets:")
    for target in scsi.targets:
        present = "present" if target.present else "absent "
        print(f"  id {target.id}: {present} {target.device_type!r} {target.description}")

    status = scsi.bus_status
    selected = "none" if status.selected_target == 0xFF else status.selected_target
    print(f"bus: phase={status.phase} selected={selected} "
          f"status=${status.status_register:02X} irq={status.irq_pending}")


if __name__ == "__main__":
    run(demo, description="acorn-scsi target and bus inspection.",
        extra_args=["--acorn-scsi"])
