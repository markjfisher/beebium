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

"""Compute the Beebium protocol fingerprint.

A fingerprint over the *semantic* wire contract, not the proto source text. It
is invariant to comments, formatting, declaration order, file names, and import
paths, and changes only when the contract changes: message field numbers/types/
labels/names, enum values, and service method signatures (name, input/output
type, streaming).

Mechanism: compile the protos to a FileDescriptorSet WITHOUT source info (which
drops all comments and source locations), normalise it to a canonical model
keyed by fully-qualified type name, and SHA-256 that. The same fingerprint is
then injected into the server and every client so the connect-time handshake
compares a single value built once -- never each language hashing its own
(differing) stub subset.

Usage:
    python protocol_fingerprint.py --include DIR [--include DIR ...] PROTO [PROTO ...]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from google.protobuf import descriptor_pb2


# Well-known types are fixed by the protobuf release, not part of Beebium's
# contract; excluding them keeps the fingerprint stable across protobuf versions.
_WELL_KNOWN_PREFIX = "google/protobuf/"


def _descriptor_set(proto_files: list[str], includes: list[str]) -> bytes:
    """Compile protos to a FileDescriptorSet without source info (no comments)."""
    with tempfile.NamedTemporaryFile(suffix=".pb", delete=False) as tmp:
        out = tmp.name
    cmd = [
        sys.executable, "-m", "grpc_tools.protoc",
        f"--descriptor_set_out={out}",
        "--include_imports",
        # NOTE: --include_source_info is deliberately omitted, so comments and
        # source locations are not present in the descriptor set.
    ]
    for inc in includes:
        cmd.append(f"-I{inc}")
    cmd.extend(proto_files)
    subprocess.run(cmd, check=True)
    data = Path(out).read_bytes()
    Path(out).unlink()
    return data


def _canonical_message(msg: descriptor_pb2.DescriptorProto, scope: str) -> dict:
    fqname = f"{scope}.{msg.name}"
    fields = sorted(
        (
            {
                "number": f.number,
                "name": f.name,
                "label": int(f.label),
                "type": int(f.type),
                "type_name": f.type_name,  # fully-qualified for message/enum fields
                "oneof_index": f.oneof_index if f.HasField("oneof_index") else -1,
            }
            for f in msg.field
        ),
        key=lambda d: d["number"],
    )
    oneofs = [o.name for o in msg.oneof_decl]
    nested = {}
    for n in msg.nested_type:
        nested.update(_canonical_message(n, fqname))
    enums = {f"{fqname}.{e.name}": _canonical_enum(e) for e in msg.enum_type}
    result = {fqname: {"fields": fields, "oneofs": oneofs, "enums": enums}}
    result.update(nested)
    return result


def _canonical_enum(enum: descriptor_pb2.EnumDescriptorProto) -> dict:
    return {
        "values": sorted(
            ({"name": v.name, "number": v.number} for v in enum.value),
            key=lambda d: (d["number"], d["name"]),
        )
    }


def _canonical_service(svc: descriptor_pb2.ServiceDescriptorProto, pkg: str) -> dict:
    fqname = f"{pkg}.{svc.name}" if pkg else svc.name
    methods = sorted(
        (
            {
                "name": m.name,
                "input_type": m.input_type,
                "output_type": m.output_type,
                "client_streaming": m.client_streaming,
                "server_streaming": m.server_streaming,
            }
            for m in svc.method
        ),
        key=lambda d: d["name"],
    )
    return {fqname: methods}


def _canonical_model(fds: descriptor_pb2.FileDescriptorSet) -> dict:
    messages: dict = {}
    enums: dict = {}
    services: dict = {}
    for f in fds.file:
        if f.name.startswith(_WELL_KNOWN_PREFIX):
            continue
        scope = f.package
        for m in f.message_type:
            for fq, body in _canonical_message(m, scope).items():
                # Hoist nested enums into the top-level enum map for stability.
                enums.update(body.pop("enums", {}))
                messages[fq] = body
        for e in f.enum_type:
            enums[f"{scope}.{e.name}"] = _canonical_enum(e)
        for s in f.service:
            services.update(_canonical_service(s, scope))
    return {"messages": messages, "enums": enums, "services": services}


def compute_fingerprint(proto_files: list[str], includes: list[str]) -> str:
    fds = descriptor_pb2.FileDescriptorSet.FromString(
        _descriptor_set(proto_files, includes)
    )
    model = _canonical_model(fds)
    canonical = json.dumps(model, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Compute the Beebium protocol fingerprint.")
    parser.add_argument("--include", "-I", action="append", default=[], dest="includes")
    parser.add_argument("protos", nargs="+")
    args = parser.parse_args()
    print(compute_fingerprint(args.protos, args.includes))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
