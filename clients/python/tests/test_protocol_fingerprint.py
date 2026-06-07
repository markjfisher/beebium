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

"""Protocol fingerprint handshake tests."""

from __future__ import annotations

from pathlib import Path

import pytest

import beebium.client
from beebium import (
    Beebium,
    PROTOCOL_FINGERPRINT,
    ProtocolMismatchError,
    ServerNotFoundError,
)


def test_server_reports_matching_fingerprint(bbc: Beebium) -> None:
    """The server reports the same protocol fingerprint the client carries."""
    assert bbc.system.protocol_fingerprint == PROTOCOL_FINGERPRINT


def test_mismatched_client_is_rejected_at_connect(
    monkeypatch: pytest.MonkeyPatch,
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> None:
    """A client whose fingerprint differs from the server's is refused."""
    monkeypatch.setattr(beebium.client, "PROTOCOL_FINGERPRINT", "mismatched-fingerprint")
    # This needs a real server (like the other integration tests); skip when one
    # is not available, e.g. in the unit-test job that builds no server.
    try:
        with pytest.raises(ProtocolMismatchError):
            with Beebium.launch(
                mos_filepath=mos_filepath,
                basic_filepath=basic_filepath,
                server_filepath=beebium_server_filepath,
            ):
                pass
    except ServerNotFoundError as e:
        pytest.skip(str(e))
