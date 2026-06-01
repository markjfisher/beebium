# Beebium Linux Client

Native Linux frontend for the Beebium BBC Micro emulator.

The Linux client now provides:

- a Qt 6 desktop application with a dockable window layout
- a central BBC display with OpenGL video presentation
- host/port connection controls, disconnect, and local-server ownership status
- gRPC video, audio, keyboard, system, indicator, disc, sideways, and serial integration
- local machine/config profile management and local server launch from the UI
- persisted window layout, UI state, and per-profile configuration data
- configurable display aspect and presentation controls

## Requirements

- Qt 6.5+ with `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets`, and `Multimedia`
- KDDockWidgets for the full dockable shell experience (the client can fall back to standard Qt docks if unavailable)
- gRPC and Protobuf development packages
- a built Beebium server such as `beebium-model-b`

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

### Connect to an existing server

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

### Use a dedicated config folder

The Linux client can persist its UI state, dock layout, generated presets, and config profiles under a chosen folder:

```bash
./build-release/clients/linux/beebium-linux/beebium-linux \
  --config-folder=/home/markf/dev/bbc/beebium-linux-profile \
  --host 127.0.0.1 \
  --port 48875
```

Files created there include:

- `beebium-linux.ini` for UI settings
- `beebium-linux-layout.json` for dock layout
- `beebium-linux-configs.json` for saved machine configs
- `presets/*.preset.beebium` for generated local-launch presets
- `beebium-local-server.log` if the UI detaches and leaves a local server running on exit

### Start a local configured machine from the UI

You do not need to pre-start a server for this path.

1. Launch the Linux client.
2. Open `Machine > Configs...` to edit or create saved machine configs.
3. Select a config from `Machine > <config name>`.
4. The UI writes a preset, launches the matching local `beebium-*` server, and reconnects automatically.

Notes:

- If the UI already owns a local server, selecting another config replaces it automatically.
- If you are connected to an external/remote server, the UI prompts before disconnecting and starting a local one.
- If a detached local server is already running and the UI does not own it, the UI will not replace it automatically; you must stop that server first.

## Current Scope

The Linux client is now beyond the initial proof-of-concept stage, but it is still not at full parity with the macOS client.

Implemented:

- dockable shell with central display and persistent layout
- connection, status, indicators, storage, serial, audio, and configuration summary panes
- menu-driven local machine config selection and launching
- config editor window with saved profiles and generated presets
- audio playback via Qt Multimedia with device selection and volume control
- OpenGL video display with configurable aspect and presentation options
- keyboard input for common non-text keys and printable characters
- serial defaults carried through config-driven local server launch

Keyboard notes:

- Click the emulator display to focus it before typing.
- Printable characters use the server's canonical character-to-key mapping.
- Common special keys are defined centrally in the Linux client keymap.
- `F12` is mapped to BBC Break.
- `F1`-`F10` map to BBC `f0`-`f9`.
- Return, Backspace, arrows, Home (`COPY`), Shift, Ctrl, Caps Lock, Escape, Tab, and Space are wired directly.
- Keyboard focus remains important: click the display before typing if you have interacted with other widgets.

Display notes:

- `View > Display Aspect` offers `Auto`, `4:3`, and `Square Pixels`.
- `View > Display Presentation` offers texture sampling and integer-scaling options.
- `clients/linux/docs/video_options.md` describes those presentation controls in more detail.

Configuration notes:

- `Machine > Configs...` edits saved machine profiles.
- `Machine > <config name>` selects and launches a local machine with that config.
- The connection panel shows whether the current local server is owned by the UI.
- `Keep local server running on exit` only applies when the UI owns the launched local server.

Still incomplete / follow-on work:

- full macOS feature parity
- debugger/disassembly/memory tooling
- richer extension and peripheral-specific UI
- automatic migration for arbitrary future incompatible dock layout schema changes
- broader built-in ROM catalog and model-aware config authoring polish

Those remain follow-on milestones for the Linux frontend.
