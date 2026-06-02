# Packaging and Distribution

How the Beebium server is packaged for distribution, and how the clients are
distributed separately. The driving use case is running the headless emulator
as a **test environment for BBC Micro software in CI** (and locally): a user
installs the server, then drives it from the Python or TypeScript client.

For how an installed server discovers ROMs and presets at runtime, see
[Deployment and Resource Discovery](deployment.md). For embedding the servers in
the macOS `.app`, see [macOS App Packaging](macos-app-packaging.md).

## Distribution model

| Component | Channel | Status |
|-----------|---------|--------|
| Server (headless core) | Self-contained `.deb` (apt) + `.tar.gz` (everything else) | **Done** (Linux, both arches) |
| Server (macOS) | Homebrew tap `rob-smallshire/homebrew-beebium`, formula `beebium-server` | Planned |
| Python client | PyPI (`beebium`) | Planned |
| TypeScript client | npm (`beebium`) | Planned |

The server and the clients are deliberately distributed through **different**
channels (system package manager for the server, language ecosystems for the
clients). A user installs the server one way and `pip install beebium` /
`npm install beebium` separately; that is expected and fine.

## The self-contained static bundle (Linux)

The Linux server is shipped as a **self-contained, statically linked bundle**:
gRPC, protobuf, abseil, OpenSSL, re2, c-ares and zlib are linked **statically**
(via vcpkg), so the only dynamic dependencies are the base system libraries
(`libc`, `libstdc++`, `libgcc_s`, `libm`). This decouples the bundle from
whatever versions of those libraries a given distro ships, so one artifact per
architecture runs across distros.

### Why static, not a system-integrated `.deb`

A `.deb` that `Depends:` on the distro's `libgrpc++`/`libprotobuf` would tie the
server to each distro's library versions and couple it to the host's glibc
generation. The static bundle avoids both: it runs on any distro with a
new-enough glibc, which is exactly the heterogeneity CI inflicts. It also pins
the server's gRPC at build time, which is the right behaviour for the
cross-language compatibility story (see
[Versioning and Protocol Compatibility](versioning-and-compatibility.md)).

### The glibc floor

The bundle still links glibc dynamically (a fully static glibc breaks `dlopen`
and NSS, and Beebium `dlopen`s its plugins). A binary linked against an *older*
glibc runs on any *newer*-glibc system, so the bundle is built in a
**`debian:bookworm`** container (glibc 2.36). That floor covers Ubuntu 22.04+,
Raspberry Pi OS (Bookworm), and Arch (glibc 2.43+). Building natively on the
target distro would invert this and break portability, so we never do that.

### Architectures

Both `amd64` and `arm64` are first-class. The `arm64` CPU floor is the
Raspberry Pi 4 / 400 (Cortex-A72, baseline ARMv8-A): the build uses default
`aarch64` codegen with no `-mcpu` tuning, so one `arm64` bundle runs across
Pi 4 / 400 / 5. Raspberry Pi support requires a 64-bit OS image (Pi OS 64-bit or
Ubuntu for Pi); the 32-bit default image is not supported.

## Install layout

Both package formats lay down the same relocatable tree under `/opt/beebium`,
with `/usr/bin` symlinks onto the four server binaries so they are on `PATH`:

```
/opt/beebium/
├── beebium-model-b, beebium-model-b-plus, beebium-model-b-plus-128k, beebium-model-b-romram
├── lib/{libbeebium_extension_api.so, libbeebium_extension_ui_proto.so}
├── extensions/<name>/{<plugin>.so, manifest.json}
└── share/beebium/{roms,presets}/
/usr/bin/beebium-model-b -> /opt/beebium/beebium-model-b   (+ the other three)
```

The binaries resolve their extension ABI libraries, plugins, ROMs and presets
relative to their own on-disk location (read from the OS, not `argv[0]`), so the
PATH symlinks work and the tree relocates intact. See
[Deployment](deployment.md) for the discovery details.

## Package formats

`cmake --install` produces the tree; CPack (`cmake/BeebiumPackaging.cmake`)
produces the distributable packages:

- **`.deb`** (`beebium-server_<version>_<arch>.deb`) — for Debian / Ubuntu /
  Raspberry Pi OS. Runtime dependencies are derived with `dpkg-shlibdeps`; since
  gRPC/protobuf are static, it depends only on `libc6 (>= 2.36)`, `libgcc-s1`,
  `libstdc++6`. The maintainer scripts create and remove the `/usr/bin` symlinks.
- **`.tar.gz`** (`beebium-server-<version>-linux-<arch>.tar.gz`) — the same tree
  for non-`dpkg` distros (Arch, Fedora, openSUSE, NixOS) and bare containers.
  Extract to `/` and put `/opt/beebium` on `PATH` (or use the AUR package, when
  available).

### ROMs

The full `roms/` tree is shipped in every package. The copyright holders for the
ROMs Beebium needs are all commercially defunct, so the enforcement risk is
treated as negligible; bundling them means there is no ROM-sourcing step in the
user's quickstart. Disc images are **not** shipped — the disc under test is the
user's own BBC software.

## Building the bundle

`docker/linux-bundle/Dockerfile` builds the bundle for one architecture. On an
Apple Silicon (arm64) Mac, the `arm64` build is **native** (fast) and the
`amd64` build runs under emulation (slow):

```bash
docker buildx build --platform linux/arm64 \
    -f docker/linux-bundle/Dockerfile \
    --build-arg VCPKG_TRIPLET=arm64-linux-static \
    --target artifact --output type=local,dest=./_artifacts .
# amd64: --platform linux/amd64 --build-arg VCPKG_TRIPLET=x64-linux-static
```

Notes:

- vcpkg is pinned to the same commit the macOS CI uses, so all artifacts link
  identical gRPC/protobuf versions. Its dependency install is a separate layer
  keyed only on `vcpkg.json` + the static triplet overlays
  (`triplets/{arm64,x64}-linux-static.cmake`), with a binary-cache mount, so
  source-only changes do not re-trigger the ~30-40 min gRPC build.
- The server-main translation units instantiate the full `Machine<Hardware>`
  template against the static gRPC/protobuf headers and peak at ~2.5-3 GB each.
  A depth-1 Ninja job pool (`beebium_heavy_compile`) serialises just those, and
  the Dockerfile's `BUILD_JOBS` arg caps overall parallelism, so the build does
  not OOM on a memory-limited host (e.g. an 8 GB Docker VM).

## Validation

Three layers, increasing in fidelity:

1. **`install_smoke`** (CTest, labelled `packaging`) — installs to a staging
   prefix and boots each server, asserting the ABI libs resolve via RPATH, the
   plugins are discovered, ROMs resolve, and the gRPC server comes up.
2. **`scripts/smoke-installed-tree.sh`** — validates an *extracted* bundle on
   any distro: `ldd` self-containment (no `libgrpc`/`libprotobuf`/…), plugin
   discovery, ROM discovery + boot, and a bare-command (PATH-symlink) check.
   Runs inside every bundle build.
3. **Install-from-package smoke** — installs the actual `.deb`/`.tar.gz` in a
   *clean* target distro and runs the above, plus an interaction test driving the
   installed server through the Python client. This is what the CI workflow does.

## CI

`.github/workflows/linux-packages.yml` is **manual only** (`workflow_dispatch`,
also triggerable via `gh workflow run linux-packages.yml`) — the from-source
static gRPC build is too expensive for per-push runs. It builds both
architectures on native runners (`ubuntu-latest` for `amd64`,
`ubuntu-24.04-arm` for `arm64`, free on public repos), then exercises the
produced packages in clean `debian:bookworm` (`.deb`, with the Python
interaction smoke) and Arch (`archlinux` for x86_64, Arch Linux ARM for arm64)
containers.

A future version-bump/tag flow (see
[Versioning and Protocol Compatibility](versioning-and-compatibility.md)) can
invoke this workflow on a release tag and publish the artifacts.

## Status

**Done:**
- Self-contained static bundle for `amd64` and `arm64`, built and validated.
- `.deb` (correct per-arch `Architecture`, minimal deps, `/usr/bin` symlinks) and
  `.tar.gz`, both validated by install-in-clean-distro on Debian and Arch
  (x86_64 + Arch Linux ARM).
- The manual CI workflow that builds and smoke-tests both.

**Remaining:**
- macOS Homebrew tap (`rob-smallshire/homebrew-beebium`, formula `beebium-server`).
- An AUR `beebium-bin` PKGBUILD that repackages the `.tar.gz` for Arch.
- Publishing the Python client to PyPI and the TypeScript client to npm.
- Wiring a release-tag trigger to build and publish artifacts automatically.
- On-real-hardware validation on a 64-bit Raspberry Pi 4 / 400.
