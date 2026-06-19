#!/usr/bin/env bash
# Build (and optionally run) the macOS frontend with a freshly built server
# embedded in the app bundle.
#
# The macOS app spans two build systems: CMake builds the headless server
# executables, and the Xcode build embeds them into the app bundle. The Xcode
# embed phase reads the server location from BEEBIUM_SERVERS_BUILD_DIR. This
# script builds the servers and passes that variable, so the bundled server can
# never silently go stale (forgetting to *export* it is an easy, invisible
# mistake -- the embed phase just skips and keeps an old server).
#
# Usage:
#   scripts/build-macos-app.sh           # build servers + app (Debug)
#   scripts/build-macos-app.sh --run     # ... and launch the app
#   BUILD_DIR=build-release CONFIG=Release scripts/build-macos-app.sh
#
# BUILD_DIR must already be configured with CMake (see README.md).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
CONFIG="${CONFIG:-Debug}"
MACOS_PROJECT_DIR="$REPO_ROOT/clients/macos/Beebium"
SERVERS_DIR="$BUILD_DIR/src/server"

RUN=0
[ "${1:-}" = "--run" ] && RUN=1

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  echo "error: $BUILD_DIR is not configured for CMake." >&2
  echo "       Configure it first (see README.md), or set BUILD_DIR=<dir>." >&2
  exit 1
fi

echo "=== Building servers in $BUILD_DIR ==="
cmake --build "$BUILD_DIR" --target beebium-servers --parallel

echo "=== Building macOS app ($CONFIG), embedding servers from $SERVERS_DIR ==="
cd "$MACOS_PROJECT_DIR"
BEEBIUM_SERVERS_BUILD_DIR="$SERVERS_DIR" \
  xcodebuild build -scheme Beebium -configuration "$CONFIG"

if [ "$RUN" = "1" ]; then
  APP="$(BEEBIUM_SERVERS_BUILD_DIR="$SERVERS_DIR" \
    xcodebuild -scheme Beebium -configuration "$CONFIG" -showBuildSettings 2>/dev/null \
    | awk '/ BUILT_PRODUCTS_DIR =/ {print $3}')/Beebium.app"
  echo "=== Launching $APP ==="
  open "$APP"
fi
