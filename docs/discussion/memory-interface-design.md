

Beebium Debugger Memory API
===========================

The Beebium debugger exposes the memory of a running BBC Micro instance through a small, explicit, and composable Python API. The design prioritises **clarity**, **safety**, and **debugger-appropriate ergonomics** over implicit convenience.

At a high level:

*   Memory access is **always explicit about side effects**
*   Bytes are the fundamental unit; all higher-level views are layered on top
*   Assignment size and semantics are determined by the _left-hand side_
*   Sequential writes use explicit methods, not slicing tricks

* * *

Entry point: `bbc.memory`
-------------------------

The `bbc.memory` object provides **two types of access**:

### Address space access (16-bit flat addressing)

```python
bbc.memory.address.bus    # Side-effecting access
bbc.memory.address.peek   # Side-effect-free access
```

### Region-based access (named memory regions)

```python
bbc.memory.region("main_ram")     # Returns a Region object
bbc.memory.region("shadow_ram")   # Model B+ shadow RAM
bbc.memory.region("bank_4")       # Sideways ROM/RAM bank
```

### Rationale

On a real BBC Micro, reading memory can have side effects (notably for memory-mapped I/O). The API therefore requires the user to make an explicit choice:

*   **`bus`** — normal, side-effecting access (reads/writes go through the memory bus)
*   **`peek`** — guaranteed side-effect-free access

This decision is made _before_ any read, write, or reinterpretation, and all further operations are layered on top of it.

* * *

PC-context access (for B+ shadow RAM)
-------------------------------------

On the BBC Model B+, memory access in the 0x3000-0x7FFF range is routed differently depending on where the executing code resides. This is the "VDU driver code shadow RAM" feature:

- **MOS code (0xC000-0xDFFF)** sees shadow RAM
- **Paged RAM code (0xA000-0xAFFF)** sees shadow RAM (when paged RAM is enabled)
- **All other code** sees main RAM

The debugger API provides PC-context accessors to query "what would code at PC=X see when accessing address Y?":

```python
# Without PC context (default behavior)
value = bbc.memory.address.peek[0x5000]  # Uses default routing

# With PC context
user_view = bbc.memory.address.peek.with_pc(0x1000)[0x5000]  # What user code sees
mos_view = bbc.memory.address.peek.with_pc(0xD000)[0x5000]   # What MOS code sees

# Works with both bus and peek access modes
bbc.memory.address.bus.with_pc(0xD000)[0x5000]   # Side-effecting, MOS perspective
bbc.memory.address.peek.with_pc(0xD000)[0x5000]  # Side-effect-free, MOS perspective

# Typed access via cast() is also supported
word = bbc.memory.address.peek.with_pc(0xD000).cast("<H")[0x5000]
```

### Behavior on different machines

| Machine | `with_pc()` behavior |
|---------|---------------------|
| Model B | No effect (no shadow RAM) |
| Model B+ (shadow disabled) | No effect |
| Model B+ (shadow enabled) | Routes based on simulated PC |

### When to use PC-context access

Use `with_pc()` when debugging B+ shadow RAM scenarios:

- Inspecting what the MOS VDU driver sees during screen rendering
- Understanding why user code and MOS code see different values at the same address
- Testing shadow RAM enable/disable logic
- Debugging programs that use the shadow RAM feature

For direct access to shadow RAM regardless of routing, use region-based access:

```python
# Always accesses shadow RAM directly, bypassing all routing
bbc.memory.region("shadow_ram").bus[0x5000]
```

* * *

Address space access
--------------------

The `bbc.memory.address` object provides access to the full 16-bit address space:

```python
addr = bbc.memory.address

# Side-effecting access (through memory bus)
value = addr.bus[0x1000]
addr.bus[0x1000] = 0x42

# Side-effect-free access (for I/O regions)
value = addr.peek[0xFE4D]
```

Both `bus` and `peek` return a **memory access object** that supports:

*   Indexing and slicing (`__getitem__`, `__setitem__`)
*   Explicit sequential reads and writes (`read()`, `write()`)
*   Typed reinterpretation (`cast(fmt)`)
*   File I/O (`load()`, `save()`)
*   Fill operations (`fill()`)

The two access objects behave identically, except that **write operations are forbidden on `peek`** and **`peek` reads are not observable by the emulated machine**.

* * *

Region-based access
-------------------

The `bbc.memory.region(name)` method provides access to named memory regions:

```python
# Access main RAM (0x0000-0x7FFF on Model B)
main = bbc.memory.region("main_ram")
value = main.bus[0x1234]

# Access shadow RAM on Model B+ (0x3000-0x7FFF)
shadow = bbc.memory.region("shadow_ram")
shadow.bus[0x3000] = 0x42

# Access sideways banks (mapped at 0x8000)
bank4 = bbc.memory.region("bank_4")
data = bank4.peek[0x8000:0x8100]
```

### Absolute addressing

Regions use **absolute addresses** matching their hardware mapping:

| Region | Base Address | Size | Notes |
|--------|-------------|------|-------|
| `main_ram` | 0x0000 | 32KB | Always present |
| `shadow_ram` | 0x3000 | 20KB | Model B+ only |
| `andy_ram` | 0x8000 | 12KB | Model B+ ANDY RAM |
| `bank_0` - `bank_15` | 0x8000 | 16KB | Sideways ROM/RAM |
| `mos_rom` | 0xC000 | 16KB | Operating system |

Each region object has `.bus` and `.peek` accessors that work identically to address space accessors.

### Region discovery

```python
# List available regions
for region in bbc.memory.regions:
    print(f"{region.name}: base=0x{region.base_address:04X}, "
          f"size={region.size}, active={region.active}")

# Machine type
print(bbc.memory.machine_type)  # "ModelB" or "ModelBPlus"
```

* * *

Byte-level access (fundamental layer)
-------------------------------------

### Reading

```python
mem = bbc.memory.address.bus

byte = mem[0x1000]                # int (0-255)
data = mem[0x1000:0x1010]         # bytes
```

### Writing

```python
mem[0x1000] = 0x42
mem[0x2000:0x2004] = b"\x01\x02\x03\x04"
```

### Rules

*   Indexing reads or writes **exactly one byte**
*   Slicing reads or writes **exactly the slice length**
*   Slice assignment requires the data length to match the slice length
*   Assigning multi-byte values to a single address is **not allowed**

### Rationale

This preserves a strong invariant:

> **The size of a write is always determined by the left-hand side.**

This avoids surprising behaviour and makes memory modifications easy to reason about during debugging and code review.

* * *

Sequential access: `read()` and `write()`
-----------------------------------------

For operations where the length is naturally determined by the data itself, the API provides explicit methods.

### Reading

```python
data = mem.read(0x1000, 16)   # returns bytes
```

### Writing

```python
mem.write(0x2000, b"READY")
```

### Semantics

*   `read(addr, length)` reads exactly `length` bytes starting at `addr`
*   `write(addr, data)` writes `len(data)` bytes starting at `addr`
*   `data` may be any object supporting the buffer protocol

### Rationale

Slice assignment requires the user to compute the end address. For sequential operations, this is unnecessary friction. `read()` and `write()` express intent directly and mirror well-understood APIs such as file I/O and `struct.pack_into`.

* * *

Side-effect-free access: `peek`
-------------------------------

```python
io = bbc.memory.address.peek

status = io[0xFE4D]
word   = io.cast("<H")[0xFE00]
```

### Rules

*   All read operations are permitted
*   All write operations raise `TypeError`

### Rationale

This makes side-effect-free inspection explicit and mechanically enforced, which is critical when interacting with hardware registers or I/O space.

* * *

Typed access: `cast(fmt)`
-------------------------

The `cast(fmt)` method reinterprets memory using a `struct`-style format string.

```python
u16 = bbc.memory.address.bus.cast("<H")

value = u16[0x0070]
u16[0x0070] = 0x1234
```

### Slices with casts

```python
values = bbc.memory.address.bus.cast("<H")[0x1000:0x1008]
```

*   Slice length must be a multiple of the format size
*   Returns a tuple of values
*   Assignment requires matching arity

### Rationale

This provides structured access without introducing new primitive types or DSLs, and leverages an existing, well-understood Python convention (`struct`).

* * *

File I/O: `load()` and `save()`
-------------------------------

Memory accessors support loading and saving binary files:

```python
# Load a binary file into memory
bytes_loaded = bbc.memory.address.bus.load(0x1900, "mygame.bin")

# Save memory to a file
bbc.memory.address.bus.save(0x1900, 0x1000, "dump.bin")

# Also works with regions
bbc.memory.region("main_ram").bus.load(0x1900, "program.bin")
```

* * *

Fill operations: `fill()`
-------------------------

Fill a range of memory with a value:

```python
# Fill address range with zeros
bbc.memory.address.bus.fill(0x1000, 0x2000, 0x00)

# Fill a region
bbc.memory.region("shadow_ram").bus.fill(0x3000, 0x8000, 0xFF)
```

* * *

Error behaviour
---------------

The API prefers **early, explicit errors** over silent coercion:

*   Writing to `peek` memory raises `TypeError`
*   Length mismatches raise `ValueError`
*   Invalid casts or misaligned slices raise `ValueError`
*   Implicit multi-byte writes via indexing are disallowed
*   Region addresses outside the valid range raise `ValueError`

This makes mistakes obvious during interactive debugging.

* * *

Design principles (summary)
---------------------------

*   **Explicit over implicit** - especially for side effects
*   **Bytes first** - all higher-level views are layered
*   **Left-hand side determines size** - no hidden writes
*   **Methods for actions, syntax for structure**
*   **Familiar Python idioms** - no bespoke mini-languages
*   **Absolute addressing** - regions use their hardware-mapped addresses

* * *

Complete example
----------------

```python
from beebium import Beebium

with Beebium.connect() as bbc:
    bbc.debugger.stop()

    # Address space access
    pc = bbc.cpu.pc
    opcode = bbc.memory.address.peek[pc]
    print(f"Next instruction at ${pc:04X}: ${opcode:02X}")

    # Read zero page pointers
    lomem = bbc.memory.address.peek.cast("<H")[0x00]
    himem = bbc.memory.address.peek.cast("<H")[0x06]
    print(f"LOMEM=${lomem:04X} HIMEM=${himem:04X}")

    # Region discovery
    print(f"Machine: {bbc.memory.machine_type}")
    for region in bbc.memory.regions:
        if region.active:
            print(f"  {region.name}: 0x{region.base_address:04X}")

    # Direct sideways bank access
    bank = bbc.memory.region("bank_0")
    header = bank.peek[0x8007:0x800F]
    print(f"Bank 0 title: {header.decode('ascii', errors='replace')}")
```

* * *

This API is designed to feel natural to Python users while remaining faithful to the realities of low-level hardware debugging. It encourages clarity of intent, safe experimentation, and composability as the system grows in complexity.
