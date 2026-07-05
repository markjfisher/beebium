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

"""Tests for frame geometry stability across successive video frames.

Verifies that alternate video fields/frames have identical renderable
geometry (width, height, borders, regions) to prevent display shimmer
caused by the interlace dummy raster producing different frame dimensions.
"""

from __future__ import annotations

import pytest

from beebium.client import Beebium
from beebium.client.screen import screen_contains
from beebium.client.video import Frame

NUM_FRAMES = 10  # Frames to compare for stability
SETTLE_FRAMES = 5  # Frames to discard while CRTC timing stabilises


def _capture_frames_while_running(bbc: Beebium, num_frames: int, settle_frames: int = SETTLE_FRAMES) -> list[Frame]:
    """Capture frames while the emulator is running, discarding early ones.

    The first few frames after streaming starts may have incomplete border
    geometry because the CRTC has not yet completed a full stable display
    cycle.  Discard ``settle_frames`` before returning ``num_frames`` for
    comparison.
    """
    frames = []
    with bbc.debugger.running():
        for frame in bbc.video.stream_frames(max_frames=settle_frames + num_frames):
            if settle_frames > 0:
                settle_frames -= 1
                continue
            frames.append(frame)
    return frames


def _print_frame_geometry(frame: Frame, label: str = "") -> None:
    """Print frame geometry for diagnostics."""
    prefix = f"{label}: " if label else ""
    print(
        f"{prefix}#{frame.frame_number:4d}  "
        f"{frame.width}x{frame.height}  "
        f"display={frame.display_width}x{frame.display_height}  "
        f"borders=L{frame.left_border} R{frame.right_border} "
        f"T{frame.top_border} B{frame.bottom_border}  "
        f"field_order={frame.field_order}  "
        f"regions={len(frame.regions)}"
    )
    for i, r in enumerate(frame.regions):
        print(f"  region[{i}]: lines {r.start_line}-{r.end_line}, width={r.pixel_width}")


def _assert_geometry_stable(frames: list[Frame]) -> None:
    """Assert all frames have identical geometry (excluding pixel data)."""
    assert len(frames) >= 2, "Need at least 2 frames"

    ref = frames[0]
    for i, frame in enumerate(frames[1:], 1):
        assert frame.width == ref.width, f"Frame {i} width {frame.width} != frame 0 width {ref.width}"
        assert frame.height == ref.height, f"Frame {i} height {frame.height} != frame 0 height {ref.height}"
        assert frame.display_width == ref.display_width, (
            f"Frame {i} display_width {frame.display_width} != frame 0 {ref.display_width}"
        )
        assert frame.display_height == ref.display_height, (
            f"Frame {i} display_height {frame.display_height} != frame 0 {ref.display_height}"
        )
        assert frame.left_border == ref.left_border, (
            f"Frame {i} left_border {frame.left_border} != frame 0 {ref.left_border}"
        )
        assert frame.top_border == ref.top_border, (
            f"Frame {i} top_border {frame.top_border} != frame 0 {ref.top_border}"
        )
        assert frame.bottom_border == ref.bottom_border, (
            f"Frame {i} bottom_border {frame.bottom_border} != frame 0 {ref.bottom_border}"
        )
        assert frame.right_border == ref.right_border, (
            f"Frame {i} right_border {frame.right_border} != frame 0 {ref.right_border}"
        )
        assert len(frame.regions) == len(ref.regions), (
            f"Frame {i} has {len(frame.regions)} regions != frame 0 has {len(ref.regions)}"
        )
        for j, (r_frame, r_ref) in enumerate(zip(frame.regions, ref.regions, strict=False)):
            assert r_frame.start_line == r_ref.start_line, (
                f"Frame {i} region {j} start_line {r_frame.start_line} != {r_ref.start_line}"
            )
            assert r_frame.end_line == r_ref.end_line, (
                f"Frame {i} region {j} end_line {r_frame.end_line} != {r_ref.end_line}"
            )
            assert r_frame.pixel_width == r_ref.pixel_width, (
                f"Frame {i} region {j} pixel_width {r_frame.pixel_width} != {r_ref.pixel_width}"
            )


@pytest.fixture
def bbc_mode7(bbc: Beebium) -> Beebium:
    """BBC Micro booted into Mode 7 (default)."""
    bbc.debugger.stop()
    bbc.run_until_or_timeout(
        lambda: screen_contains(bbc, ">"),
        emulated_seconds=10.0,
    )
    return bbc


class TestFrameGeometryStability:
    """Test that successive frames have identical renderable geometry."""

    def test_mode7_geometry_stable(self, bbc_mode7: Beebium) -> None:
        """Mode 7 (interlace sync+video, R8=3): geometry should be stable."""
        frames = _capture_frames_while_running(bbc_mode7, NUM_FRAMES)
        print(f"\n=== Mode 7 Frame Geometry ({len(frames)} frames) ===")
        for frame in frames:
            _print_frame_geometry(frame)
        _assert_geometry_stable(frames)

    @pytest.mark.parametrize("mode", [0, 1, 2, 4, 5])
    def test_bitmap_mode_geometry_stable(self, bbc_mode7: Beebium, mode: int) -> None:
        """Bitmap modes (R8=0): geometry should be stable after mode switch."""
        bbc = bbc_mode7
        # Switch to the target mode
        bbc.keyboard.type(f"MODE {mode}\r")
        bbc.run_for_emulated_seconds(1.0)

        frames = _capture_frames_while_running(bbc, NUM_FRAMES)
        print(f"\n=== Mode {mode} Frame Geometry ({len(frames)} frames) ===")
        for frame in frames:
            _print_frame_geometry(frame)
        _assert_geometry_stable(frames)
