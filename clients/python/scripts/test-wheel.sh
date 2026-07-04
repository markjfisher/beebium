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

# Build the wheel and run the packaging tests against it in a fresh virtualenv.
#
# This is the load-bearing packaging gate (see
# docs/discussion/python-client-architecture.md section 4.3): the suite must run
# against code installed from the built wheel, never against src/, so that
# missing package data, an unshipped py.typed marker, namespace misconfiguration
# and unregistered entry points are caught rather than silently passed over by a
# source-tree run.
#
# The emulator server is NOT required: only the server-free packaging assertions
# in tests_packaging/ run here. The full integration suite runs separately
# against a built server.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$CLIENT_DIR"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

echo "==> Building wheel"
rm -rf dist
uv build --wheel
WHEEL="$(ls "$CLIENT_DIR"/dist/*.whl)"
echo "    built $(basename "$WHEEL")"

echo "==> Creating a fresh virtualenv at $WORK_DIR/venv"
uv venv "$WORK_DIR/venv"
VENV_PYTHON="$WORK_DIR/venv/bin/python"
[[ -x "$VENV_PYTHON" ]] || VENV_PYTHON="$WORK_DIR/venv/Scripts/python.exe"  # Windows

echo "==> Installing the wheel (with the dev extra) into the fresh venv"
uv pip install --python "$VENV_PYTHON" "${WHEEL}[dev]"

echo "==> Running packaging tests against the INSTALLED wheel (cwd outside src/)"
# Run from WORK_DIR, passing the absolute test path, so `import beebium`
# resolves from site-packages -- never from ./src.
cd "$WORK_DIR"
"$VENV_PYTHON" -m pytest "$CLIENT_DIR/tests_packaging" -v

echo "==> Wheel packaging tests passed."
