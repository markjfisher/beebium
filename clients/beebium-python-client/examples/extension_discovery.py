"""Extensions: discover what the server loaded, and reach a typed adapter.

The client mirrors the server's two extension registries:

  * peripheral extensions (attach to a bus/port) -> bbc.extensions
  * Econet transports (provide the Econet wire)  -> bbc.transport

Each facade lists what's loaded and bridges it to a typed adapter, keyed by
name (generic, base type) or by adapter class (concrete type, autocompletes).
A uniform ``Adapter.attach(bbc)`` works for either -- the adapter knows its own
category.

Launched here with --rpc-serial and --aun so both registries have an entry.
"""

from __future__ import annotations

from _demo import run
from beebium.client import Beebium
from beebium.client.extension import (
    ECONET_ENTRY_POINT_GROUP,
    PERIPHERAL_ENTRY_POINT_GROUP,
    describe_adapter,
    installed_adapter_names,
)
from beebium.ext.econet.aun import Aun
from beebium.ext.peripheral.rpc_serial import RpcSerial


def demo(bbc: Beebium) -> None:
    # 1. Peripheral extensions the server loaded (PeripheralExtensionService).
    print("peripheral extensions loaded:")
    for info in bbc.extensions.loaded:
        print(f"  {info.name:<12} id={info.id[:8]} has_ui={info.has_ui} "
              f"attaches_to={list(info.attaches_to)}")

    # 2. Econet transports the server loaded (EconetTransportService).
    print("econet transports loaded:")
    for t in bbc.transport.list():
        print(f"  {t.name:<12} id={t.id[:8]} active={t.active}")

    # 3. Installed client adapters, per category registry.
    for group in (PERIPHERAL_ENTRY_POINT_GROUP, ECONET_ENTRY_POINT_GROUP):
        print(f"\nadapters in {group}:")
        for name in installed_adapter_names(group):
            print(f"  {name:<12} {describe_adapter(name, group, single_line=True)}")

    # 4. Reach a loaded extension's adapter -- peripheral via bbc.extensions,
    #    transport via bbc.transport; Adapter.attach(bbc) works for either.
    rpc = bbc.extensions[RpcSerial]          # peripheral: concrete type
    aun = bbc.transport[Aun]                 # transport:  concrete type
    print(f"\nrpc-serial via extensions: {type(rpc).__name__}")
    print(f"aun via transport        : {type(aun).__name__}")
    print(f"aun via attach           : {type(Aun.attach(bbc)).__name__}")


if __name__ == "__main__":
    run(demo, description="Discover loaded extensions and reach typed adapters.",
        extra_args=["--rpc-serial", "--aun", "net=1"])
