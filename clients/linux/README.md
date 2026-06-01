# Beebium Linux Client

Native Linux frontend for the Beebium BBC Micro emulator.

This initial implementation provides:

- a Qt 6 desktop application
- host/port connection controls
- gRPC video streaming from the emulator server
- gRPC audio streaming and playback
- OpenGL presentation of streamed BGRA frames
- basic system-info display for the connected machine
- basic keyboard input for interaction with the BBC

## Requirements

- Qt 6.5+ with `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets`, and `Multimedia`
- gRPC and Protobuf development packages
- a running Beebium server such as `beebium-model-b`

## Building

From the repository root:

```bash
cmake -S . -B build -DBEEBIUM_BUILD_LINUX_CLIENT=ON
cmake --build build --target beebium-linux
```

For an optimized release build of both the emulator server and the Linux UI:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBEEBIUM_BUILD_LINUX_CLIENT=ON
cmake --build build-release --target beebium-model-b beebium-linux
```

If you want optimized code but still want symbols for debugging/profiling:

```bash
cmake -S . -B build-relwithdebinfo -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBEEBIUM_BUILD_LINUX_CLIENT=ON
cmake --build build-relwithdebinfo --target beebium-model-b beebium-linux
```

Or from the Linux client directory itself:

```bash
cd clients/linux/beebium-linux
cmake -S . -B build
cmake --build build --target beebium-linux
```

From the client directory, a release-only UI build looks like:

```bash
cd clients/linux/beebium-linux
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target beebium-linux
```

If Qt 6 is not installed, the Linux client target is skipped automatically.

## Running

Start a Beebium server first:

```bash
./build/src/server/beebium-model-b
```

For the release build shown above:

```bash
./build-release/src/server/beebium-model-b
```

Then run the Linux client:

```bash
./build/clients/linux/beebium-linux/beebium-linux --host 127.0.0.1 --port 48875
```

For the release build shown above:

```bash
./build-release/clients/linux/beebium-linux/beebium-linux --host 127.0.0.1 --port 48875
```

## Current Scope

This is the first Linux GUI milestone from the implementation plan. It is intentionally narrower than the macOS client.

Implemented:

- build-system integration under `clients/linux`
- native window shell
- gRPC `VideoService` integration
- gRPC `SystemService` integration for machine summary
- OpenGL video display preserving the server-provided display geometry basics
- audio playback via Qt Multimedia
- keyboard input for common non-text keys and printable characters

Keyboard notes:

- Click the emulator display to focus it before typing.
- Printable characters use the server's canonical character-to-key mapping.
- Common special keys are defined centrally in the Linux client keymap.
- `F12` is mapped to BBC Break.
- `F1`-`F10` map to BBC `f0`-`f9`.
- Return, Backspace, arrows, Home (`COPY`), Shift, Ctrl, Caps Lock, Escape, Tab, and Space are wired directly.
- This is still an early implementation and does not yet match the full macOS keyboard mapping feature set.

Not yet implemented:

- local machine launch and preset management
- discovery
- extension UI rendering
- debugger, storage, and peripheral sidebars

Those remain follow-on milestones for the Linux frontend.
