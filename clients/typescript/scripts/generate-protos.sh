#!/bin/bash
# Generate TypeScript stubs from proto files using ts-proto.
#
# Requires: protoc, ts-proto (installed via npm)
#
# Usage:
#   npm run generate-protos

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PROTO_DIR="$PROJECT_DIR/../../src/service/proto"
OUT_DIR="$PROJECT_DIR/src/generated"

# ts-proto plugin path
PLUGIN="$PROJECT_DIR/node_modules/.bin/protoc-gen-ts_proto"

if [ ! -x "$PLUGIN" ]; then
    echo "Error: ts-proto plugin not found at $PLUGIN"
    echo "Run 'npm install' first."
    exit 1
fi

# Clean and recreate output directory
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# Proto files to generate (matching Python's 8 services + audio)
PROTOS=(
    "debugger.proto"
    "system.proto"
    "video.proto"
    "audio.proto"
    "keyboard.proto"
    "disc.proto"
    "econet.proto"
    "tube.proto"
)

echo "Generating TypeScript stubs from proto files..."

for proto in "${PROTOS[@]}"; do
    echo "  $proto"
    protoc \
        --plugin="protoc-gen-ts_proto=$PLUGIN" \
        --ts_proto_out="$OUT_DIR" \
        --ts_proto_opt=outputServices=grpc-js \
        --ts_proto_opt=esModuleInterop=true \
        --ts_proto_opt=env=node \
        --ts_proto_opt=forceLong=number \
        --ts_proto_opt=useExactTypes=false \
        -I "$PROTO_DIR" \
        "$PROTO_DIR/$proto"
done

echo "Done. Generated files in $OUT_DIR"
