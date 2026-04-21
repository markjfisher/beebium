# Win32 Serial I/O Capability Gap (Piconet on Windows)

Beebium has no Windows implementation of its `SerialPort` abstraction, which means the Piconet Econet transport extension does not build on Windows. This document captures the gap, its current consequences for the Extension UI framework's build configuration, and what closing the gap would entail.

Status: Capability gap. Not committed to implementation.

---

## The Gap

`src/extensions/piconet/include/beebium/econet/piconet/SerialPort.hpp` defines an abstract base for serial-port I/O. The only concrete implementation is `PosixSerialPort` (`src/extensions/piconet/include/beebium/econet/piconet/PosixSerialPort.hpp` + `.../src/PosixSerialPort.cpp`), which uses `open(2)`, `select(2)`, `read(2)`, `write(2)`, `termios(3)`, `fcntl(2)`, and `stat(2)`. All POSIX-only.

The piconet extension's `CMakeLists.txt` consequently skips itself on Windows:

```cmake
if(WIN32)
    message(STATUS "Skipping piconet extension on Windows (no Win32SerialPort)")
    return()
endif()
```

Result: on Windows, `--piconet device_path=...` produces a "no such extension" error from the CLI; the binary supports AUN over UDP and the test backend, but not real Econet hardware via the Piconet adapter.

The gap predates the Extension UI framework branch. It has been a known limitation since the Piconet extension first shipped on the prior branch.

## Why this affects the Extension UI framework's build configuration

The Extension UI framework introduced a small new shared library, `beebium_extension_ui_proto`, holding the generated proto/gRPC code for `extension_ui.proto`. The motivation for SHARED rather than STATIC is to keep exactly one copy of the proto descriptors and class definitions in the process — when an out-of-tree plugin loaded via `dlopen` (currently just Piconet) also consumes these types, two copies of the same `.proto` registered into protobuf's global descriptor pool can abort with "File already exists in database".

On Windows the trade-off changes:

* No `dlopen`'d plugins exist (because Piconet, the only POSIX plugin today, doesn't build).
* MSVC requires every symbol exported from a DLL to be marked `__declspec(dllexport)` at the source level. Protobuf-generated code has no such annotations. CMake's `WINDOWS_EXPORT_ALL_SYMBOLS` covers function symbols but not the `_DefaultTypeInternal` data globals that protobuf emits per message type. Building `beebium_extension_ui_proto` as a DLL on Windows leaves the import `.lib` empty / absent, and downstream consumers fail to link with `LNK1181: cannot open input file 'beebium_extension_ui_proto.lib'`. Even if the symbols were exported, consumers also need `__declspec(dllimport)` annotations to use the data globals, which the proto headers don't have.

The current build configuration therefore makes `beebium_extension_ui_proto` SHARED on POSIX (where the dlopen'd Piconet plugin makes a single-copy guarantee load-bearing) and STATIC on Windows (where there is no second consumer to share with, so STATIC sidesteps the MSVC export problems entirely). See the inline rationale at the top of `add_library(beebium_extension_ui_proto ...)` in `src/core/extension-api/CMakeLists.txt`.

This is a contingent choice. It assumes that Piconet remains Windows-unsupported. **When Win32SerialPort lands, this CMake split must be revisited** — see "Closing the gap" below.

## Closing the gap

Two pieces of work, in order:

### 1. Win32SerialPort

Implement `Win32SerialPort : public SerialPort` using the Windows comm-port API:

* `CreateFile` to open `COMn` or `\\.\COMnn` paths.
* `SetCommState` + `DCB` to configure 115200 8N1, no parity, no flow control.
* `SetCommTimeouts` to match `PosixSerialPort`'s non-blocking-with-timeout read semantics.
* `ReadFile` / `WriteFile` with `OVERLAPPED` for non-blocking operation, plus `WaitForSingleObject` with a 100 ms timeout to match the existing reader-loop cadence.
* `CloseHandle` for shutdown.
* Hot-unplug detection: `WAIT_OBJECT_0` with `ERROR_BAD_COMMAND` / `ERROR_OPERATION_ABORTED` from `GetOverlappedResult` indicates the device went away. Equivalent to the read-error path in `PosixSerialPort`.
* For periodic device-existence checks (the equivalent of `PosixSerialPort`'s `stat()` poll), use `QueryDosDevice(L"COMn", ...)` or enumerate `SetupDiGetClassDevs(GUID_DEVINTERFACE_COMPORT, ...)` and look for the path.

Live alongside `PosixSerialPort.cpp` in `src/extensions/piconet/src/`. The piconet extension's `CMakeLists.txt` `if(WIN32) return()` early-out becomes a per-platform source selection (build `Win32SerialPort.cpp` on Windows, `PosixSerialPort.cpp` on POSIX). The `SerialPort.hpp` interface stays unchanged.

Estimate: ~300 LOC + a Windows-specific test fake (the existing `FakePiconetDeviceOnPty` is POSIX-only — needs an equivalent built on Windows named pipes or a different mechanism entirely).

### 2. Restore SHARED on Windows for `beebium_extension_ui_proto`

Once a Windows piconet plugin can be loaded at runtime, the duplicate-descriptor concern returns. The proper Windows-friendly solution is protoc's `dllexport_decl` option:

1. Modify the protoc invocation in `src/core/extension-api/CMakeLists.txt`:
   ```cmake
   COMMAND ${PROTOC_EXECUTABLE}
       --cpp_out=dllexport_decl=BEEBIUM_EXT_UI_PROTO_API:${EXT_UI_PROTO_OUT_DIR}
       ...
   ```
   This adds `BEEBIUM_EXT_UI_PROTO_API` annotations to every generated message class.

2. Use CMake's `generate_export_header()` to produce a small header defining `BEEBIUM_EXT_UI_PROTO_API` as `__declspec(dllexport)` when the DLL is being built and `__declspec(dllimport)` when consumed (with empty defines on POSIX).

3. Force-include the export header into the generated `.pb.h` via `-include` (GCC/Clang) / `/FI` (MSVC) compile options on every target that consumes the proto headers. Or post-process the generated `.pb.h` to add a `#include "beebium_ext_ui_proto_export.h"` line at the top.

4. Revert the `if(WIN32) STATIC else() SHARED endif()` split to unconditional SHARED.

Estimate: ~50 LOC of CMake + small export header + careful verification across all four platforms (POSIX builds must not regress; Windows builds must successfully resolve the data globals through the new dllimport markings).

This work should land alongside Win32SerialPort, not before — there is no benefit in carrying the dllexport_decl complexity while no Windows plugins exist.

## Scope note

Both pieces above are deliberately separate from the Extension UI framework branch (`extension-ui-framework`). The Extension UI framework's job is to ship the framework itself; Win32 piconet support is a Beebium-on-Windows capability question that has its own verification surface (manual hardware testing on Windows with a real Piconet adapter, COM port enumeration behaviours, the device-loss edge cases that `PosixSerialPort` handles via `stat()` but Windows would handle differently). Bundling them would muddy both diffs.
