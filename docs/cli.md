# Beebium Command Line Interface

The Beebium emulator runs as a headless gRPC server. This document covers all command-line options for the server executables.

## Executables

| Executable | Machine | Description |
|------------|---------|-------------|
| `beebium-model-b` | BBC Model B | Original 32K BBC Micro with MOS 1.20 |
| `beebium-model-b-plus` | BBC Model B+ 64K | Enhanced 64K model with MOS 2.0 |

## Usage

```bash
<executable> [global-options] <subcommand> [subcommand-options]
```

If no subcommand is specified, `start` is assumed.

## Global Options

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Show global help message |

## Integer Formats

All integer arguments accept multiple formats:

| Format | Prefix | Example | Value |
|--------|--------|---------|-------|
| Decimal | (none) | `48875` | 48875 |
| Hexadecimal | `0x` or `0X` | `0xBEEB` | 48875 |
| Binary | `0b` or `0B` | `0b1010` | 10 |
| Octal | `0o` or `0O` | `0o377` | 255 |

This applies to `--port`, `--screen-mode`, `--links`, slot numbers in `--sideways`, and drive numbers in `--floppy`.

## Subcommands

### start

Start the emulator server. This is the default subcommand.

```bash
beebium-model-b start [options]
beebium-model-b [options]           # Equivalent (start is default)
```

#### ROM Configuration

| Option | Description |
|--------|-------------|
| `--mos <filepath>` | Path to MOS ROM (default: machine-specific) |
| `--sideways <slot>:<type>[:<image>]` | Configure sideways slot (see below) |
| `--rom-dir <dirpath>` | ROM directory (auto-detected if not specified) |

The `--sideways` option supports three slot types:
- `SLOT:rom:IMAGE` - Load ROM image file into slot
- `SLOT:ram[:IMAGE]` - Configure as RAM (optionally pre-load from file)
- `SLOT:empty` - Leave slot empty

#### Disc Configuration

| Option | Description |
|--------|-------------|
| `--floppy <drive>:<filepath\|url>` | Load disc image into drive 0 or 1 |
| `--fdc <type>` | Disc controller to install (Model B only) |

The `--floppy` option accepts file paths (most common) or `file://` URLs.

Disc controller types for `--fdc`:
- `acorn-1770` - Acorn WD1770 controller
- `none` - Leave socket empty (no disc)

#### Network

| Option | Default | Description |
|--------|---------|-------------|
| `--port <port>` | 48875 (0xBEEB) | gRPC server port |

Use `--port 0` to request dynamic port allocation. The server prints the allocated port to stdout:

```
Starting gRPC server...
Listening on port 54321
```

Scripts can parse the `Listening on port <N>` line to discover the allocated port.

#### Startup Control

| Option | Description |
|--------|-------------|
| `--wait` | Delay emulation start (mode auto-detected) |
| `--wait=cli` | Wait for RETURN keypress on stdin |
| `--wait=api` | Wait for `Run()` RPC from client |

The `--wait` option allows clients to connect and set up before emulation begins.

**`--wait` (bare)**: Auto-detects mode based on whether stdin is a TTY:
- **TTY detected**: Uses `cli` mode (waits for RETURN)
- **No TTY**: Uses `api` mode (waits for Run() RPC)

**`--wait=cli`**: Waits for the user to press RETURN on the console before starting emulation.

**`--wait=api`**: Pauses the machine immediately after the 6502 reset sequence completes (7 cycles). Call `DebuggerControl/Run` to start emulation.

#### Startup Options (Keyboard Links)

| Option | Description |
|--------|-------------|
| `--screen-mode <0-7>` | Startup screen mode (default: 7) |
| `--auto-boot` | Reverse SHIFT-BREAK behavior (SHIFT-BREAK boots disc) |
| `--links <0-255>` | Raw startup options byte |

`--links` is mutually exclusive with `--screen-mode` and `--auto-boot`.

### list-fdcs

List available disc controllers that can be installed in machines with a disc controller socket.

```bash
beebium-model-b list-fdcs
```

### describe-machine

Output machine information as JSON for programmatic use.

```bash
beebium-model-b describe-machine
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

### help

Show help for a subcommand.

```bash
beebium-model-b help                # Global help
beebium-model-b help start          # Start subcommand help
beebium-model-b help list-fdcs      # List-fdcs subcommand help
```

## Exit Codes

Exit codes follow sysexits.h conventions:

| Code | Name | Description |
|------|------|-------------|
| 0 | OK | Success |
| 64 | USAGE | Command line usage error |
| 65 | DATAERR | Data format error |
| 66 | NOINPUT | Cannot open input file |
| 70 | SOFTWARE | Internal software error |
| 74 | IOERR | I/O error |
| 78 | CONFIG | Configuration error |

## Examples

```bash
# Basic usage with defaults
beebium-model-b
beebium-model-b start

# Load a game disc and auto-boot
beebium-model-b start --floppy 0:elite.ssd --auto-boot

# Replace BASIC with Forth
beebium-model-b start --sideways 15:rom:forth.rom

# Multiple ROMs
beebium-model-b start --sideways 14:rom:dfs.rom --sideways 13:rom:viewsheet.rom

# Configure slot as RAM
beebium-model-b start --sideways 4:ram

# Remove default DFS on Model B+ (leave slot 11 empty)
beebium-model-b-plus start --sideways 11:empty

# Dynamic port for testing
beebium-model-b start --port 0

# Use default port in hex (0xBEEB = 48875)
beebium-model-b start --port 0xbeeb

# Wait for API control (automated testing)
beebium-model-b start --wait=api --port 0

# Start in Mode 0 instead of Mode 7
beebium-model-b start --screen-mode 0

# Screen mode in binary (0b101 = 5)
beebium-model-b start --screen-mode 0b101

# Keyboard links in hex (0xff = 255)
beebium-model-b start --links 0xff

# List available disc controllers
beebium-model-b list-fdcs

# Get machine info as JSON
beebium-model-b describe-machine

# Install disc controller (Model B only)
beebium-model-b start --fdc acorn-1770

# Help variants
beebium-model-b --help              # Global help
beebium-model-b help                # Global help
beebium-model-b help start          # Start subcommand help
beebium-model-b start --help        # Start subcommand help
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
