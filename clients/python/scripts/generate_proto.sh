#!/bin/bash
# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

# Generate Python protobuf and gRPC code from .proto files

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROTO_DIR="${SCRIPT_DIR}/../../../src/service/proto"
EXTENSIONS_DIR="${SCRIPT_DIR}/../../../src/extensions"
OUT_DIR="${SCRIPT_DIR}/../src/beebium/_proto"

# Ensure output directory exists
mkdir -p "$OUT_DIR"

echo "Generating protobuf code from $PROTO_DIR (and extension protos) to $OUT_DIR..."

# Service-layer protos under src/service/proto and extension protos
# under src/extensions/<name>/. Both are passed in one protoc
# invocation so cross-file imports (none today, but possible) resolve.
python -m grpc_tools.protoc \
    -I "$PROTO_DIR" \
    -I "$EXTENSIONS_DIR/aun" \
    -I "$EXTENSIONS_DIR/piconet" \
    --python_out="$OUT_DIR" \
    --grpc_python_out="$OUT_DIR" \
    "$PROTO_DIR/video.proto" \
    "$PROTO_DIR/keyboard.proto" \
    "$PROTO_DIR/debugger.proto" \
    "$PROTO_DIR/audio.proto" \
    "$PROTO_DIR/system.proto" \
    "$PROTO_DIR/disc.proto" \
    "$PROTO_DIR/econet.proto" \
    "$PROTO_DIR/econet_transport.proto" \
    "$EXTENSIONS_DIR/aun/aun.proto" \
    "$EXTENSIONS_DIR/piconet/piconet_service.proto"

# Fix imports in generated files to use relative imports
# The generated code uses absolute imports which don't work with src layout
for file in "$OUT_DIR"/*_pb2_grpc.py; do
    if [[ -f "$file" ]]; then
        # Replace 'import xxx_pb2' with 'from . import xxx_pb2'
        sed -i '' 's/^import \(.*_pb2\) as/from . import \1 as/' "$file"
    fi
done

# Inject the project copyright/licence header into each generated file.
# The pre-commit hook insists on it; protoc doesn't add one. Skip files
# that already have it (idempotent re-runs).
HEADER='# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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
'
for file in "$OUT_DIR"/*_pb2.py "$OUT_DIR"/*_pb2_grpc.py; do
    if [[ -f "$file" ]] && ! grep -q "Robert Smallshire" "$file"; then
        printf '%s\n%s' "$HEADER" "$(cat "$file")" > "$file"
    fi
done

echo "Proto files generated successfully in $OUT_DIR"
