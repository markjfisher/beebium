# macOS App Packaging

How the Beebium macOS app (`clients/macos/Beebium`) bundles the headless
emulator servers and their native dependencies into a self-contained
`.app`, and what remains to be done for public distribution.

## Background

Beebium's architecture is multi-process: the Swift/Metal frontend launches
a headless C++ server executable (`beebium-model-b`, `-plus`, `-romram`)
and talks to it over gRPC. For a distributable app, those server
executables — and everything they load — must travel inside the `.app`.

The servers are not trivially relocatable. They link:

- **Internal shared libraries** built by this repo: `libbeebium_extension_api.dylib`
  and `libbeebium_extension_ui_proto.dylib` (the latter is deliberately a
  shared library so plugins can share its protobuf descriptor pool — see
  `docs/` notes on the extension UI service).
- **Homebrew libraries**: gRPC, protobuf, abseil, OpenSSL, c-ares, re2 and
  their transitive dependencies (~100 dylibs).
- **Plugins** loaded at runtime via `dlopen` from `<exe-dir>/extensions/`:
  the transport and peripheral extensions (piconet, acorn-scsi, acorn-rtc,
  scsi-hard-disc, test-scratch-ram). Built-in extensions (AUN, the 65C02
  coprocessor) are statically linked and need no plugin files.

As built, the servers and plugins reference the internal libraries via an
`@rpath` that points at the absolute development build tree
(`<repo>/build/lib`), and the Homebrew libraries via absolute
`/opt/homebrew/...` paths. Neither exists on an end user's machine, so an
app that merely copies the executables only runs where it was built.

## Bundle layout

```
Beebium.app/Contents/
├── MacOS/Beebium                     # the Swift app
├── Frameworks/                       # (Swift app's own dylibs only, if any)
└── Resources/
    ├── roms/                         # bundled ROMs
    ├── presets/                      # bundled machine presets + thumbnails
    └── servers/                      # <-- the headless server payload
        ├── beebium-model-b
        ├── beebium-model-b-plus
        ├── beebium-model-b-romram
        ├── lib*.dylib                # internal + Homebrew deps, @rpath-ified
        └── extensions/
            └── <name>/
                ├── <name>.dylib      # the plugin
                └── manifest.json     # plugin manifest
```

### Why `Resources/servers/`, not `Frameworks/`

The obvious home for embedded code is `Contents/Frameworks/`, but
`codesign` validates that directory as **code-only**: the plugin tree's
`manifest.json` files trip it with *"code object is not signed at all / In
subcomponent: .../extensions/<name>/manifest.json"* and the build fails at
the final app-signing step.

The server payload is a mix of executables, dylibs, and resource files, so
it lives under `Resources/` instead, where `codesign` seals it as ordinary
bundle resources (nested Mach-O included). The server resolves its plugins
relative to its own location (`<exe-dir>/extensions/`), so keeping the
executables, their dylibs, and `extensions/` together in one directory is
all that's required. The Swift side finds this directory via
`Bundle.main.resourcePath + "/servers"` (`PresetManager.serversDirpath`).

## Making the payload self-contained

The build phase **"Embed Server Executables, Presets, and ROMs"** in
`clients/macos/Beebium/project.yml` copies the executables, presets, ROMs,
and the `extensions/` tree into the bundle, then runs
`clients/macos/Beebium/scripts/bundle_dependencies.py` over
`Resources/servers/`.

`bundle_dependencies.py` walks the dependency graph of every server
executable and every plugin and, for each non-system dependency:

1. Copies the real dylib into `Resources/servers/` (next to the executables).
2. Rewrites every reference to it as `@rpath/<name>` (`install_name_tool
   -change`), and sets each copied dylib's own id to `@rpath/<name>`.
3. Gives each binary an `@rpath` that resolves to `Resources/servers/`:
   `@loader_path` for the executables and the flat dylibs, `@loader_path/../..`
   for plugins nested under `extensions/<name>/`.
4. Deletes the absolute build-tree and Homebrew `LC_RPATH` entries, so the
   bundle is the *only* resolution source (this is also what guarantees
   portability).
5. Re-signs each modified Mach-O **ad-hoc** (`codesign --force --sign -`).
   This is mandatory on Apple Silicon: dyld refuses to load a Mach-O whose
   signature no longer matches after `install_name_tool` edits it.

System libraries (`/usr/lib/`, `/System/`) are left untouched — they exist
on every macOS install.

The dependency resolver mirrors dyld's behaviour of searching the `@rpath`
list of every binary in the load chain (not just the immediate referrer):
Homebrew's gRPC `upb` libraries, for instance, reference siblings via
`@rpath` but carry no rpath of their own, relying on the top-level
executable's `/opt/homebrew/lib`. The script seeds a global search-path
fallback from every root's absolute rpaths to cover this.

## Building a self-contained app

The embedding only runs when `BEEBIUM_SERVERS_BUILD_DIR` points at a built
server tree (the directory containing the server executables, their
`presets/`, and `extensions/`):

```bash
# 1. Build the servers + plugins
cmake --build build --target beebium-servers -j

# 2. Build the app with embedding enabled
cd clients/macos/Beebium
xcodegen generate   # if project.yml changed
xcodebuild build -scheme Beebium -destination 'platform=macOS' \
  BEEBIUM_SERVERS_BUILD_DIR="$PWD/../../../build/src/server"
```

Without `BEEBIUM_SERVERS_BUILD_DIR`, the embed phase is skipped and the
app falls back to launching servers from the development build tree (or
from `$BEEBIUM_SERVERS_DIRPATH`); convenient for day-to-day development.

### Verifying self-containment

```bash
APP=~/Library/Developer/Xcode/DerivedData/Beebium-*/Build/Products/Debug/Beebium.app
SRV="$APP/Contents/Resources/servers"

# No dependency should point outside the bundle (only /usr/lib, /System OK):
find "$SRV" -type f | while read -r f; do
  file "$f" | grep -q Mach-O && otool -L "$f" | tail -n +2 \
    | grep -E '/opt/homebrew|/Users/|/usr/local' && echo "LEAK: $f"
done

# The signature must verify:
codesign --verify --deep --strict "$APP"
```

## Remaining work for distribution

The bundle above is self-contained and runs on a clean machine, but it is
only **ad-hoc signed**. Shipping it to other users requires:

1. **Developer ID signing.** Re-sign every nested dylib, every server
   executable, the plugins, and the app with a *Developer ID Application*
   certificate (not ad-hoc). The `bundle_dependencies.py` ad-hoc signing
   step would be replaced (or followed) by signing with the real identity,
   leaf-first.
2. **Hardened Runtime + entitlements.** Notarization requires the Hardened
   Runtime. Because the servers `dlopen` plugins and load our own dylibs,
   evaluate whether `com.apple.security.cct.allow-dyld-environment-variables`
   / disable-library-validation entitlements are needed, or — preferably —
   sign all nested code with the *same* Team ID so Library Validation is
   satisfied without weakening it. (Ad-hoc nested code under a
   Developer-ID-signed, library-validated app will be **rejected** at load
   time — this is the main reason the current bundle is dev-only.)
3. **Notarization + stapling.** Submit the signed app (zipped) to Apple's
   notary service (`notarytool`), then `stapler staple` the ticket.
4. **Distribution medium.** Package as a signed/notarized DMG or zip.

These steps are out of scope for the current build, which targets a working
self-contained development bundle. They should be added as a separate,
opt-in build configuration (e.g. a Release scheme with signing identity and
a notarization script) when distribution begins.

## Related

- `docs/deployment.md` — ROM discovery and FHS install layout for the
  server executables outside the macOS app.
- `clients/macos/Beebium/scripts/bundle_dependencies.py` — the bundling
  implementation.
