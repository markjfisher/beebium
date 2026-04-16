# Memory View

A real-time visual representation of the 64K address space, rendered as a 256x256 pixel image where each pixel corresponds to one byte. Colour encodes the type of memory access: reads, writes, and instruction fetches are each mapped to a separate RGB channel, producing an at-a-glance picture of what the CPU is doing and where.

## Inspiration

B-Em's Memory View window maintains three per-address counters (`readc`, `writec`, `fetchc`). Each counter is set to 31 on access and decays by 1 per frame. The counter value is multiplied by 8 to produce a 0-248 brightness in one RGB channel:

| Channel | Access type       |
|---------|-------------------|
| Red     | Write             |
| Green   | Data read         |
| Blue    | Instruction fetch |

Combined colours emerge naturally: yellow (red + green) for addresses that are both read and written, cyan (green + blue) for data read from code regions, and so on. Addresses with no recent access fade to black.

The result is surprisingly useful for understanding program behaviour — you can immediately see the shape of the stack, the working set of a running program, tight loops that hammer a few addresses, DMA-like activity sweeping through screen memory, and disc controller buffer fills.

## Why gRPC Makes This Easy

B-Em's implementation is tightly coupled to the emulator core — the counters are incremented inline in the 6502 read/write paths, and the rendering runs in a thread inside the same process. This is fast but invasive.

Beebium's architecture offers a cleaner approach. The `DebuggerControl.PeekMemory` RPC already exists and can read the entire 64K address space in a single call without side effects. A memory view client doesn't need to instrument the CPU at all — it just polls the memory contents and computes the visualisation externally.

### Approach: Snapshot Differencing

Rather than tracking individual read/write/fetch events (which would require server-side instrumentation), the client takes periodic memory snapshots and diffs them:

1. Poll `PeekMemory(address=0, length=65536)` at a fixed interval (e.g. 10-20 Hz)
2. Compare each byte against the previous snapshot
3. Any byte that changed gets its "write" counter set to the maximum brightness
4. Unchanged bytes decay toward black

This gives a simplified but still highly useful view: you see *where memory is changing* over time, which covers the most valuable use case (identifying active regions, watching screen memory updates, seeing stack activity, spotting DMA patterns).

### What Snapshot Differencing Misses

The diff approach cannot distinguish reads from writes, nor can it detect instruction fetches. A byte that is read but not modified looks the same as an untouched byte. In B-Em's scheme this means we lose the blue (fetch) and green (read-only) channels — the view becomes single-channel (showing change vs. no change) rather than three-channel.

This is a reasonable trade-off for a first version. The "where is memory changing?" question is the most common one, and it requires no server-side changes at all.

### Future: Server-Side Access Tracking

A richer implementation could add optional access tracking to the server. This would require:

- Per-address access counters (or bitfields) maintained by the memory bus
- A new RPC to retrieve the access map, e.g. `GetMemoryAccessMap` returning a byte array where each byte encodes recent access types
- Server-side decay logic (or frame-stamped counters that the client decays)

This would restore the full three-channel view (read/write/fetch in separate colours) and could also support features like highlighting I/O register access. But it's not needed for a useful first version.

## Client Implementation

The memory view is a pure client-side feature. Any Beebium frontend can implement it.

### macOS (Swift/Metal)

A 256x256 Metal texture, updated from a timer callback that:

1. Calls `PeekMemory` for the full 64K
2. Diffs against the previous snapshot
3. Updates per-address brightness counters (increment on change, decay otherwise)
4. Writes the counter values into the texture's pixel data
5. Presents the texture in a dedicated window

The texture update is cheap — 65,536 pixels at 4 bytes each is 256K of data, well within what Metal can handle at 20 Hz.

### Python

The Python client could render a memory view using any image library (PIL, pygame, etc.) for debugging and automation purposes. The same poll-diff-render loop applies.

## Interaction Ideas

- **Hover tooltip**: show the address (hex), current value, and any known label at the cursor position
- **Click to inspect**: clicking a pixel could open a hex dump or disassembly centred on that address
- **Zoom**: allow zooming into a region (e.g. a 16x16 block showing 256 bytes) for finer inspection
- **Pause/resume**: freeze the display to examine a snapshot without it decaying away
- **Speed control**: adjust the polling rate and decay speed
- **Region overlay**: optionally draw region boundaries (RAM / ROM / I/O / screen memory) as a transparent overlay to provide spatial context
