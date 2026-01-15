# Beebium Command Line Interface

The Beebium emulator runs as a headless gRPC server. This document covers all command-line options for the server executables.

## Executables

| Executable | Machine | Description |
|------------|---------|-------------|
| `beebium-model-b` | BBC Model B | Original 32K BBC Micro with MOS 1.20 |
| `beebium-model-b-plus` | BBC Model B+ 64K | Enhanced 64K model with MOS 2.0 |

## Usage

```bash
beebium-model-b [options]
beebium-model-b-plus [options]
```

## Options

### ROM Configuration

| Option | Description |
|--------|-------------|
| `--mos <filepath>` | Path to MOS ROM (default: machine-specific) |
| `--rom <slot>:<filepath>` | Load ROM into sideways slot 0-15 |
| `--rom <slot>:` | Leave slot empty (overrides default) |
| `--rom-dir <dirpath>` | ROM directory (auto-detected if not specified) |

### Disc Configuration

| Option | Description |
|--------|-------------|
| `--floppy <drive>:<url>` | Load disc image into drive 0 or 1 |

The `--floppy` option accepts file paths or `file://` URLs. Paths are resolved to canonical absolute paths.

### Network

| Option | Default | Description |
|--------|---------|-------------|
| `--port <port>` | 48875 (0xBEEB) | gRPC server port |

Use `--port 0` to request dynamic port allocation. The server prints the allocated port to stdout:

```
Starting gRPC server...
Listening on port 54321
```

Scripts can parse the `Listening on port <N>` line to discover the allocated port.

If the port is already in use, the server exits with an error:
```
Error: Failed to bind to port 48875 (port may already be in use)
```

### Startup Control

| Option | Description |
|--------|-------------|
| `--wait` | Delay emulation start (mode auto-detected) |
| `--wait=cli` | Wait for RETURN keypress on stdin |
| `--wait=api` | Wait for `Run()` RPC from client |

The `--wait` option allows clients to connect and set up before emulation begins.

#### `--wait` (bare)

Auto-detects mode based on whether stdin is a TTY:
- **TTY detected**: Uses `cli` mode (waits for RETURN)
- **No TTY**: Uses `api` mode (waits for Run() RPC)

#### `--wait=cli`

Waits for the user to press RETURN on the console before starting emulation. The server prints:

```
Now press RETURN.
```

Useful for interactive debugging or ensuring the server is fully initialized before manual testing.

#### `--wait=api`

Pauses the machine immediately after the 6502 reset sequence completes (7 cycles). The CPU is at the first instruction (PC points to the reset vector address, typically $D9CD for MOS 1.20). The server prints:

```
Paused at first instruction (PC=$D9CD). Waiting for Run() RPC...
```

Call `DebuggerControl/Run` to start emulation. This mode is designed for:
- Automated testing (connect, configure, then start)
- Debugger frontends that need to set breakpoints before execution
- Differential testing (both emulators start from identical states)

### Startup Options (Keyboard Links)

The BBC Micro reads DIP switches (keyboard links) during reset to configure startup behavior.

| Option | Description |
|--------|-------------|
| `--screen-mode <0-7>` | Startup screen mode (default: 7) |
| `--auto-boot` | Reverse SHIFT-BREAK behavior (SHIFT-BREAK boots disc) |
| `--links <0-255>` | Raw startup options byte |

`--links` is mutually exclusive with `--screen-mode` and `--auto-boot`.

### Information

| Option | Description |
|--------|-------------|
| `--info` | Print machine information as JSON and exit |
| `--help` | Show usage information |

## Examples

```bash
# Basic usage with defaults
./beebium-model-b

# Load a game disc and auto-boot
./beebium-model-b --floppy 0:elite.ssd --auto-boot

# Replace BASIC with Forth
./beebium-model-b --rom 15:forth.rom

# Multiple ROMs
./beebium-model-b --rom 14:dfs.rom --rom 13:viewsheet.rom

# Remove default DFS on Model B+ (leave slot 11 empty)
./beebium-model-b-plus --rom 11:

# Dynamic port for testing
./beebium-model-b --port 0

# Wait for API control (automated testing)
./beebium-model-b --wait=api --port 0

# Start in Mode 0 instead of Mode 7
./beebium-model-b --screen-mode 0
```

## Machine Discovery

The `--info` flag outputs machine information as JSON:

```bash
./beebium-model-b --info
```

Output:
```json
{
  "executable": "beebium-model-b",
  "machine_type": "ModelB",
  "display_name": "BBC Model B",
  "version": "0.1.0",
  "default_mos_rom": "acorn-mos_1_20.rom",
  "default_language_rom": "bbc-basic_2.rom",
  "default_language_slot": 15
}
```

Model B+ also includes DFS information:
```json
{
  "default_dfs_rom": "acorn-dfs_2_26.rom",
  "default_dfs_slot": 11
}
```

## Environment Variables

| Variable | Description |
|----------|-------------|
| `BEEBIUM_NO_PACING` | Disable real-time pacing (run at maximum speed) |

## Server Lifecycle

1. **Startup**: Server loads ROMs, initializes machine, binds gRPC port
2. **Ready**: Prints "BBC Model B ready. Press Ctrl+C to stop."
3. **Wait** (if `--wait`): Blocks until condition met
4. **Running**: Main emulation loop at ~2MHz (paced) or maximum speed
5. **Shutdown**: On SIGINT/SIGTERM, notifies clients via `WatchServerStatus`, then exits

Clients can subscribe to `SystemService/WatchServerStatus` to receive:
- `SERVER_STATUS_READY` immediately on subscription
- `SERVER_STATUS_SHUTTING_DOWN` when shutdown begins (with grace period)

## See Also

- [grpc-server.md](grpc-server.md) - gRPC service documentation
- [deployment.md](deployment.md) - ROM discovery and installation
- [keyboard.md](keyboard.md) - Keyboard matrix documentation
