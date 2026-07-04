#!/bin/bash
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

# Regeneration-is-a-no-op check.
#
# Runs generate_proto.sh (with the pinned codegen toolchain from the `dev`
# extra) and asserts it produced no change to the committed _proto tree. This
# closes the drift hole where committed stubs and the generator diverge: if
# this check fails, either the .proto files changed and the stubs weren't
# regenerated, or the generator/toolchain no longer reproduces what's committed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROTO_OUT_DIR="src/beebium/_proto"

cd "$CLIENT_DIR"

echo "Regenerating proto stubs with the locked toolchain (uv default dev group)..."
uv run bash scripts/generate_proto.sh

echo "Checking that regeneration was a no-op..."
if ! git diff --quiet -- "$PROTO_OUT_DIR"; then
    echo
    echo "ERROR: regenerating the proto stubs changed the committed tree." >&2
    echo "The committed stubs are out of sync with the .proto sources or the" >&2
    echo "codegen toolchain. Re-run scripts/generate_proto.sh and commit the" >&2
    echo "result. Diff:" >&2
    echo
    git --no-pager diff --stat -- "$PROTO_OUT_DIR" >&2
    git --no-pager diff -- "$PROTO_OUT_DIR" >&2
    exit 1
fi

# Also fail if regeneration introduced brand-new (untracked) stub files that
# aren't committed -- a proto was added to the generator but never checked in.
untracked="$(git ls-files --others --exclude-standard -- "$PROTO_OUT_DIR")"
if [[ -n "$untracked" ]]; then
    echo
    echo "ERROR: regeneration produced untracked stub files not committed:" >&2
    echo "$untracked" >&2
    exit 1
fi

echo "OK: proto stubs are in sync with sources and toolchain."
