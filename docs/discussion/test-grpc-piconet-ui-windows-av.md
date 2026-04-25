# `test_grpc_piconet_ui` AV on Windows — investigation log

Status: **open, root cause unknown**. The test reliably AVs on Windows
on the `grpc-windows-streaming-race` branch as of commit `05b81f4`.
This document captures every diagnostic step taken on 2026-04-25, what
each one ruled in or out, and the hypotheses still in play. It exists
so the next person looking at this (or future-me looking at a similar
ghost) does not re-run the same dead ends.

---

## Symptom

`tests/test_grpc_piconet_ui.exe` on Windows fails the
`Piconet end-to-end: SubscribeView -> EditorCommit -> reopen -> next View`
test case with:

```
test cases:  1 |  0 passed | 1 failed
assertions: 16 | 15 passed | 1 failed
```

15 assertions pass, then a fatal SIGSEGV. Catch2's signal handler
attributes the failure to the test's first source line. The same test
passes on macOS.

The companion test `test_grpc_piconet_service` AVs identically. Both
were previously gated with `if(NOT WIN32)` for this reason; the gates
were lifted in commit `8f586a0` to expose the bug.

The original branch was started on the *production* manifestation of
this AV: macOS frontend connects to a Windows server, switches to the
Network panel, server crashes ~1s later. That bug and the test bug
share the same crash signature (call dispatched to
`0x7ff?_00000000`) and very likely the same root cause.

## Crash signature

**Always the same shape on Windows**, regardless of whether the std::function
involved is `serial_factory_` (a `std::function<unique_ptr<SerialPort>(string const&)>`
holding a free-function pointer) or `on_async_state_change_`
(a `std::function<void()>` holding a stateless-`this`-capturing lambda):

```
RIP at fault       = 0x00007ff?_00000000   (high 16 bits = piconet.dll's load base)
Faulting address   = 0x00007ff?_00000000   (unmapped page)
Stack frame at AV  = 1 deep (the bad call target itself)
```

The pattern `<piconet-base-high-16> 00 00 00 00 00 00` is the
fingerprint. It looks like a 64-bit pointer with the bottom 48 bits
zeroed, but no straightforward "wrote 0 to bytes 0..3 of slot"
hypothesis survives the data we collected (see below).

## Reproduction

On Slioch (Windows 10 22H2, MSVC 19.44, vcpkg gRPC):

```powershell
cd C:\Users\rjs\dev\beebium\build
cmake --build . --config RelWithDebInfo --target test_grpc_piconet_ui
cd tests\RelWithDebInfo
.\test_grpc_piconet_ui.exe --reporter compact
```

100% reproducible at full speed, RelWithDebInfo.

## Investigation log

### Step 1 — Initial wrong diagnosis: cross-DLL gRPC state

First hypothesis was [gRPC #39198](https://github.com/grpc/grpc/issues/39198) —
gRPC's per-module thread-local state when statically linked into multiple
DLLs in the same process. The bug looked like a perfect match: the same
test passes on POSIX, the test EXE has gRPC linked, and `piconet.dll`
also linked gRPC (because it had `PiconetService` compiled in for
production). Two static gRPC instances → mismatched ExecCtx →
crash in `grpc_closure_list_append`.

**Why we believed it**: the very first cdb stack trace caught the AV
inside `CallOpSet::ContinueFillOpsAfterInterception` reaching
`grpc_closure_list_append`, with frames split across the EXE and
`beebium_extension_ui_proto.dll`. We acted on this by:

1. Splitting `.grpc.pb.cc` and `ExtensionUiServiceImpl` out of
   `beebium_extension_ui_proto.dll` (SHARED) into a new
   `beebium_extension_ui_service` STATIC library linked only into
   the server EXE / test EXE. Commit `8f586a0`.
2. Decoupling class-level `BEEBIUM_EXT_API` from MSVC dllimport-on-class
   so consumer DLLs that don't link `beebium_extension_api` could still
   compile (`BEEBIUM_EXT_TYPE_VISIBLE` macro). Commit `0261b1f`.
3. Removing gRPC linkage from `piconet.dll` entirely — drop
   `PiconetService.cpp`, drop `grpc_services()` override, leave the
   plugin DLL with just the extension class and UI. Commit `4f9284b`.

**Result**: `test_grpc_extension_ui_service` (which doesn't load
piconet.dll) **does** pass after step 1. So the cross-DLL gRPC issue
**is real** for that test, and the architectural cleanup was
worthwhile — the `beebium_extension_ui_service` static-library split
should stay. But `test_grpc_piconet_ui` still AVs after all three
commits land, with `piconet.dll` carrying no gRPC at all.

So the cross-DLL gRPC theory is **not** the cause of the
`test_grpc_piconet_ui` AV. It is *a* real bug we fixed along the way,
but it's a different bug from the one this document is about.

### Step 2 — Heisenbug discovery

The fundamental property of this bug, established early:

- Adding `fprintf(stderr, ...)` calls inside `PiconetBackend` makes the
  bug **disappear**. Test passes 44/44.
- Removing the diagnostic prints brings the bug back, deterministically.
- Single-stepping the suspected call instruction under cdb makes the
  bug **disappear** for that call. Test continues, eventually crashes
  somewhere else.
- Enabling Application Verifier with Full PageHeap on the test EXE
  makes the bug **disappear**. Test passes 44/44.

This pattern means the bug is sensitive to:
- heap layout (PageHeap puts every allocation on its own guard page,
  shifting addresses dramatically), and/or
- timing (single-stepping introduces ms-scale delays between
  instructions, which lets concurrent threads make different progress),
  and/or
- compiler codegen near the call site (`fprintf` calls force
  spill/reload of registers around them).

Crucially, **PageHeap full does not** *catch* the corruption with a
fault. PageHeap would fault immediately on a heap overflow or
use-after-free against a separate allocation. It hides the crash by
relayout, not by detecting it. So the bug is **not** a classic
heap-overflow or use-after-free.

### Step 3 — Hardware data watchpoints

Set per-thread (`ba w 4`) and process-wide (`~* ba w 4`) hardware
watchpoints on the std::function's storage at the offsets that the
disassembled call sequence reads:

- `+0x48` (function pointer slot of `serial_factory_`)
- `+0x78` (`_Mybase` pointer of `serial_factory_`)
- `+0x108` (vtable slot of `on_async_state_change_`)
- `+0x140` (`_Mybase` pointer of `on_async_state_change_`)

**Result**: **none of the watchpoints ever fire** before the AV. The
expected slots are never written through normal CPU writes between
the breakpoint at `process_pending_reopen` entry and the AV.

We verified the watchpoints were registered (`bl` showed them with
`0:****` thread-broadcast scope), but never observed a fire.

This rules out:
- A naïve "wild write zeroes the function pointer" theory — there is no
  such write.
- A buffer-overflow from an adjacent allocation that lands precisely
  on the slot — same reason.

It's *consistent* with:
- Writes that do not go through standard memory stores (DMA, kernel,
  some SIMD instructions in some configurations) — but we have no
  positive evidence of any of these.
- The bytes at the slot **do not actually change** at all — see Step 4.

### Step 4 — Direct byte inspection at AV time

Captured PB's address into a cdb pseudo-register at
`process_pending_reopen` entry, ran to AV, and dumped 256 bytes
starting at PB+0x40. **The bytes are identical at AV time and at
function entry** — vtable, function pointer, _Mybase all correct.
Multiple runs, same result.

We additionally verified at AV:
- `PB still at <addr>` — heap allocation alive.
- `vtable still at <piconet-vtable>` — std::function impl pointer correct.
- `[vtable+0x10]` (= `_Do_call` slot) = correct piconet `_Do_call` address.

So the entire chain that the dispatch reads from memory is correct at
the moment of the AV. Yet the dispatch lands at
`0x7ff?_00000000`. The discrepancy between "memory contents are
correct" and "dispatch goes wrong" is the central mystery.

### Step 5 — Single-step the dispatch instruction

Disassembled `process_pending_reopen` and `notify_state_changed`. Both
end in essentially the same sequence (the std::function dispatch is
inlined):

```asm
mov  rcx, [rbx+offset]      ; rcx = _Mybase
test rcx, rcx
je   <skip>
mov  rax, [rcx]             ; rax = vtable
... arg setup ...
call qword ptr [rax+10h]    ; or: jmp qword ptr [rax+10h]  (tail call)
```

Set `bp /1` at the `call`/`jmp` instruction. When hit:

- `rax` = correct vtable address.
- `[rax+0x10]` = correct `_Do_call` address.
- `t` step lands at `_Do_call`'s first instruction — dispatch works.

So the *instructions* dispatch correctly when stepped. The crash only
happens at full speed.

### Step 6 — Trace the call sequence with conditional breakpoints

`bp piconet!<fn> ".printf ...; gc"` on every PiconetBackend method,
running the test:

```
reader_loop T:0x24e0       (reader thread, started in ctor)
pp_reopen entry T:0x2bd8    (test thread)
tear_down T:0x2bd8          (test thread)
notify entry T:0x24e0       (READER thread, called from reader_loop's
                             close-error branch)
AV at IP=0x7ff9_00000000 T:0x2bd8 RAX=<heap> RCX=<non-canonical>
```

Critical observations:
- `install_open_serial` and `install_failed_serial` are **never reached**.
- The AV is on the test thread (`T:0x2bd8`).
- Reader thread *did* enter `notify_state_changed` (its own
  `on_async_state_change_` lambda call). That call **succeeded** — no
  crash from the reader thread.
- The test thread reaches the section between `tear_down`'s return
  and `install_*_serial`. Within that section the AV happens.

### Step 7 — Source-level fprintf bisection

Added fine-grained `fprintf` markers inside `process_pending_reopen`:

```cpp
[diag-A] before tear_down
[diag-B] after tear_down
[diag-C] after config_= assign       // string assignment to config_.device_path
[diag-C2] sf bytes={... vtable, fn-ptr ..., _Mybase ...}
auto fresh = serial_factory_(*pending);
[diag-D] after serial_factory_ call
```

Output at full speed:

```
[diag-A] before tear_down
[diag-B] after tear_down
[diag-C] after config_= assign
[diag-C2] sf bytes={00007ff962fb3ad8 00007ff962f73d00 ...
                    ... ... ... ... 000001b46dc4e0c0} addr=000001B46DC4E0C0
                                    ^ _Mybase, points to its own slot
SIGSEGV
```

So:
- We get past `tear_down`.
- We get past the locked `config_.device_path = *pending;` assignment.
- The std::function bytes *immediately before* the call are correct.
- The call to `serial_factory_(*pending)` is what AVs.

This confirms the AV is in the call dispatch itself and that the
std::function's storage is structurally intact at the moment of the
call.

### Step 8 — UI-snapshot mutex (data race fix attempt)

Before all the above, the most plausible looking lead was a data race
between `PiconetUi::build_view` (running on the gRPC SubscribeView
thread) reading `config_.device_path`, `serial_`,
`open_error_message_`, and `process_pending_reopen` (running on the
test/emulation thread) writing those same fields. Concurrent access
to a `std::string` is UB and could plausibly explain the crash.

Implemented (commit `05b81f4`):
- New `PiconetBackend::UiSnapshot` struct + `ui_snapshot()` accessor,
  which returns a copy of the racy fields under a brief
  `ui_mutex_`.
- `PiconetUi::build_view` calls `ui_snapshot()` once at the top and
  reads from the resulting struct.
- All write sites (`process_pending_reopen`, `install_open_serial`,
  `install_failed_serial`, the ctor's open-failure branch) take
  `ui_mutex_` around the writes.

**Result**: The race **was real** (concurrent access definitely happens
without the lock) but fixing it does **not** fix the AV. The test
still fails with the same crash signature.

The mutex-based fix is the right thing to keep — silently-UB
concurrent string access is a latent bug — but it isn't the one
biting us.

## Confirmed facts

After all the above, the things we are confident about:

1. The AV is at `serial_factory_(*pending)` inside
   `process_pending_reopen` (commit `05b81f4` code path).
2. The fault occurs on the test/emulation thread.
3. The faulting address is always `<piconet-base-high-16>00000000`.
4. The std::function's stored state (vtable, function pointer, _Mybase)
   is bit-for-bit correct at the moment of the call.
5. The vtable's `+0x10` slot in piconet.dll's RDATA holds the correct
   `_Do_call` address at the moment of the call.
6. The call dispatch executed instruction-by-instruction (under cdb
   `t`) goes to the correct `_Do_call`.
7. Every memory-corruption-shaped explanation has been
   ruled out by hardware watchpoints + AppVerifier + direct byte
   inspection.
8. The bug is sensitive to heap layout and/or timing — anything that
   perturbs them hides it.

## Hypotheses ruled out

- **Cross-DLL gRPC TLS state mismatch.** Removing gRPC from
  `piconet.dll` entirely doesn't fix it.
- **Heap overflow corrupting the std::function's bytes.** PageHeap full
  doesn't catch it; bytes at the slot are unchanged at AV time.
- **Use-after-free of PiconetBackend.** PB is alive at AV time
  (verified by dq), unique_ptr ownership is correct, no destructor
  ran early.
- **`build_view` ↔ `process_pending_reopen` data race on
  `config_.device_path`.** Mutex-protected snapshot doesn't fix it.
- **Wrong `_Mybase` pointer.** It's correct.
- **Wrong vtable.** It's correct.
- **MSVC's std::function dispatch path emitting wrong code.**
  Single-stepped dispatch lands at the right `_Do_call`.

## Hypotheses still in play

In rough order of likelihood:

1. **Stack corruption near the call.** The test thread's stack might
   be getting written by another thread, or by an unwind-handler bug,
   such that when the call instruction loads RAX, RCX, RDX, R8 from
   the stack-frame setup, one of them is wrong even though the *source
   memory* it was loaded from in step 7 looked right. Hardware data
   watchpoints on stack addresses are awkward (the address moves
   between calls). Not yet attempted.

2. **MSVC codegen quirk specific to the optimized layout with the
   `lock_guard` block.** The disassembly we have is from a build
   *before* the mutex landed; the layout has shifted since, and we
   haven't disassembled the *current* `process_pending_reopen` in
   detail to see how the optimizer arranged things. A spilled register
   that gets reloaded incorrectly across the mutex unlock would fit
   the pattern.

3. **Instruction-pipeline / store-to-load forwarding bug in the
   specific Intel CPU on Slioch.** Exotic, but the
   "memory contents are correct yet dispatch is wrong" pattern is
   what spectre-class issues look like at the user level. Would
   probably reproduce on other CPUs if it were generic.

4. **Catch2's signal-handling fixture interfering with the test
   thread's stack.** Catch2 installs SEH translators; if the AV is
   actually at `install_open_serial` or later and Catch2's exception
   transport is what we're seeing, the "before-call diag prints OK,
   no after-call diag print" pattern would be a misread of what's
   actually happening. The current understanding is that the `[diag-D]`
   absence proves the call AVs, but Catch2's SEH might be subtler.

## Tools and techniques (reusable)

### Application Verifier — Full PageHeap

```powershell
# Run as Administrator
appverif -enable Heaps -for test_grpc_piconet_ui.exe
appverif -query -for test_grpc_piconet_ui.exe   # verify
# ... reproduce ...
appverif -disable * -for test_grpc_piconet_ui.exe
```

Registry footprint at
`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\<exe>`:
`GlobalFlag=0x100`, `PageHeapFlags=0x3`,
`VerifierDlls=vrfcore.dll`. AppVerifier ships with Windows 10
(`C:\Windows\System32\appverif.exe`); the standalone `gflags.exe`
isn't part of recent WinDbg packages.

### cdb scripted breakpoints

The new WinDbg from the Microsoft Store installs at
`C:\Program Files\WindowsApps\Microsoft.WinDbg_*_x64*\amd64\cdb.exe`,
but its ACL prevents direct execution. A robocopy of the `amd64`
subdirectory to a normal user path (we used `C:\Users\rjs\windbg\`)
gives a working command-line debugger.

Pseudo-registers (`$t0`-`$t9`) survive `g`, so the standard pattern
for "save a value at one breakpoint, inspect it at the next" is:

```
bp piconet!<fn>
g
r $t0 = @rcx                # save PiconetBackend* into $t0
.printf "PB=%p\n", @$t0
g
.echo at next event
dq @$t0+0x40 L10            # bytes are still PB-relative
```

Shell-quoting `$t0` in a heredoc is painful; write the cdb script to
a file and invoke `cdb -cf <file>` instead:

```bash
scp /tmp/script.txt rjs@Slioch.local:C:/Users/rjs/script.txt
ssh rjs@Slioch.local '
cd C:/Users/rjs/dev/beebium/build/tests/RelWithDebInfo
& C:/Users/rjs/windbg/cdb.exe -cf C:/Users/rjs/script.txt .\test.exe 2>&1 |
  Out-File C:/Users/rjs/script.log
'
```

Process-wide hardware watchpoints (apply to all current threads):

```
~* ba w 8 @@(@$t0+0x140)
```

Caveat: only existing threads at the time of `~* ba` are armed. New
threads created later are not. cdb has 4 hardware breakpoints per
thread (DR0-DR3); large numbers of threads × multiple watchpoints can
hit that limit silently.

### Source-line breakpoints

```
bp `PiconetBackend.cpp:530`  ".printf ...; gc"
```

These rely on the PDB having a path that matches what we type. If the
build was done in a different working directory, the source path in
the PDB might not match `PiconetBackend.cpp` and the breakpoint
silently fails to bind. Function-symbol breakpoints
(`bp piconet!fn_name`) are more reliable.

### Conditional breakpoint trace pattern

```
bp piconet!fn ".printf \"fn entry T:%p\\n\", @$tid; gc"
```

`gc` continues past the bp. The bp stays armed for repeated hits
unless `/1` is used. `printf` arguments must be expressions cdb's
`.printf` understands — `poi(addr) ? 1 : 0` is **not** valid; it
parses as numeric subtraction. Stick to `%p` of registers or
`@$tid`-style values.

### Diagnostic-print bisection

When watchpoints don't work and the bug is layout-sensitive,
`fprintf` markers at every "interesting" line in the suspect function
are a blunt but effective tool. Print *before* and *after* each
suspected statement; the gap between the last print and the AV
identifies the line. Be aware that adding the prints often makes the
bug disappear (Heisenbug) — if it does, that's data: the bug is
layout-/timing-sensitive.

## Approaches not yet tried

In priority order if someone picks this up:

1. **Disassemble the current `process_pending_reopen`** (with the
   mutex code) on Slioch to see the actual instruction sequence and
   register allocation around the `serial_factory_` call. We have only
   pre-mutex disassemblies on file. If the optimizer is doing something
   surprising near the unlock or the load of the std::function members,
   it'll show up here.

2. **MSVC AddressSanitizer (`/fsanitize=address`).** ASan instruments
   loads and stores at compile time, separately from the heap
   allocator. Unlike PageHeap, it should still trigger on
   bad accesses regardless of relayout. Possibly defeats the
   Heisenbug; cost is one rebuild.

3. **Optimization off.** Build with `/Od` (or RelWithDebInfo overridden
   to `-O0`). If the bug disappears it's an optimizer issue; if it
   persists, the codegen is innocent.

4. **Reproduce on a different Windows machine / CPU.** Slioch is the
   only Windows box we test on. If the bug is microarchitectural this
   will reveal it.

5. **Run under `ttd.exe`** (Time Travel Debug — included in WinDbg).
   Records execution, then lets you step backwards from the AV. If the
   AV is real (not a Catch2 SEH artifact), TTD lets you walk
   instruction-by-instruction backwards from the bad RIP and observe
   exactly when it loaded.

6. **Switch `serial_factory_` from `std::function` to a raw function
   pointer typedef.** If the bug only manifests with
   std::function, this would dodge it for the production path
   (`&make_platform_serial` is a free function pointer). It would not
   be a fix — it would be a workaround whose mechanism we don't
   understand — but if it works, it ships the macOS-frontend feature
   while the deeper investigation continues. The two test harnesses
   that pass a lambda factory would need a small refactor.

## Workarounds that would unblock shipping

The user-facing goal of the branch is the macOS frontend's Piconet
device-path editor. The AV's *production* manifestation
(macOS client → Windows server → Network panel switch → server
crash) needs the same bug fixed before that ships.

If a definitive fix is not found:

- Returning the failing tests to `if(NOT WIN32)` un-blocks CI but
  leaves Windows users with a broken Network panel. **Not acceptable
  for the user-facing goal.**
- Removing `serial_factory_` as a `std::function` avoids the bug at
  the call site... or so we thought. **Tried in commit `a84e1a8` and
  reverted — it did not fix the bug.** Both `serial_factory_` and
  `on_async_state_change_` were converted to plain function pointer +
  userdata; the std::function dispatch was eliminated; the test still
  AVs at `0x7ff?_00000000` with the same crash signature. Conclusion:
  the bug is *not* in the std::function dispatch path. It survives
  even when the call lowers to a single register-indirect `call rax`
  through a raw function pointer member. See "Hypotheses that
  survived the workaround" below.
- Routing all reopen mutations through a single thread (instead of
  the current "test thread calls receive_frame which calls
  process_pending_reopen") would change the threading model enough
  that the bug might evaporate, but we don't have a clean reason to
  believe this is the fix vs. a heap-relayout coincidence.

## Hypotheses that survived the workaround attempt

After commit `a84e1a8` was reverted, the working hypothesis space
narrowed substantially. The bug survives:

- replacement of `std::function<R(Args...)>` with raw `R (*)(Args...)`
  function pointers,
- removal of `<functional>` include,
- removal of all type-erased callable storage from `PiconetBackend`.

So the corrupting agent is not anything specific to std::function's
SBO storage, vtable dispatch, or `_Mybase`/`_Do_call` indirection.
The remaining live hypotheses are:

1. **The `serial_factory_` member's 8-byte slot itself is being
   clobbered**, regardless of its type. Hardware data watchpoints
   on its address still don't fire (per Step 3), but neither do they
   for the std::function case. Whatever is doing the write is
   invisible to DR-register watchpoints.
2. **MSVC codegen around the call site is producing a wrong
   target,** independent of what the source object is. A
   spilled-and-reloaded copy of `serial_factory_` could be
   construct-time-correct yet load-time-corrupt if the spill slot is
   on the test thread's stack and a different thread overwrites that
   stack slot. This requires stack corruption from another thread,
   which is not normally possible — unless an SEH unwind handler or a
   `setjmp`/`longjmp` somewhere is mucking with it.
3. **A microarchitectural / Windows-kernel bug in the Slioch
   environment.** Same speculation as before; would need a different
   Windows machine to confirm.

The TTD (Time Travel Debugging) approach in "Approaches not yet
tried" item 5 is now the most promising next step, because it lets
us walk *backwards* from the bad RIP to the precise instruction that
loaded the bad target — bypassing the question of whether the source
memory was corrupted or the load was miscoded.

## A note on the post-revert build state

After reverting `a84e1a8`, a **clean rebuild** of
`test_grpc_piconet_ui` on Slioch produced an EXE that crashes at
process startup with no output at all (Catch2 banner never prints,
exit code `0xC0000005`). The cdb stack at the AV showed gRPC's
`grpc_core::PerCpuShardingHelper::state_` thread-local dynamic
initializer + 0x26, called from `__dyn_tls_init`. The crash IP was
the same canonical-high `0x7ff?_00000000` pattern.

This is **probably the same underlying bug** manifesting earlier in
the lifetime of the process, exposed by whatever heap layout the
clean rebuild happens to produce. It's another data point that the
bug is layout-sensitive and broadly hits anything that goes through
a function-pointer-table dispatch in this binary on Windows. Not
specific to PiconetBackend at all.

## Related upstream issues

- [gRPC #39198](https://github.com/grpc/grpc/issues/39198) — cross-DLL
  service-impl on Windows. Real, fixed by commit `8f586a0`. **Not** the
  bug this document tracks.
- [protobuf #15069](https://github.com/protocolbuffers/protobuf/issues/15069),
  [#18097](https://github.com/protocolbuffers/protobuf/issues/18097)
  — `protobuf::Map` lookup failures from non-deterministic absl hash
  seeds. Related family of "Windows-specific gRPC headache" but
  different surface.

## Files / commits relevant to this investigation

| Commit    | Summary                                                                  |
|-----------|--------------------------------------------------------------------------|
| `5e3a7f3` | Initial co-location of `ExtensionUiServiceImpl` (early wrong fix)        |
| `0261b1f` | `BEEBIUM_EXT_TYPE_VISIBLE` macro (decouple POSIX visibility from MSVC dllimport) |
| `8f586a0` | Split `.grpc.pb.cc` out of `beebium_extension_ui_proto.dll` (real fix for `test_grpc_extension_ui_service`) |
| `4f9284b` | Remove gRPC linkage from `piconet.dll` (didn't fix `test_grpc_piconet_ui`) |
| `05b81f4` | `PiconetBackend::UiSnapshot` mutex (real data-race fix; doesn't fix `test_grpc_piconet_ui`) |
| `a84e1a8` | Replace std::function with raw fn pointers (didn't fix; reverted in `37ffe2b`) |
| `37ffe2b` | Revert `a84e1a8` (the workaround did not fix the bug)                    |

The test that exposes this bug is `tests/test_grpc_piconet_ui.cpp`,
specifically the `Piconet end-to-end: SubscribeView -> EditorCommit ->
reopen -> next View` test case starting at line 181.
