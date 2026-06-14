#!/usr/bin/env python3
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

"""Make the embedded Beebium servers self-contained inside the .app bundle.

The server executables (in Contents/Frameworks/) and the transport/
peripheral plugins (dlopen'd from Contents/Frameworks/extensions/<name>/)
are built against the development tree: they reference internal shared
libraries via an @rpath that points at the absolute build path
(<repo>/build/lib) and link Homebrew's gRPC/protobuf/abseil from
/opt/homebrew. Neither path exists on an end user's machine, so the
packaged app only runs where it was built.

This script walks the dependency graph of every server executable and
every plugin, copies each non-system dependency into Contents/Frameworks/,
rewrites every reference to @rpath/<name>, gives each binary an rpath that
resolves @rpath to Contents/Frameworks/, strips the absolute build/Homebrew
rpaths, and re-signs each modified Mach-O ad-hoc (mandatory on arm64 --
dyld refuses to load a modified binary whose signature no longer matches).

The result is a bundle that resolves all non-system libraries from within
itself. Distribution still requires Developer ID signing + notarization;
see docs/macos-app-packaging.md.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Dependencies under these prefixes ship with every macOS install and must
# never be bundled (and never rewritten).
SYSTEM_PREFIXES = ("/usr/lib/", "/System/")


def run(cmd: list[str]) -> str:
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def is_macho(path: Path) -> bool:
    """True if path is a Mach-O file (not a script, text, or symlink dir)."""
    if not path.is_file():
        return False
    try:
        with path.open("rb") as f:
            magic = f.read(4)
    except OSError:
        return False
    # 0xfeedfacf (64-bit LE/BE), 0xfeedface (32-bit), 0xcafebabe (fat).
    return magic in (
        b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
        b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xce",
        b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
    )


def is_system(path: str) -> bool:
    return path.startswith(SYSTEM_PREFIXES)


def otool_id(path: Path) -> str | None:
    """The install id (LC_ID_DYLIB) of a dylib, or None for an executable."""
    out = run(["otool", "-D", str(path)])
    lines = [ln for ln in out.splitlines() if ln and not ln.endswith(":")]
    return lines[0].strip() if lines else None


def otool_deps(path: Path) -> list[str]:
    """Linked dependency install names (excluding the file's own id)."""
    out = run(["otool", "-L", str(path)])
    own_id = otool_id(path)
    deps = []
    for line in out.splitlines()[1:]:  # first line echoes the filename
        line = line.strip()
        if not line:
            continue
        dep = line.split(" (", 1)[0].strip()
        if dep and dep != own_id:
            deps.append(dep)
    return deps


def otool_rpaths(path: Path) -> list[str]:
    out = run(["otool", "-l", str(path)])
    rpaths = []
    lines = out.splitlines()
    for i, line in enumerate(lines):
        if "cmd LC_RPATH" in line:
            # The 'path <value> (offset N)' line follows within the command.
            for follow in lines[i + 1:i + 4]:
                stripped = follow.strip()
                if stripped.startswith("path "):
                    rpaths.append(stripped[len("path "):].split(" (offset", 1)[0])
                    break
    return rpaths


def resolve(dep: str, referrer: Path, rpaths: list[str],
            search_paths: list[str]) -> Path | None:
    """Resolve a dependency install name to a real file on disk.

    For @rpath dependencies dyld searches the rpaths of every binary in the
    load chain, not just the immediate referrer -- Homebrew's gRPC upb libs,
    for instance, reference siblings via @rpath but carry no rpath of their
    own, relying on the top-level executable's /opt/homebrew/lib. We mirror
    that by falling back to search_paths (the absolute rpaths gathered from
    every root) when the referrer's own rpaths don't resolve the name.
    """
    loader = str(referrer.parent)

    def expand(p: str) -> str:
        return (p.replace("@loader_path", loader)
                 .replace("@executable_path", loader))

    if dep.startswith("@rpath/"):
        suffix = dep[len("@rpath/"):]
        for rp in list(rpaths) + search_paths:
            candidate = Path(expand(rp)) / suffix
            if candidate.exists():
                return candidate.resolve()
        return None
    if dep.startswith(("@loader_path", "@executable_path")):
        candidate = Path(expand(dep))
        return candidate.resolve() if candidate.exists() else None
    candidate = Path(dep)
    return candidate.resolve() if candidate.exists() else None


def sign(path: Path) -> None:
    # Ad-hoc signature (-s -). Required on arm64 after any Mach-O edit.
    run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(path)])


class Bundler:
    def __init__(self, frameworks: Path):
        self.frameworks = frameworks.resolve()
        self.bundled: dict[str, Path] = {}   # referenced basename -> dest
        self.processed: set[Path] = set()
        self.touched: list[Path] = []        # files needing re-signing
        # Absolute rpath dirs gathered from every binary, used as a global
        # fallback when resolving @rpath deps (see resolve()).
        self.search_paths: list[str] = []

    def seed_search_paths(self, roots: list[Path]) -> None:
        for root in roots:
            self._collect_search_paths(root)

    def _collect_search_paths(self, macho: Path) -> None:
        for rp in otool_rpaths(macho):
            if rp.startswith("/") and rp not in self.search_paths and Path(rp).is_dir():
                self.search_paths.append(rp)

    def _is_inside_bundle(self, real: Path) -> bool:
        try:
            real.relative_to(self.frameworks)
            return True
        except ValueError:
            return False

    def _bundle_rpath_for(self, macho: Path) -> str:
        # Make @rpath resolve to Contents/Frameworks/ regardless of how deep
        # the Mach-O sits (servers + libs live directly there; plugins live
        # under Frameworks/extensions/<name>/).
        rel = macho.parent.resolve().relative_to(self.frameworks)
        ups = len(rel.parts)
        return "@loader_path" if ups == 0 else "@loader_path/" + "/".join([".."] * ups)

    def process(self, macho: Path) -> None:
        macho = macho.resolve()
        if macho in self.processed:
            return
        self.processed.add(macho)

        os.chmod(macho, 0o755)
        rpaths = otool_rpaths(macho)
        self._collect_search_paths(macho)

        for dep in otool_deps(macho):
            if is_system(dep):
                continue
            real = resolve(dep, macho, rpaths, self.search_paths)
            if real is None:
                print(f"  WARNING: unresolved dependency {dep} of {macho.name}",
                      file=sys.stderr)
                continue
            if is_system(str(real)):
                continue
            if self._is_inside_bundle(real):
                # Already a bundled lib; make sure we descend into it.
                self.process(real)
                continue

            base = Path(dep).name
            dest = self.frameworks / base
            if base not in self.bundled:
                shutil.copy(real, dest)
                os.chmod(dest, 0o755)
                run(["install_name_tool", "-id", f"@rpath/{base}", str(dest)])
                self.bundled[base] = dest
                self._mark(dest)
                self.process(dest)

            new_ref = f"@rpath/{base}"
            if dep != new_ref:
                run(["install_name_tool", "-change", dep, new_ref, str(macho)])
                self._mark(macho)

        self._fix_rpaths(macho, rpaths)

    def _fix_rpaths(self, macho: Path, existing: list[str]) -> None:
        wanted = self._bundle_rpath_for(macho)
        if wanted not in existing:
            run(["install_name_tool", "-add_rpath", wanted, str(macho)])
            self._mark(macho)
        # Drop absolute rpaths (the dev build/lib and Homebrew paths) so the
        # bundle is the only resolution source -- this is what makes the app
        # portable, and doubles as a self-containment assertion.
        for rp in existing:
            if rp.startswith("/"):
                run(["install_name_tool", "-delete_rpath", rp, str(macho)])
                self._mark(macho)

    def _mark(self, path: Path) -> None:
        if path not in self.touched:
            self.touched.append(path)

    def finalize(self) -> None:
        # Sign leaf-first so containers see valid nested signatures.
        for path in reversed(self.touched):
            sign(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frameworks",
                        help="Path to the app's Contents/Frameworks directory")
    parser.add_argument(
        "--search-dir", action="append", default=[], dest="search_dirs",
        metavar="DIR",
        help="Additional directory to search for @rpath dependencies "
             "(repeatable). Needed when the binaries' baked rpaths point at a "
             "build tree that does not exist on this machine -- e.g. a server "
             "built in CI references <ci-workspace>/build/lib, so point this at "
             "the shipped lib/ directory holding the actual dylibs.")
    args = parser.parse_args()

    frameworks = Path(args.frameworks)
    if not frameworks.is_dir():
        print(f"error: not a directory: {frameworks}", file=sys.stderr)
        return 1

    # Roots: server executables sitting directly in Frameworks/, plus every
    # plugin dylib under Frameworks/extensions/.
    roots: list[Path] = [p for p in frameworks.iterdir() if is_macho(p)]
    ext_dir = frameworks / "extensions"
    if ext_dir.is_dir():
        roots += [p for p in ext_dir.rglob("*.dylib") if is_macho(p)]

    if not roots:
        print("No Mach-O roots found in Frameworks; nothing to bundle.")
        return 0

    print(f"Bundling dependencies for {len(roots)} Mach-O root(s)...")
    bundler = Bundler(frameworks)
    # Explicit search dirs (e.g. the shipped lib/) take precedence over the
    # rpaths baked into the binaries, which may reference a non-existent build
    # tree when the servers were built on a different machine (CI).
    for search_dir in args.search_dirs:
        resolved = Path(search_dir).resolve()
        if resolved.is_dir():
            bundler.search_paths.append(str(resolved))
        else:
            print(f"  WARNING: --search-dir does not exist: {search_dir}",
                  file=sys.stderr)
    bundler.seed_search_paths(roots)
    for root in roots:
        bundler.process(root)
    bundler.finalize()
    print(f"  Bundled {len(bundler.bundled)} librarie(s); "
          f"re-signed {len(bundler.touched)} file(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
