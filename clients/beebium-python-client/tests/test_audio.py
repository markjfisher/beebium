# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

"""Tests for the AudioService client (bbc.audio).

The conversion test is server-free (builds an audio_pb2.AudioFormat directly).
The integration tests launch a real server and read the format and a chunk.
"""

from __future__ import annotations

from beebium.client._proto import audio_pb2
from beebium.client.audio import (
    AudioChunk,
    AudioFormat,
    AudioSource,
    SourceEncoding,
    _audio_format_from_proto,
)


# --------------------------------------------------------------------------
# Server-free conversion test
# --------------------------------------------------------------------------

def test_audio_format_from_proto_maps_sources_and_encoding():
    proto = audio_pb2.AudioFormat(sample_rate=48000, source_count=1)
    proto.sources.add(
        source_index=0,
        source_name="SN76489",
        encoding=audio_pb2.ENCODING_4X8BIT_UNSIGNED,
        channel_names=["0", "1", "2", "3"],
        group_id=0,
    )
    fmt = _audio_format_from_proto(proto)

    assert isinstance(fmt, AudioFormat)
    assert fmt.sample_rate == 48000
    assert fmt.source_count == 1
    assert len(fmt.sources) == 1
    source = fmt.sources[0]
    assert isinstance(source, AudioSource)
    assert source.source_name == "SN76489"
    assert source.encoding is SourceEncoding.ENCODING_4X8BIT_UNSIGNED
    assert source.channel_names == ("0", "1", "2", "3")


# --------------------------------------------------------------------------
# Integration tests (real server)
# --------------------------------------------------------------------------

def test_format_reports_sample_rate_and_sources(bbc):
    fmt = bbc.audio.format
    assert fmt.sample_rate > 0
    assert fmt.source_count >= 1
    # A BBC always has the SN76489 internal sound chip as a source.
    assert any("SN76489" in s.source_name for s in fmt.sources)


def test_subscribe_yields_a_chunk(bbc):
    chunk = next(bbc.audio.subscribe(chunk_size=256))
    assert isinstance(chunk, AudioChunk)
    assert chunk.sample_count > 0
    assert isinstance(chunk.samples, bytes)
    # samples is sample_count x source_count x 4 bytes.
    expected = chunk.sample_count * bbc.audio.format.source_count * 4
    assert len(chunk.samples) == expected
