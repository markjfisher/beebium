# gRPC sync-server streaming race on Windows

A reproducible server crash when two server-streaming RPC handlers
transition (begin or end) near-simultaneously on Windows. Surfaces in
production as the macOS client connecting to a Windows-built server
and switching to the Network sidebar — the panel briefly renders, the
server stalls, the BBC video freezes, the process disappears with a
0xC0000005 access violation and no stderr output.

Status: Open. Discovered while doing manual Windows verification of the
`piconet-device-path-editor` branch; the bug reproduces on `master`
with no extensions loaded, so it predates that branch and is unrelated
to its content. This document is the starting point for a separate
branch that fixes it properly.

---

## Symptoms

- macOS Beebium frontend connects to a Windows-built `beebium-model-b*`
  server. Sidebar starts on Storage; everything works (video stream,
  audio stream, indicators, etc.).
- User switches to the Network panel. Both `aun` and `piconet`
  `ExtensionPanelView`s spawn, each opening a server-streaming
  `ExtensionUiService.SubscribeView` RPC.
- Within ~1 second the Piconet panel content disappears, the BBC
  video frame freezes (cursor stops flashing), and the server process
  exits silently.

The same scenario on a macOS-built server is fully stable.

## Repro (headless)

A Python script using two concurrent `SubscribeView` calls reproduces
the crash deterministically. With no extensions loaded on the server:

```python
import grpc, threading, time
from beebium._proto import extension_ui_pb2, extension_ui_pb2_grpc
from beebium._proto import system_pb2, system_pb2_grpc

ch = grpc.insecure_channel('localhost:50100')
ui = extension_ui_pb2_grpc.ExtensionUiServiceStub(ch)
sysv = system_pb2_grpc.SystemServiceStub(ch)

def open_stream(name):
    try:
        for v in ui.SubscribeView(
                extension_ui_pb2.SubscribeViewRequest(extension_name=name)):
            pass
    except grpc.RpcError:
        pass

threading.Thread(target=open_stream, args=('aun',), daemon=True).start()
threading.Thread(target=open_stream, args=('piconet',), daemon=True).start()
time.sleep(2)
sysv.GetSystemInfo(system_pb2.GetSystemInfoRequest(), timeout=3)
# UNAVAILABLE -- server has crashed.
```

One stream by itself is fine. A second concurrent `SubscribeView`
(any extension name, including the same one twice) reliably crashes
the server.

## Crash signature

Windows Error Reporting captures (with `LocalDumps` enabled in the
registry):

```
Faulting application name: beebium-model-b-romram.exe
Exception code: 0xc0000005      (ACCESS_VIOLATION)
Fault offset:  0x00000000156e2f
Faulting module name: beebium-model-b-romram.exe
Fault bucket: 1833214760671568167  (consistent across reproductions)
```

The fault offset is inside our binary, but immediately after a return
from `ExtensionUiServiceImpl::SubscribeView` -- so almost certainly
inside gRPC's status-completion code path that runs after the handler
returns. Without symbol files (Release builds emit no .pdb on the
default config) we can't pin down the exact gRPC source line; that's
the next investigation step.

We do not have `cdb` / `windbg` available on the Slioch dev box and
`winget install Microsoft.WinDbg` fails over SSH (logon-session
restriction). A `RelWithDebInfo` build plus a debugger on a
non-headless Windows session would be the path to a precise stack
trace.

## What was investigated

Tactical logging in the handler narrowed the failure window to
"between handler return and gRPC's status-completion send":

```cpp
[XUI] SubscribeView entry name=aun tid=117768
[XUI] find_extension returned 0000000000000000 tid=117768
[XUI] ui=0000000000000000 tid=117768
[XUI] returning NOT_FOUND tid=117768
... pacing line ...
[XUI] SubscribeView entry name=aun tid=114216
[XUI] find_extension returned 0000000000000000 tid=114216
[XUI] ui=0000000000000000 tid=114216
[XUI] returning NOT_FOUND tid=114216
                                        <-- crash here, no further output
```

The first handler logs all four steps, gRPC sends the NOT_FOUND
status, server keeps running (a Pacing line appears). The second
handler logs all four steps, then the server crashes after returning
but before the next emulation tick.

## What didn't fix it

Each tested in isolation, then in combination, against the headless
repro. Default sync-server settings are `NUM_CQS=1`, `MIN_POLLERS=1`,
`MAX_POLLERS=2`.

| Attempted change | Result |
|---|---|
| `MAX_POLLERS=32`, `MIN_POLLERS=4` | Stalls less on the begin path but server still dies later (e.g. on stream cancel from client disconnect). |
| `NUM_CQS=8` (separate completion queues per CQ) | Same -- crashes near begin or end of two near-concurrent streams. |
| Replace `return grpc::Status(NOT_FOUND, ...)` with a wait-for-cancel loop returning OK | Crash window moves from stream begin to stream end; same race on close. |
| All three together | Same. |

The only thing that reliably suppressed the crash was a 200ms sleep
*before* returning from the handler -- which staggered the two
returns in time enough that gRPC's status-completion paths didn't
overlap. That's a workaround, not a fix; we will not ship a
hard-coded sleep into a hot RPC path.

## Likely root cause

A race in gRPC's sync-server `Status` completion machinery on Windows
when two server-streaming RPC handlers transition (call enter, status
send, or call end) on the same `ServerCompletionQueue` near-
simultaneously. Possible specific shapes:

1. **CallOpSet / batch-op cleanup race.** When a streaming handler
   returns, gRPC enqueues a `CallOpSendStatusFromServer` op. If two
   such ops complete on the same CQ within the same poll cycle, the
   per-call structures freed by one may be touched by the other.
2. **Sync-server worker-pool spawn race.** The default 2-poller pool
   spawns more workers under load. The Windows-specific spawn path
   may have a UAF on the per-poller call data.
3. **iocp + completion-queue interaction.** Windows uses IOCP under
   the gRPC CQ; completion ordering and cancellation interact in a
   way that POSIX (epoll-based) does not.

(Bumping `NUM_CQS` and `MAX_POLLERS` reduces collision probability
but doesn't eliminate it, so the bug is not pure poller starvation.)

A definitive answer needs a `RelWithDebInfo` build of beebium-model-b
plus a stack trace from windbg / cdb. Capturing that is the first
concrete task on this branch.

## Why a workaround is wrong

Several "fixes" suggest themselves but each leaves a sharp edge:

- **Hard-coded sleep before status send.** Reduces throughput, hides
  the bug, becomes load-bearing dead weight.
- **Server-wide mutex around handler return.** Serialises every
  streaming RPC, defeats parallelism, doesn't actually serialise
  what's racing (gRPC's internal completion).
- **Limit the macOS client to one streaming RPC at a time.** Pushes
  the workaround to clients; trivially broken by future code that
  also opens multiple streams; doesn't help the Python or
  TypeScript clients.
- **`#ifdef _WIN32` and use the async API only on Windows.** Two code
  paths in the most-trafficked part of the system, divergence-prone.

## Proposed direction

Migrate the server-streaming services onto gRPC's **async / callback
API** (`grpc::Server::async`). The async API uses an event-driven
state machine instead of one thread per call, which sidesteps the
sync-server completion path entirely. Apple, Google, and many large
gRPC users run their services this way for exactly this kind of
reliability.

Scope:

1. **Concrete diagnosis first.** Capture a RelWithDebInfo + windbg
   stack trace from the existing repro so we know precisely which
   gRPC function is access-violating. May reveal a simpler fix than
   a full async migration (e.g. a known gRPC Windows patch we just
   need to update past). Track upstream gRPC issues that match the
   signature.
2. **Pilot async on `ExtensionUiService` only.** It's the smallest
   server-streaming surface (one method, simple state) and the one
   that surfaces the bug. Prove the async pattern works end-to-end:
   subscribe, mark_dirty bumps revision, push, dispatch, all the
   existing tests still pass.
3. **Migrate the other server-streaming RPCs** (`VideoService`,
   `AudioService`, `IndicatorService`, `EconetService.WatchEconetStatus`)
   in follow-up commits. Each has slightly different semantics
   (binary frames vs. typed messages) but the same async-state-
   machine shape.
4. **Tear out** the workarounds we discover are necessary
   (e.g. the `if(NOT WIN32)` guards on `test_grpc_extension_ui_service`
   and `test_grpc_piconet_ui` -- those exist because the gRPC test
   fixture's sync-server hosting hits the related Windows DLL-
   boundary issue, see `tests/CMakeLists.txt`).

## What's NOT in scope on this branch

- Switching the *client* APIs to async. The client side is fine on
  both platforms; only the server hosting tickles the bug.
- Refactoring the `ExtensionUi` extension-side abstraction. Its
  contract (`build_view`, `handle_event`, `mark_dirty`) is unchanged;
  only the gRPC plumbing under it changes.
- The Windows DLL-boundary issue with the gRPC test fixture (the
  `if(NOT WIN32)` guards in `tests/CMakeLists.txt` mention "MSVC
  rough edge with `dllexport_decl` + gRPC service stubs"). This
  branch may be able to revisit that incidentally; it's not the
  primary objective.

## Open questions

- Can we identify a *specific* gRPC source line / commit / version
  involved? Our repro is small enough to bisect against gRPC versions
  if needed (vcpkg pins a particular version).
- Is the async API supported by the gRPC version we currently use
  (vcpkg-pinned)? If not, what's the upgrade path?
- Does the async server support Streaming-Server-Streams-To-Many-
  Clients efficiently for our typical fan-out (one machine, many
  potential clients)? The video stream is high-bandwidth.
- Are there existing Beebium services that would benefit from the
  async refactor independently of this bug? (E.g. CPU efficiency on
  Linux when many clients subscribe.)
- Workflow: do we want one big PR or a sequence (proof-of-concept on
  ExtensionUiService, then service-by-service)?

## References

- `tests/CMakeLists.txt` -- the existing `if(NOT WIN32)` guards on
  gRPC tests document a related but distinct DLL-boundary issue.
- `src/service/include/beebium/service/ExtensionUiService.hpp` --
  current sync-server-based implementation; the migration target.
- `src/service/include/beebium/service/Server.hpp` -- where the
  `grpc::ServerBuilder` settings live.
- macOS frontend: `clients/macos/Beebium/Beebium/SidebarModeContent.swift`
  opens both extension panels concurrently when the Network tab
  appears, which is what triggers the production-side repro.
