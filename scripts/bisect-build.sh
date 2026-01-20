#!/bin/bash
#
# bisect-build.sh - Build script for git bisect of cursor blink rate bug
#
# This script builds both the C++ core library and the macOS client from scratch,
# using a fresh DerivedData directory to avoid Xcode caching issues when switching
# between commits with different file structures.
#
# Usage:
#   ./scripts/bisect-build.sh
#
# For git bisect:
#   git bisect start
#   git bisect bad HEAD
#   git bisect good <known-good-commit>
#   git bisect run ./scripts/bisect-build.sh
#
# Note: For manual bisecting where you need to visually check cursor blink rate,
# use this script to build, then run the server and client manually.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DERIVED_DATA_PATH="/tmp/beebium-bisect"

echo "========================================"
echo "Bisect build at commit: $(git rev-parse --short HEAD)"
echo "$(git log -1 --pretty=format:'%s')"
echo "========================================"

# Clean and rebuild C++ core
echo ""
echo "Building C++ core..."
cd "$PROJECT_ROOT"
rm -rf build
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j8

# Clean and rebuild macOS client
echo ""
echo "Building macOS client..."
rm -rf "$DERIVED_DATA_PATH"
cd "$PROJECT_ROOT/clients/macos/Beebium"
xcodebuild -scheme Beebium -configuration Debug -derivedDataPath "$DERIVED_DATA_PATH" clean build

echo ""
echo "========================================"
echo "Build successful at $(git rev-parse --short HEAD)"
echo "========================================"
echo ""
echo "To test:"
echo "  1. Run server:  ./build/beebium-model-b"
echo "  2. Run client:  open $DERIVED_DATA_PATH/Build/Products/Debug/Beebium.app"
echo ""
