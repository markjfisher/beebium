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
| Server (macOS) | Homebrew tap `rob-smallshire/homebrew-beebium`, formula `beebium-server` | **Done** (arm64 CI-gated; Intel best-effort); tap publish is manual |
| Server (Windows) | Self-contained `.zip` + Scoop bucket + WinGet | Planned |
| Python client | PyPI (`beebium`) | Planned |
| TypeScript client | npm (`beebium`) | Planned |

The server and the clients are deliberately distributed through **different**
channels (system package manager for the server, language ecosystems for the
clients). A user installs the server one way and `pip install beebium` /
`npm install beebium` separately; that is expected and fine.

## Repositories and where things live

Packaging now spans **two repositories**. Knowing which is which avoids editing
the wrong copy of the formula.

### The monorepo — `rob-smallshire/beebium`

This repo (where you are reading) holds the source and **all the packaging
inputs**. Nothing here is the published Homebrew formula; this is where packaging
is authored and tested.

```
beebium/
├── CMakeLists.txt              # install rules + CPack feed the Linux packages
├── cmake/BeebiumPackaging.cmake# CPack config (.deb + .tar.gz)
├── docker/linux-bundle/        # static-bundle build (the Linux .deb/.tar.gz)
├── scripts/smoke-installed-tree.sh
├── packaging/
│   ├── debian/{postinst,prerm} # .deb maintainer scripts
│   ├── smoke/test_smoke.py     # client-drives-installed-server interaction smoke
│   └── homebrew/
│       ├── beebium-server.rb   # CANONICAL formula (source of truth, reviewed/CI'd)
│       ├── test-formula.sh     # build/install/test/audit against the working tree
│       └── sync-tap.sh         # pin a release into the tap (below)
└── .github/workflows/
    ├── linux-packages.yml      # build + smoke the Linux packages
    ├── macos-package.yml       # build/test/audit the Homebrew formula
    └── release.yml             # tag -> Linux bundles + macOS formula + draft Release
```

### The tap — `rob-smallshire/homebrew-beebium`

A **separate GitHub repo** that Homebrew clones when a user runs
`brew tap rob-smallshire/beebium`. It holds the **published** formula and the tap
README. Locally it lives under the Homebrew prefix, not beside this repo:

```
/opt/homebrew/Library/Taps/rob-smallshire/homebrew-beebium/
├── Formula/beebium-server.rb   # the PUBLISHED formula users install
├── README.md                   # tap landing page (install instructions, status)
└── .github/workflows/          # bottle-building CI (brew test-bot / pr-pull)
```

### How the two formulae stay in sync

`packaging/homebrew/beebium-server.rb` (monorepo) is the source of truth;
`Formula/beebium-server.rb` (tap) is a published copy pinned to a release. They
are connected by `packaging/homebrew/sync-tap.sh <version> <tap-checkout>`, which
fetches the release source tarball, computes its `sha256`, and writes the pinned
formula into the tap's `Formula/`. **Edit the canonical copy in the monorepo**,
validate it (`packaging/homebrew/test-formula.sh`), then run `sync-tap.sh` to push
the change to the tap — never hand-edit the tap's formula. (The `head` spec in the
canonical formula means `brew install --HEAD beebium-server` builds from the
monorepo's `master` directly, independent of any pinned release.)

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

## The macOS Homebrew formula

The macOS server is distributed through a Homebrew tap
(`rob-smallshire/homebrew-beebium`, formula `beebium-server`). Unlike the Linux
bundle, it is a **source build against Homebrew's own grpc/protobuf/abseil** —
the idiomatic Homebrew approach — rather than a static bundle.

### Tap layout and naming

A tap is a single GitHub repo (`homebrew-<name>`; the `homebrew-` prefix is
stripped in `brew` commands) that can hold any number of formulae and casks. One
repo — `rob-smallshire/homebrew-beebium`, tapped as `rob-smallshire/beebium` —
holds **everything Beebium ships through Homebrew**; separate repos are not
needed:

```
homebrew-beebium/
├── Formula/
│   └── beebium-server.rb     # headless backend (CLI, build-from-source)
└── Casks/
    └── beebium-gui.rb        # macOS GUI app (.app bundle, future, out of scope now)
```

The packages are installed independently:

```
brew install rob-smallshire/beebium/beebium-server   # backend only
brew install --cask rob-smallshire/beebium/beebium-gui   # frontend only (future)
```

**Naming decision:** the backend is `beebium-server` and the macOS GUI will be
`beebium-gui`; the bare `beebium` token is left unclaimed. The names are
deliberately symmetric and descriptive so neither component squats the project's
bare name — consistent with Beebium's headless-core identity, where the GUI is
one of several frontends rather than "the" application. (A cask token
conventionally mirrors an app's display name, which would pull the GUI toward a
bare `beebium`; that is rejected here precisely because casks are macOS-only and
the bare name should not resolve to one platform's frontend.) The only hard
Homebrew constraint is that a formula and a cask in the same tap must not share a
token, or `brew install <token>` becomes an ambiguous formula-vs-cask choice;
distinct `-server`/`-gui` tokens avoid that entirely. The macOS app currently
embeds its own server binaries; if that changes, the cask can
`depends_on formula: "rob-smallshire/beebium/beebium-server"` while staying
separately installable.

### Why source-build, not static

Static-linking gRPC was historically forced on macOS by the "duplicate gRPC
runtime" crash: when both the server and a dlopened plugin embedded gRPC/abseil,
serving a plugin-hosted service segfaulted in `ExecCtx::Run`. Since the
**ExtensionRpc channel** landed, plugins no longer host gRPC services — they link
only `libbeebium_extension_api` and the shared `libprotobuf`, so only the core
links gRPC. The duplicate-runtime hazard is gone, and the formula can simply
`depends_on "grpc"` and friends and build from source.

The build needs no special toolchain: `CMakeLists.txt` already prefers CONFIG-mode
`find_package(Protobuf)` / `find_package(gRPC)` (Homebrew's), and
`nlohmann_json` is resolved the same way (`find_package(nlohmann_json CONFIG)`),
falling back to the pinned `FetchContent` copy only when no package is installed
— Homebrew's build sandbox forbids `FetchContent` network access, so the packaged
`nlohmann-json` is used there.

### Accepted gRPC-skew trade-off

A Homebrew-installed server links Homebrew's gRPC, which floats with the tap's
`grpc` formula, while a client may be installed independently (uv/pip, npm) or on
a different host/OS entirely. That skew is acceptable: gRPC keeps the wire
protocol cross-version compatible, and the connect-time **protocol-fingerprint
handshake** (see [Versioning and Protocol
Compatibility](versioning-and-compatibility.md)) rejects any *schema* mismatch.
Server and client version numbers need not match across the wire.

### Keg layout

The formula installs the whole relocatable tree under `libexec` and symlinks the
four servers into `bin`, so only the servers land on the user's `PATH` (not
`bin/extensions/`):

```
<keg>/
├── bin/{beebium-model-b, ...}            # symlinks -> ../libexec/bin/<server>
└── libexec/
    ├── bin/{beebium-model-b, ...}        # the real binaries
    ├── bin/extensions/<name>/{<plugin>.dylib, manifest.json}
    ├── lib/{libbeebium_extension_api.dylib, libbeebium_extension_ui_proto.dylib}
    └── share/beebium/{roms,presets}/
```

Discovery follows the `bin` symlink to the real binary's on-disk location (via
`_NSGetExecutablePath`), then resolves extensions, the ABI dylibs (via the
`@loader_path/../lib` install RPATH), ROMs and presets relative to it — the same
mechanism the Linux `/usr/bin` symlinks rely on.

### Files and validation

- `packaging/homebrew/beebium-server.rb` — the canonical formula, kept in the
  monorepo for review and CI.
- `packaging/homebrew/test-formula.sh` — packages the working tree into a
  GitHub-style source tarball, pins a throwaway formula at it, then
  `brew install --build-from-source` + `brew test` + `brew audit --strict` +
  an extension-discovery check. Run locally or in CI; both run identical steps.
- `packaging/homebrew/sync-tap.sh <version> <tap-checkout>` — fetches the
  released source tarball, computes its `sha256`, and writes the pinned formula
  into the tap's `Formula/` for the maintainer to commit and push.

Publishing the tap is deliberately manual: author + validate here, then push the
formula to `rob-smallshire/homebrew-beebium` to go live.

### Current state and remaining macOS work

The tap (`rob-smallshire/homebrew-beebium`) exists and carries the formula, and a
full end-to-end install has been validated from the real tap:
`brew install --HEAD rob-smallshire/beebium/beebium-server` clones `master`,
builds, puts the four servers on `PATH`, and discovers all extensions. So the
**`--HEAD` (build-from-`master`) path works today.**

Remaining before a normal `brew install beebium-server` works, in order:

1. **Cut a tagged release.** The formula's `url`/`sha256` point at a
   `v<version>` source tarball that does not exist yet; `sha256` is a placeholder.
   Tag a release (`git tag v0.1.0 && git push --follow-tags`, or `bump-my-version`),
   then run `packaging/homebrew/sync-tap.sh <version> <tap-checkout>` to fetch the
   tarball, compute the real checksum, and write the pinned formula into the tap;
   commit and push the tap. Only then does the stable (non-`--HEAD`) formula
   resolve.
2. **Wire bottles.** Until bottles exist, every `brew install` compiles
   beebium-server from source (~1 min locally, a few minutes on slower runners) —
   acceptable for dev, undesirable at CI scale. The `brew tap-new` scaffolding
   already added a bottle-building workflow to the tap; once `v<version>` is
   tagged, enable it (PR -> `brew test-bot` builds bottles per macOS
   runner/arch -> `pr-pull` uploads them and adds the `bottle do` block to the
   formula). After that, installs pour a pre-built binary and only unsupported
   macOS versions (or `--build-from-source`) compile.

A self-contained macOS `.tar.gz` (Homebrew-free, for macOS CI that wants the same
download-and-run experience as Linux) is a possible later addition; it needs the
vcpkg-static build path rather than the Homebrew-deps build.

## Windows

The Windows server build and tests have always been green in CI on
`windows-2022`. The packaging described below has now been **validated
end-to-end as a proof of concept** (built on a Windows box, not yet in CI): a
self-contained `x64-windows-static-md` ZIP installs and runs through Scoop with
all extensions loading. What remains is automation (a CI job and the Scoop
bucket), not feasibility. The approach mirrors the Linux/macOS strategy — a
self-contained artifact for CI plus a package-manager front end.

The normal CI build still links the vcpkg `x64-windows` libraries *dynamically*
(copying the gRPC/protobuf/abseil DLLs next to the executables); the packaging
build instead uses the static triplet below.

### Server: a self-contained `.zip`

The primary artifact is a **self-contained ZIP**, the direct analogue of the Linux
`.tar.gz`: extract, put on `PATH`, run — no installer, no admin, ideal for CI. It
is produced by the same CPack machinery (the ZIP generator).

Self-containment uses a **static vcpkg triplet**, just like Linux/macOS, instead
of shipping a pile of DLLs:

- `x64-windows-static-md` — folds gRPC/protobuf/abseil into the executables and
  plugin DLLs but keeps the **dynamic CRT** (users need the near-universal Visual
  C++ Redistributable). The usual sweet spot.
- `x64-windows-static` — folds the CRT in as well: zero external dependency,
  larger binaries.

Either gives the Linux bundle's benefits: one artifact decoupled from system
library versions, with gRPC pinned at build time (good for the protocol
fingerprint story). This needs a new `x64-windows-static-md` overlay triplet
alongside the existing four.

### Distribution channels

- **Scoop** — the best fit for a headless dev/CI tool: user-scoped, no admin, and
  a "bucket" is the exact analogue of the Homebrew tap (a small repo of JSON
  manifests pointing at the release ZIP, with autoupdate). A `scoop-beebium`
  bucket repo would mirror `homebrew-beebium`. Lead with this.
- **WinGet** — Microsoft's official manager, built into Windows 10/11, so the
  broadest reach. Submit a manifest to `microsoft/winget-pkgs` (PR-based) or
  self-host; automatable with `wingetcreate`. Second priority, for discoverability.
- **Chocolatey** — entrenched in enterprise CI but older and admin-oriented; not
  pursued.

### Code signing (Authenticode)

Unlike Linux, unsigned Windows executables trip SmartScreen/Defender on download —
the counterpart to macOS notarization. Since OV certificates without hardware
tokens were withdrawn in 2023, the current route is **Azure Trusted Signing**
(Microsoft's managed service, GA 2024): inexpensive, no hardware token,
SmartScreen-reputable, with a GitHub Actions integration. Eligibility currently
requires either a verified organisation or roughly three years of individual
identity history. Signing matters more for the GUI (end users download it) than
the server (devs/CI tolerate a warning), but is worth doing for both.

### GUI client (future)

The eventual Windows front end (the counterpart to the macOS app and the planned
`beebium-gui` cask) would ship as an **MSIX** package — the modern Windows app
format, with clean install/uninstall, sandboxing and auto-update — distributed
through the **Microsoft Store** (which handles signing, trust and updates) and/or
**WinGet**. A WiX Toolset v5 **MSI** would only be added if traditional
enterprise deployment (Intune/SCCM/Group Policy) is needed.

### Status

Done (validated as a proof of concept):

- `triplets/x64-windows-static-md.cmake` — static gRPC/protobuf/abseil, dynamic
  CRT. `dumpbin /dependents` confirms only system DLLs + the VC++ runtime + our
  own ABI DLLs.
- `cmake --install` produces the right tree on Windows with no fixups: `bin/`
  (exes + ABI DLLs + `extensions/`), `share/beebium/{roms,presets}`.
  `list-extensions` loads all built-in and dlopened extensions.
- `packaging/windows/make-zip.ps1` zips `bin/` + `share/` (root-level) into
  `beebium-server-<version>-windows-x64.zip` (~28 MB; the `lib/` import libraries
  are omitted).
- `packaging/scoop/beebium-server.json` — the Scoop manifest; a real
  `scoop install` (local-file URL + hash) created the four shims and the bare
  `beebium-model-b` command loaded every extension.

Remaining:

1. Wire it into `release.yml` (build the ZIP on a Windows runner, attach it to the
   GitHub Release) — like `macos-package.yml`.
2. Stand up a `scoop-beebium` bucket repo (the Scoop analogue of the Homebrew tap)
   and pin the release URL + hash into the manifest at release time.
3. A WinGet manifest (after a release exists).
4. Azure Trusted Signing (decide whether to sign from the first release or later).
5. Later: arm64 Windows (Windows on ARM), and the GUI MSIX + Store path.

The `static-md` vs full `static` decision is settled in favour of `static-md`
(dynamic CRT, only the VC++ Redistributable needed). The sign-now vs
ship-unsigned-first decision is still open.

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

`.github/workflows/macos-package.yml` builds, installs, tests and audits the
Homebrew formula on `macos-14` (arm64) by running
`packaging/homebrew/test-formula.sh`. The source build is cheap (~1 min), so it
runs on PRs that touch the formula or the build system, plus on demand. It is
**arm64 only**: GitHub's Intel `macos-13` runners are scarce and deprecated, and
a release-gating Intel leg stuck in the runner queue would block the draft
indefinitely.

`.github/workflows/macos-intel.yml` covers Intel (`x86_64`) **best-effort** — the
same `test-formula.sh`, on `macos-13`, triggered weekly / on demand / on
formula-or-build PRs. It is standalone and **never gates a release**, so a stuck
or absent Intel runner only affects that run. The formula is a source build, so
Intel Macs work for users regardless of CI. For reliable Intel coverage, register
an Intel Mac as a self-hosted runner and point `runs-on` at its label.

`.github/workflows/release.yml` ties these together: pushing a `v*` tag (the
final step of the `bump-my-version` release) builds the Linux bundles, builds the
Windows ZIP, validates the macOS formula, and creates a **draft** GitHub Release
with the Linux `.deb`/`.tar.gz` and the Windows `.zip` attached. The draft is
published manually; the Homebrew tap and Scoop bucket are then synced
(`packaging/homebrew/sync-tap.sh` and the Scoop equivalent). `linux-packages.yml`,
`macos-package.yml` and `windows-package.yml` all expose `workflow_call` so the
release flow reuses them.

### Smoke-test layers

End-to-end installability is checked at two layers, because the public download
URLs and the tap/bucket pins do not exist until *after* a release is published:

1. **Build-isolation smoke** — runs inside the package build, on a clean runner
   separate from the build, against the *just-built* artifact. It proves the
   artifact installs and runs without the build environment. Linux: `smoke-deb`
   (install the `.deb` in a fresh `debian:bookworm`, plus the Python interaction
   smoke) and `smoke-tarball` (extract in a fresh Arch). Windows: the `smoke` job
   in `windows-package.yml` extracts the ZIP on a fresh runner and runs a server.
   macOS is inherently covered because `brew install` *is* the build+install.

2. **Published end-to-end smoke** — `.github/workflows/release-smoke.yml`,
   `workflow_dispatch` with a `version` input, run **after** the release is
   published and the tap + bucket are synced. Each platform leg installs from the
   real public channel on its own clean runner: Linux downloads the `.deb`/
   `.tar.gz` from the Release URL; macOS runs `brew install beebium-server` from
   the tap; Windows runs `scoop install beebium-server` from the bucket. This is
   what exercises the actual URLs, hashes and package-manager plumbing.

## Status

**Done:**
- Self-contained static bundle for `amd64` and `arm64`, built and validated.
- `.deb` (correct per-arch `Architecture`, minimal deps, `/usr/bin` symlinks) and
  `.tar.gz`, both validated by install-in-clean-distro on Debian and Arch
  (x86_64 + Arch Linux ARM).
- The manual CI workflow that builds and smoke-tests both.
- macOS Homebrew formula (`beebium-server`): source build against Homebrew's
  grpc/protobuf, validated locally and in CI on arm64 (Intel best-effort)
  (`brew install`/`test`/`audit` + extension discovery + a Python-client
  fingerprint-handshake smoke).
- A release-tag workflow (`release.yml`) that builds Linux bundles, validates the
  macOS formula, and drafts a GitHub Release with the Linux artifacts attached.
- The tap `rob-smallshire/homebrew-beebium` is live with the formula;
  `brew install --HEAD beebium-server` validated end-to-end from the real tap.

**Remaining (macOS Homebrew):**
- Cut a tagged release and pin the checksum into the tap
  (`sync-tap.sh`) so plain `brew install beebium-server` works, not just `--HEAD`.
- Wire the tap's bottle-building workflow so installs pour a pre-built binary
  instead of compiling (matters at CI scale).
- Possibly add a Homebrew-free self-contained macOS `.tar.gz` for macOS CI
  (needs the vcpkg-static build path).

**Remaining (Windows):**
- The self-contained `x64-windows-static-md` ZIP + Scoop install are validated as
  a proof of concept (see [Windows](#windows)). Remaining is automation: a CI job
  in `release.yml`, a `scoop-beebium` bucket, WinGet, and Authenticode signing
  (Azure Trusted Signing).

**Remaining (other):**
- An AUR `beebium-bin` PKGBUILD that repackages the `.tar.gz` for Arch.
- Publishing the Python client to PyPI and the TypeScript client to npm.
- On-real-hardware validation on a 64-bit Raspberry Pi 4 / 400.
