"""rpc-serial adapter: act as the device on the far end of the serial wire.

Requires --rpc-serial. The extension hands the BBC's serial port to this
client: queue bytes for the BBC to receive, and collect bytes it transmits.
(serial_demo.py drives a full ACIA round trip; this shows the adapter surface.)
"""

from __future__ import annotations

from _demo import run
from beebium.client import Beebium
from beebium.ext.peripheral.rpc_serial import RpcSerial


def demo(bbc: Beebium) -> None:
    rpc = bbc.extensions[RpcSerial]

    accepted = rpc.send(b"HELLO")
    print(f"queued {accepted} byte(s) for the BBC to receive")

    status = rpc.status
    print(f"pending: tx={status.tx_pending} (BBC->us) rx={status.rx_pending} (us->BBC)")

    collected = rpc.receive()  # bytes the BBC has transmitted so far
    print(f"collected from the BBC: {bytes(collected)!r}")


if __name__ == "__main__":
    run(demo, description="rpc-serial peer: send/receive/status.",
        extra_args=["--rpc-serial"])
