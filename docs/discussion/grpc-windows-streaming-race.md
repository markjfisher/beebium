# Cross-DLL `ExtensionUiService` access violation on Windows

A reproducible server crash when two `ExtensionUiService.SubscribeView`
streams run concurrently on Windows. Surfaces in production as the
macOS client connecting to a Windows-built server and switching to the
Network sidebar — the panel briefly renders, the server stalls, the
BBC video freezes, the process disappears with a 0xC0000005 access
violation and no stderr output.

Status: Open. Discovered while doing manual Windows verification of the
`piconet-device-path-editor` branch; the bug reproduces on `master`
with no extensions loaded, so it predates that branch and is unrelated
to its content. This document is the starting point for a separate
branch that fixes it properly.

The first revision of this document framed the bug as a "sync-server
streaming race" and proposed migrating to gRPC's async API. After
finding the upstream issue (gRPC #39198) the diagnosis is much
narrower: this is the well-known
**gRPC service-implementation-across-a-DLL-boundary** bug. The CMake
note already in `tests/CMakeLists.txt` referring to a
"MSVC rough edge with `dllexport_decl` + gRPC service stubs" is the
same problem viewed from the test-fixture side. The proposed fix is
correspondingly smaller and more targeted — see
[Proposed direction](#proposed-direction) below.

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

## Root cause

The bug is reported upstream as
**[gRPC issue #39198](https://github.com/grpc/grpc/issues/39198)** —
"gRPC server has a 'read access violation' when a service is not
implemented in the same dll of the server". Quoting the report:

> The crash occurs specifically when:
> - A gRPC server is instantiated in one DLL (A.dll)
> - A service implementation is in a different DLL (B.dll)
> - The service is added to the server and an RPC method completes
>
> The error manifests as `closure_list was 0x8` in `closure.h` line 232.
> ... closure list pointers become invalid when crossing DLL boundaries.

That is exactly our build shape on Windows:

| Component | Lives in | Module on Windows |
|---|---|---|
| `Beebium_ExtensionUiService::Service` (protoc-generated base) | `beebium_extension_ui_proto` | **DLL** (deliberate `SHARED` so plugins share proto descriptors) |
| `ExtensionUiServiceImpl` (our handler subclass) | `beebium_service` (static lib, header-only inline class) | **EXE** (linked into each `beebium-model-b*` binary) |

Every other gRPC service in the system has its proto stubs and impl
in the same static library (`beebium_service`), so when the EXE links
the library they end up in one module and there's no boundary to
cross. `ExtensionUiService` is the outlier because its proto is in a
shared library by design — that's how plugins get a single registered
copy of the descriptors.

This explains every observation:

- **Reproduces on master with no extensions loaded.** The bug is in
  the gRPC service plumbing for `ExtensionUiService`, not in any
  extension's UI code.
- **One concurrent stream is fine; two crash.** The closure-list
  corruption only manifests when two ops complete on the same CQ
  with state straddling the DLL boundary.
- **Bumping pollers / CQs doesn't help.** Adding more headroom doesn't
  fix invalid pointers across module boundaries.
- **A 200ms sleep before the return "fixes" it.** The crash window is
  the gRPC closure-list manipulation that happens after the handler
  returns; staggering returns far enough apart in time means only one
  cross-boundary closure is in flight at a time.
- **The existing `if(NOT WIN32)` guards** on `test_grpc_extension_ui_service`
  and `test_grpc_piconet_ui` (in `tests/CMakeLists.txt`) describe
  the same symptom from the test-fixture side: when the test fixture
  hosts a service whose generated class is in
  `beebium_extension_ui_proto.dll`, fixture setup segfaults. The
  CMake note speculates "MSVC rough edge with `dllexport_decl` +
  gRPC service stubs", which is the same upstream bug.

## What was investigated before the issue tracker hit

The investigation that led here is preserved below for completeness.

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
status, server keeps running. The second handler logs all four
steps, then the server crashes after returning but before the next
emulation tick. That fits the closure-list corruption narrative
exactly — the second completion's gRPC closure machinery touches the
list in a state corrupted by the first.

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

**Co-locate `ExtensionUiServiceImpl` in the same Windows DLL as the
generated `Beebium_ExtensionUiService::Service` base class.** That
removes the cross-DLL boundary the gRPC closure code stumbles over.

The cleanest shape: move `ExtensionUiServiceImpl` out of
`beebium_service`'s header-only form and into a translation unit that
compiles into `beebium_extension_ui_proto` (or a sibling shared
library that depends on it). The class then lives next to its base
class in the same module.

Concrete steps:

1. **Reproduce on Windows with the working theory.** A minimal
   experiment: compile `ExtensionUiServiceImpl` into the proto DLL
   and re-run the headless Python repro. If the crash disappears,
   the diagnosis is confirmed.
2. **Make the build change permanent.** Either:
   - Convert `ExtensionUiServiceImpl` to a non-inline class with its
     definition in a `.cpp` linked into `beebium_extension_ui_proto`, or
   - Introduce a new shared library `beebium_extension_ui_service`
     that contains the impl and links to `beebium_extension_ui_proto`.

   The first is simpler; the second separates "wire types" from
   "service handler". Either is fine — pick whichever has the
   smallest blast radius.
3. **Restore the gRPC-fixture tests on Windows.** The existing
   `if(NOT WIN32)` guards on `test_grpc_extension_ui_service` and
   `test_grpc_piconet_ui` exist because of the same root cause. Once
   the impl is co-located with the base, those fixtures should
   instantiate cleanly on Windows. Lift the guards as a follow-up
   step in the same branch and confirm the suites pass on Slioch.
4. **Manual end-to-end on Slioch.** Reconnect the macOS frontend to
   the rebuilt server and walk the Network sidebar — the original
   piconet device-path-editor verification we couldn't complete the
   first time.
5. **File a comment on the upstream gRPC issue** with our concrete
   repro and confirmation that co-location works around it. Link
   back to this doc.

## What's NOT in scope on this branch

- Migrating to gRPC's async / callback API. The first revision of
  this doc proposed it as a generic mitigation; with the actual root
  cause known, it's heavier than needed and orthogonal to the bug.
- Reorganising the other gRPC services (`VideoService`,
  `AudioService`, etc.). They're unaffected because their proto
  stubs are in the same static library as their impls.
- Refactoring the `ExtensionUi` extension-side abstraction. Its
  contract (`build_view`, `handle_event`, `mark_dirty`) is unchanged;
  only the gRPC plumbing under it changes.

## Open questions

- Are there second-order effects from compiling `ExtensionUiServiceImpl`
  into `beebium_extension_ui_proto`? It would make the proto DLL
  depend on `beebium_extension_api` (for `ExtensionUi` etc.), which
  is currently the dependency direction. Need to check for cycles.
- Does the same DLL-boundary issue affect any future gRPC service
  whose stubs we'd want plugin-shared? If so, this branch should
  document the pattern as a general rule.
- Does the upstream gRPC issue have any forward progress between the
  pinned vcpkg version and current master? Worth checking before we
  commit to the co-location workaround as permanent.

## References

- **Upstream gRPC issue:**
  [grpc/grpc#39198](https://github.com/grpc/grpc/issues/39198) — the
  matching upstream report of cross-DLL service-implementation
  access violations.
- `tests/CMakeLists.txt` — the existing `if(NOT WIN32)` guards on
  the gRPC fixture-based test suites describe the same root cause
  ("MSVC rough edge with `dllexport_decl` + gRPC service stubs"),
  in a different observation window. These guards become removable
  once the co-location fix lands.
- `src/core/extension-api/CMakeLists.txt` — `beebium_extension_ui_proto`
  is declared `SHARED` here (line 85). The relevant constraint to
  preserve is "one copy of proto descriptors and class definitions
  shared between server / plugin DLLs".
- `src/service/CMakeLists.txt` — `beebium_service` is `STATIC`. The
  current `ExtensionUiServiceImpl` lives header-only in this lib;
  the fix moves it.
- `src/service/include/beebium/service/ExtensionUiService.hpp` —
  current header-only impl. The migration target.
- `src/service/include/beebium/service/Server.hpp` — gRPC
  `ServerBuilder` setup; no changes expected here.
- macOS frontend: `clients/macos/Beebium/Beebium/SidebarModeContent.swift`
  opens both extension panels concurrently when the Network tab
  appears, which is what triggers the production-side repro.
