# Serial subsystem: step-by-step refactoring plan

Status: working plan, on the `feature/serial` integration branch. Companion to
`docs/discussion/serial-architecture-review.md` (the why); this is the how.

Goal: evolve Mark Fisher's PR #44 (MC6850 ACIA + Serial ULA + host transport)
into the shape the review settled on -- the emulated chips in core, the
host-serial bridge as a built-in `PeripheralExtension` behind a `SerialPortDevice`
seam -- without ever leaving the tree broken, and with all three platforms green
at every step.

## Working principles

- Green ratchet: every phase ends with a commit that builds and tests clean on
  all three platforms. Small commits (see [[feedback_commit_ratchet]]).
- All-platforms-first: we do NOT begin refactoring until the as-merged code is
  green on macOS, Linux and Windows (Phase 0). Each later phase re-runs the same
  triad before it is considered done.
- Tests travel with the change: each phase names the tests it adds or moves. No
  phase is "done" on a successful build alone.
- No behaviour change until intended: Phases 1-2 are rename/structure only and
  must not alter runtime behaviour; behaviour moves in Phase 3+.
- Fix tools before leaning on them ([[feedback_fix_tools_first]]); targeted
  tests while iterating, full suite at phase end ([[feedback_targeted_tests]]).

## The cross-platform verification triad

Run these at the end of every phase. The exact serial test names are
`test_mc6850`, `test_serial_ula`, `test_serial_socket`, `test_serial_pty`
(POSIX only), plus the piconet ports tests that share the host primitives
(`test_posix_serial_port`, `test_win32_serial_port`).

macOS (local, here):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DBEEBIUM_BUILD_TESTS=ON -DBEEBIUM_BUILD_SERVICE=ON -DBEEBIUM_BUILD_SERVER=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R 'serial|mc6850|posix_serial|win32_serial|piconet'
ctest --test-dir build --output-on-failure   # full suite at phase end
```

Linux (Docker, mirrors the `server-linux` CI job; see `docs/linux-container.md`):

```bash
docker build -t beebium-linux-ci docker/linux-ci/   # first time / on Dockerfile change
docker run -d --name beebium-linux -v "$(pwd)":/workspace/beebium:ro -w /workspace \
  beebium-linux-ci sleep infinity
docker exec beebium-linux bash -c \
  "rm -rf /workspace/src && cp -r /workspace/beebium /workspace/src && rm -rf /workspace/src/build*"
docker exec beebium-linux bash -c \
  "cd /workspace/src && cmake -B build-linux -DCMAKE_BUILD_TYPE=Release \
     -DBEEBIUM_BUILD_TESTS=ON -DBEEBIUM_BUILD_SERVICE=ON -DBEEBIUM_BUILD_SERVER=ON && \
   cmake --build build-linux --parallel 2 && \
   ctest --test-dir build-linux --output-on-failure"
```

Windows (Slioch, over SSH): follow the documented workflow in
`docs/local-windows-development.md` (gitignored, machine-specific: SSH command,
VS DevShell preamble, configure/build/test commands, repo path). Do not
duplicate those commands here -- that doc is the single source of truth for the
Slioch setup.

The one wrinkle that doc does not cover: it assumes a `git pull`, but
`feature/serial` is not pushed. Two options, in order of preference:
- Rob pushes `feature/serial` once, then Slioch uses the doc's normal `git pull`
  / build / test flow each phase.
- Otherwise move the branch as a git bundle (no origin push):
  `git bundle create /tmp/serial.bundle master..feature/serial`, scp it over,
  then fetch from the bundle on Slioch and build/test per the doc.

Client suites (run when a phase touches a client or the proto):

```bash
# Python
cd clients/python && uv run --with pytest python -m pytest tests/ -v
# TypeScript (once a serial client exists)
cd clients/typescript && npm ci && npm run generate-protos && npx tsc --noEmit && npm run test:unit
# macOS GUI (once a serial panel exists)
xcodebuild test -scheme BeebiumTests -destination 'platform=macOS'
```

## Phase 0 -- Establish the green cross-platform baseline (no refactoring)

Prove the as-merged PR builds and tests on all three platforms before touching
it. This is the "start from everything working on all platforms" step and gives
us the regression net for everything after.

1. macOS: full configure/build/test (serial suite already passes here; confirm
   the service+server build and the full ctest are clean).
2. Linux/Docker: build-linux + full ctest. Expect possible GCC-vs-Clang and
   `<pty.h>` vs `<util.h>` / `posix_openpt` portability nits; fix minimally.
3. Windows/Slioch: bundle across, configure/build/test. Confirm:
   - `Win32SerialPort.cpp` compiles and links into `beebium_core`;
   - `test_serial_pty` is correctly skipped (`if(NOT WIN32)`);
   - `SerialService` builds (it includes `Win32SerialPort.hpp` under `#else`);
   - the `--serial device:` path is reachable.
4. Gaps to record, not necessarily fix yet:
   - There is no automated Windows serial round-trip (PTY is POSIX-only). Note
     options for later: a `com0com` null-modem pair on Slioch (manual), or a
     Windows named-pipe endpoint analogous to the PTY test.
   - CI builds serial everywhere but runs no serial transport round-trip on
     Windows; flag for Phase 7.

Commit: "serial: cross-platform baseline green" (with any portability fixes).
Tests: no new behaviour; ensure existing serial tests pass on macOS+Linux and
build on Windows.

## Phase 1 -- Lock seam vocabulary and naming (no behaviour change)

5. Resolve the naming collision. The PR uses `SerialPort` / `PosixSerialPort` /
   `Win32SerialPort` / `PtyMaster` for the HOST OS port. Rename these to a
   host-scoped family (e.g. `HostSerialPort`, or a `beebium::serial::host`
   namespace) so the `SerialPort...` names are free for the BBC-side seam. Update
   the piconet shims and all includes. Pure rename.
6. Introduce the `SerialPortDevice` seam -- the serial analogue of
   `UserPortDevice` / `OneMHzBusDevice` -- folding `SerialDataSource` +
   `SerialDataSink` into one bidirectional device interface (or renaming the
   split pair to the house pattern). `Loopback`/`Scriptable`/host endpoints
   implement it unchanged.
   Tests: rename-only; all existing tests still pass on the triad. Add one small
   test asserting a `SerialPortDevice` round-trips through loopback and
   scriptable under the new names.

## Phase 2 -- Model presence correctly (two axes)

See review Correction 1: Axis A = chips + RS423 (one `SerialSocket`,
`HasSerialSocket`), Axis B = cassette (own per-variant flag; absent on Compact).

7. Confirm `HasSerialSocket` is wired true for Model B / B+ / Master 128 and is
   expressible as absent/optional for the (future) Model A / Master Compact;
   encode the policy now even though those variants are unimplemented.
8. Add a cassette-presence concept to the model (a flag), independent of chip
   presence, ready for a later cassette seam -- do not derive it from
   `HasSerialSocket`.
   Tests: assert `has_serial_socket` is true on Model B; assert the service and
   any client path degrade cleanly when the socket is absent (a variant or a
   compile-time stub without the socket). Triad.

## Phase 3 -- Extract the host-serial bridge into a built-in PeripheralExtension

The core architectural move. Mirror the User Port / 1MHz bus pattern.

9. Expose a serial attachment point through `ExtensionContext` (analogous to
   `UserPort` / `OneMHzBusPort`), backed by `SerialSocket`'s set_source/set_sink.
10. Create a built-in `HostSerialExtension : PeripheralExtension, SerialPortDevice`
    that owns the `HostSerialEndpoint` + host port + `PtyMaster` and offers the
    pty / device / loopback / scriptable modes. `attaches_to()` the serial port;
    register it in `BuiltinExtensions.hpp`.
11. Move transport selection out of `SerialService` / `ServerMain` into the
    extension's configuration (manifest parameters), folding `--serial` into the
    existing extension-configuration mechanism ([[project_extension_config]]).
    Keep `loopback` / `scriptable` available for tests and demos.
12. Fix the unbounded-TX default while here: default to NONE (TX discarded) or
    cap the scriptable queue, rather than silently growing.
    Tests: extension-load test per platform; PTY round-trip now exercised through
    the extension (POSIX); scriptable round-trip; a test proving the serial host
    primitives are still shared correctly with the piconet extension (no
    regression). Triad. On Windows, validate `device:` against a `com0com` pair
    on Slioch (manual) and record the result.

## Phase 4 -- gRPC and clients aligned with conventions

13. Replace the polling `GetSerialStatus` snapshot with a server-pushed
    `WatchSerialStatus` stream, mirroring `WatchEconetStatus`
    ([[project_econet_status_streaming]]). Keep a one-shot status if useful.
14. Decide service split: serial status/diagnostics stays a gRPC service;
    transport configuration + UI go through `ExtensionUiService` so the GUI
    clients get a panel + indicator for free
    (`docs/discussion/extension-ui-architecture.md`,
    [[project_serial_port_selector]]). Retire the bespoke transport RPCs from
    `SerialService` if they are subsumed.
15. Client parity: regenerate proto stubs; bring the Python client up to the new
    surface; add a TypeScript serial client (parity with AUN/Piconet/Econet,
    [[project_typescript_client_cutover]]); add the macOS Swift client + serial
    UI panel/indicator (hidden when `has_serial_socket` is false).
    Tests: streaming test; Python integration (Linux + macOS + Windows, as the
    matrix already does for other services); TS unit + integration (Linux +
    macOS); macOS `BeebiumTests`. Triad for the C++ side.

## Phase 5 -- Presets, CLI tidy, docs

16. Make the serial transport presettable by folding it into extension config in
    presets ([[project_preset_sideways_config]] is the precedent).
17. Consolidate `--serial` spec parsing into one place with a shell-safe
    separator (the AUN `@` precedent, [[feedback_is_list_inner_separator]]);
    remove the duplicated string/enum parsing across `ServerMain` and the
    service.
18. Update `docs/serial-acia.md` to the extension model and refresh CLI docs.
    Tests: a preset round-trip test that includes a serial transport. Triad.

## Phase 6 -- Cassette seam (designed-for now, may be deferred)

19. Add a `CassetteDevice` seam attaching via the ULA cassette-select path,
    gated by the cassette-presence axis -- NOT routed through the byte-level
    `SerialPortDevice`. Implementing cassette I/O can be a separate effort; the
    point of doing the seam (or at least reserving it) now is to keep the RS423
    seam RS423-shaped. Decide with Rob whether this is in-scope for the first
    master merge or a follow-up.

## Phase 7 -- CI integration and merge to master

20. Ensure the serial C++ tests run in CI on every platform (they build today;
    confirm they execute). Add the TS serial integration job alongside the
    existing AUN/Piconet ones. Decide on a Windows serial round-trip story
    (com0com on a self-hosted runner, a named-pipe endpoint, or documented
    manual Slioch validation).
21. Final full-suite run across the triad + all client suites.
22. Curate history as desired and merge `feature/serial` into `master`.

## Decisions needed from Rob (do not block Phase 0)

- Pushing `feature/serial`: Phase 0 Windows testing needs the branch on Slioch.
  Default is a git bundle over SSH (no origin push). Confirm, or push the branch.
- Phase 4: retire the bespoke serial transport RPCs in favour of extension-UI
  configuration, or keep both surfaces?
- Phase 6: cassette seam in this effort, or explicit follow-up?

## References

- `docs/discussion/serial-architecture-review.md` (the rationale).
- `docs/discussion/extension-ui-architecture.md`,
  `docs/discussion/serial-port-selector-control.md`.
- `docs/linux-container.md`, `docs/local-windows-development.md` (gitignored).
- `.github/workflows/ci.yml` (the platform/client matrix to extend).
- External-port precedents: `src/core/include/beebium/extension/UserPortDevice.hpp`,
  `OneMHzBusDevice.hpp`, `PeripheralExtension.hpp`; Acorn RTC + SCSI extensions.
