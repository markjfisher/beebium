"""acorn-rtc adapter: the SAF3019P real-time clock.

Requires --acorn-rtc. The 7-bit-year register layout spans 1981-2099, so the
clock starts from the host's contemporary wall time; the SetTime RPC honours
that range too.
"""

from __future__ import annotations

from _demo import run

from beebium.client import Beebium
from beebium.ext.peripheral.acorn_rtc import AcornRtc


def demo(bbc: Beebium) -> None:
    rtc = AcornRtc.attach(bbc)  # equivalently: bbc.extensions[AcornRtc]

    print(f"time now  : {rtc.get_time().iso8601}")

    rtc.set_time("1985-06-15T14:30")
    now = rtc.get_time()
    print(f"after set : {now.iso8601}  (y={now.year} m={now.month} d={now.day})")

    print(f"registers : {[f'{r:02X}' for r in rtc.registers]}")


if __name__ == "__main__":
    run(
        demo,
        description="acorn-rtc clock read/set and raw registers.",
        extra_args=["--acorn-rtc", "layout=7bit-year-in-r7"],
    )
