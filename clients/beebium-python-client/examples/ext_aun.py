"""AUN adapter: the Econet-over-IP transport (Acorn Universal Networking).

Requires the server to run AUN as its Econet transport (--aun). AUN is an Econet
*transport*, so it is reached through the transport facade:
``bbc.transport[Aun]`` (or ``Aun.attach(bbc)``) -- not ``bbc.extensions``.
"""

from __future__ import annotations

from _demo import run
from beebium.client.exceptions import EconetError
from beebium.ext.econet.aun import Aun


def demo(bbc):
    aun = bbc.transport[Aun]  # equivalently: Aun.attach(bbc)

    # Read-only surface always round-trips.
    print(f"status: {aun.status}")
    print(f"peers : {aun.peers}")

    # Peer-table edits and cable control need an active AUN backend (the
    # emulated machine must have brought Econet up); guard so the example runs
    # either way while still showing the API.
    try:
        aun.add_peer(net=1, stn=254, ip_address="192.168.1.10")
        aun.add_peer(net=1, stn=1, ip_address="192.168.1.11", port=32768)
        for peer in aun.peers:
            print(f"  net {peer.net} stn {peer.stn} -> "
                  f"{peer.ip_address}:{peer.port} ({peer.source.name})")
        aun.set_connected(False)  # unplug the virtual Econet cable
        aun.remove_peer(net=1, stn=1)
        print(f"peer count now: {aun.status.peer_count}")
    except EconetError as e:
        print(f"(peer operations need an active AUN backend: {e})")


if __name__ == "__main__":
    run(demo, description="AUN peer table and cable state.",
        extra_args=["--aun", "net=1"])
