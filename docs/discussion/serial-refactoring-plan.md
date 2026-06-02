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

## Phase 1 -- Introduce the device seam (DONE, commit 4d74a7f)

6. Introduce the `SerialPortDevice` seam -- the serial analogue of
   `UserPortDevice` / `OneMHzBusDevice`. Rather than folding `SerialDataSource` +
   `SerialDataSink` away (which would break the ULA's per-direction unit tests,
   since a single bidirectional loopback echoes a TX byte back mid-test), we
   KEPT the two half-seams as the bit engine's internal interface and made
   `SerialPortDevice` inherit both. So the ULA still pulls RX / pushes TX through
   the split (tests untouched), while the socket and the outside world speak one
   device. `SerialSocket` gained `set_device(shared_ptr<SerialPortDevice>)` in
   place of `set_source`/`set_sink`; `Loopback`/`Scriptable`/`Host` endpoints and
   `SerialService` updated to match. No behaviour change.
   Tests: existing serial tests still pass on all three platforms; added a
   socket-level `set_device` loopback round-trip test.

5. (MOVED to Phase 3) Resolve the `SerialPort` naming collision. The PR uses
   `SerialPort` / `PosixSerialPort` / `Win32SerialPort` / `PtyMaster` for the HOST
   OS port. Renaming these to a host-scoped family frees `SerialPort` for the
   BBC-side port handle -- but that handle is not introduced until Phase 3, and
   the rename is large churn across the whole Piconet subsystem and its tests.
   Deferred to Phase 3 (step 8a) so the rename lands together with the consumer
   that actually needs the freed name, rather than as speculative churn now.

## Phase 2 -- Model presence correctly (DONE)

See review Correction 1: Axis A = chips + RS423 (one `SerialSocket`,
`HasSerialSocket`), Axis B = cassette (own per-variant flag; absent on Compact).

7. DONE. `HasSerialSocket` already gates Axis A correctly: it detects the
   serial_socket member, so all current Model B variants satisfy it and a future
   Master Compact that omits the member is automatically excluded; the stack
   already guards on it (e.g. SerialService reports has_serial_socket = false,
   and apply_endpoint_mode errors with "Machine has no serial socket"). Hardened
   the concept's documentation to the two-axis model and added static_assert
   coverage (positive: ModelBHardware; negative: a socket-less stub) so a future
   socket-less variant is provably excluded.
8. DEFERRED with cassette. Axis B (cassette presence) is its own per-variant
   property, but there is no consumer for a cassette-presence flag yet and
   inventing the per-variant mechanism now would pre-empt the cassette follow-up
   (Phase 6). So the flag moves to that follow-up. The obligation discharged
   here is only that cassette is NOT derived from `HasSerialSocket` -- recorded
   in the concept's doc comment.
   Tests: static_assert that `HasSerialSocket` is true for Model B and false for
   a stub without the socket (the gating mechanism the whole stack relies on).

## Phase 3 -- Extract the host-serial bridge into a built-in PeripheralExtension

The core architectural move. Mirror the User Port / 1MHz bus pattern.

Design decisions (confirmed with Rob): mirror `UserPort` / `UserPortDevice`, NOT
the Tube (TubeSocket-in-ExtensionContext is a special coprocessor case, not the
peripheral-attachment convention). Names: BBC handle = `beebium::SerialPort`,
device = `beebium::SerialPortDevice`. Ownership: the device slot is NON-OWNING
(`UserPort::attach(UserPortDevice&)` model) -- attachers own the device (the
extension owns itself; SerialService keeps its scriptable/loopback in its own
members; tests keep locals).

8a. DONE (commit 11dbb2c, green macOS+Linux+Windows). Renamed the host OS serial
    port `beebium::serial::SerialPort` -> `HostSerialPort` (Posix/Win32/Pty
    derive from it unchanged); the piconet shim keeps `beebium::piconet::SerialPort`
    as an alias to `HostSerialPort`, so Piconet code/tests are untouched. This
    frees `beebium::SerialPort` for the BBC port handle.
9. DONE (commit ef5bda2, green macOS+Linux+Windows). Added `beebium::SerialPort`
   (extension/SerialPort.hpp): `attach(SerialPortDevice&)` / `detach()` /
   `is_occupied()`, single-device, throws on a second attach -- the `UserPort`
   analogue, header-only, non-owning. `SerialSocket::set_device` is now
   non-owning (raw `SerialPortDevice*`); SerialService keeps its endpoints alive
   and passes `.get()`. All four Model B variants expose `serial_port()`;
   `HasSerialPort` concept added; `ExtensionContext` gains `SerialPort*` (get/has),
   wired from ServerMain. Handle unit test added (attach/occupancy/throw/detach/
   round-trip). No consumer attaches yet -- that is step 10.
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
14. Keep BOTH surfaces (decided): the bespoke typed `SerialService` RPCs for
    programmatic/scripting clients (Python, tests) AND `ExtensionUiService` for
    the GUI clients (panel + indicator for free). These serve different
    audiences and are not redundant -- see [[feedback_extension_multi_api]] and
    `docs/discussion/extension-ui-architecture.md`,
    [[project_serial_port_selector]]. Do not retire the typed RPCs in favour of
    the UI dispatch; tidy and align them, but keep them.
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

## Phase 6 -- Cassette seam (DEFERRED to an explicit later follow-up)

19. Cassette is out of scope for this effort (decided). It will be a separate,
    later piece of work: a `CassetteDevice` seam attaching via the ULA
    cassette-select path, gated by the cassette-presence axis, NOT routed through
    the byte-level `SerialPortDevice`. The only obligation on THIS effort is to
    keep the RS423 seam RS423-shaped so it does not foreclose that future seam
    (already covered in Phases 1-2); do not build cassette I/O now.

## Phase 7 -- CI integration and merge to master

20. Ensure the serial C++ tests run in CI on every platform (they build today;
    confirm they execute). Add the TS serial integration job alongside the
    existing AUN/Piconet ones. Decide on a Windows serial round-trip story
    (com0com on a self-hosted runner, a named-pipe endpoint, or documented
    manual Slioch validation).
21. Final full-suite run across the triad + all client suites.
22. Curate history as desired and merge `feature/serial` into `master`.

## Decisions (resolved with Rob)

- Pushing `feature/serial`: YES. Rob granted explicit, scoped permission to push
  and pull the `feature/serial` branch ONLY, between this machine and origin /
  Slioch. So Slioch follows the normal `git pull` workflow in
  `docs/local-windows-development.md`; no bundle needed.
- Phase 4: KEEP BOTH the typed `SerialService` RPCs (programmatic clients) and
  the `ExtensionUiService` path (GUIs). They serve different audiences.
- Phase 6: cassette is an EXPLICIT LATER FOLLOW-UP, not part of this effort.

## References

- `docs/discussion/serial-architecture-review.md` (the rationale).
- `docs/discussion/extension-ui-architecture.md`,
  `docs/discussion/serial-port-selector-control.md`.
- `docs/linux-container.md`, `docs/local-windows-development.md` (gitignored).
- `.github/workflows/ci.yml` (the platform/client matrix to extend).
- External-port precedents: `src/core/include/beebium/extension/UserPortDevice.hpp`,
  `OneMHzBusDevice.hpp`, `PeripheralExtension.hpp`; Acorn RTC + SCSI extensions.
- `docs/datasheets/MC6850_ACIA.pdf` -- the MC6850 ACIA datasheet; the
  authority for validating register semantics (control/status bits, TDRE/RDRF,
  /DCD, /CTS, IRQ behaviour) when reviewing and extending the ACIA tests.
