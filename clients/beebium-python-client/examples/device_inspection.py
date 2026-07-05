"""Device inspection: side-effect-free views of the support chips.

DeviceInspection exposes decoded state for the System/User VIA, the 6845 CRTC,
the Video ULA, the SN76489, and the addressable latch -- plus the named
hardware indicators (LEDs, motor). All reads are side-effect-free.
"""

from __future__ import annotations

from _demo import run

from beebium.client import Beebium


def demo(bbc: Beebium) -> None:
    bbc.debugger.stop()

    sv = bbc.system_via.state
    print(f"System VIA IFR: IRQ={sv.ifr_irq} T1={sv.ifr_t1} CA1/vsync={sv.ifr_ca1} CA2={sv.ifr_ca2}")

    crtc = bbc.crtc.state
    print(
        f"CRTC: {crtc.hdisplayed}x{crtc.vdisplayed} chars, "
        f"scanlines/row={crtc.max_scanline + 1}, interlaced={crtc.is_interlaced}"
    )

    ula = bbc.video_ula.state
    print(f"Video ULA: teletext={ula.teletext_mode} flash={ula.flash_select} fast_clock={ula.fast_clock}")

    tone0 = bbc.sound.state().tone(0)
    print(f"Sound tone 0 (MOS channel {tone0.mos_channel}): silent={tone0.is_silent}")

    print(f"Addressable latch: {bbc.addressable_latch.state}")

    print("Indicators:")
    for name, value in sorted(bbc.indicators.get_all().items()):
        print(f"  {name} = {value}")


if __name__ == "__main__":
    run(demo, description="Inspect VIA/CRTC/VideoULA/sound/latch/indicators.")
