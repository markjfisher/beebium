#!/usr/bin/env bash
#
# smoke-installed-tree.sh - validate an installed/extracted Beebium bundle is
# runnable on THIS machine. Unlike the in-tree "install_smoke" CTest, this
# operates on an already-installed <prefix> with no build tree present, so it is
# the check used to prove a release tarball runs on a foreign distro (e.g. an
# Arch container that never saw the build).
#
# It verifies the three things that distinguish a self-contained bundle:
#   1. the bundle is self-contained -- the server does NOT dynamically link
#      gRPC/protobuf/abseil/etc. (only the base system libraries);
#   2. the extension ABI shared libraries resolve via RPATH and the plugin tree
#      is discovered (`list-extensions`);
#   3. ROMs resolve through the share fallback and the gRPC server boots.
#
# Usage: smoke-installed-tree.sh <prefix>      (prefix = the .../opt/beebium dir)

set -euo pipefail

prefix="${1:?usage: smoke-installed-tree.sh <prefix>}"
server="${prefix}/bin/beebium-model-b"

fail() { echo "SMOKE FAIL: $*" >&2; exit 1; }

[ -x "${server}" ] || fail "server binary missing or not executable: ${server}"

echo "== uname: $(uname -m) / $(uname -s) =="
if command -v ldd >/dev/null 2>&1; then
    echo "== ldd ${server##*/} (self-containment check) =="
    ldd_out="$(ldd "${server}" || true)"
    echo "${ldd_out}"
    # A self-contained bundle must NOT pull these in dynamically -- they are
    # statically linked from vcpkg. If any appears, the bundle is not portable.
    if echo "${ldd_out}" | grep -Eiq 'libgrpc|libprotobuf|libabsl|libre2|libcares|libssl|libcrypto|libupb'; then
        fail "bundle dynamically links a vendored dependency (not self-contained):
$(echo "${ldd_out}" | grep -Ei 'libgrpc|libprotobuf|libabsl|libre2|libcares|libssl|libcrypto|libupb')"
    fi
fi

echo "== list-extensions (ABI lib load via RPATH + plugin discovery) =="
ext_out="$("${server}" list-extensions)" || fail "list-extensions exited non-zero"
echo "${ext_out}"
for plugin in scsi-hdd acorn-rtc piconet; do
    echo "${ext_out}" | grep -q "${plugin}" \
        || fail "plugin '${plugin}' not discovered from the installed tree"
done

echo "== boot (ROM discovery via share fallback + gRPC bring-up) =="
boot_log="$(mktemp)"
"${server}" start \
    --mos acorn-mos_1_20.rom \
    --sideways 15:rom:bbc-basic_2.rom \
    --port 0 >"${boot_log}" 2>&1 &
pid=$!
for _ in $(seq 1 40); do
    grep -q "Listening on port" "${boot_log}" && break
    kill -0 "${pid}" 2>/dev/null || break
    sleep 0.5
done
kill "${pid}" 2>/dev/null || true
wait "${pid}" 2>/dev/null || true
if ! grep -q "Listening on port" "${boot_log}"; then
    echo "---- server output ----" >&2
    cat "${boot_log}" >&2
    fail "server did not reach 'Listening on port'"
fi
grep "Listening on port" "${boot_log}"

echo "SMOKE PASS: ${prefix} is runnable on $(uname -m)/$(uname -s)"
