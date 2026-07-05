"""Audio: read the output format and pull a few sample chunks off the stream.

AudioService describes its sources (the SN76489 and any add-on sound hardware)
and streams packed sample chunks. This is the sample stream, distinct from
``bbc.sound``, which introspects the SN76489's registers.
"""

from __future__ import annotations

from _demo import run
from beebium.client import Beebium


def demo(bbc: Beebium) -> None:
    fmt = bbc.audio.format
    print(f"{fmt.sample_rate} Hz, {fmt.source_count} source field(s):")
    for source in fmt.sources:
        print(f"  [{source.source_index}] {source.source_name} "
              f"{source.encoding.name} channels={list(source.channel_names)}")

    print("\nfirst three chunks:")
    stream = bbc.audio.subscribe(chunk_size=512)
    for _ in range(3):
        chunk = next(stream)
        print(f"  seq={chunk.sequence} samples={chunk.sample_count} "
              f"bytes={len(chunk.samples)}")


if __name__ == "__main__":
    run(demo, description="Read the audio format and stream sample chunks.")
