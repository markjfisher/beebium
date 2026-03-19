# 6502 Code Generator

## Overview

The 6502 CPU emulation library uses a code generator to produce
cycle-accurate instruction dispatch functions. The generator reads the
hand-written `6502.c` (which defines the instruction semantics and bus
behaviour) and produces `6502_internal.inl` (which contains the
cycle-by-cycle state machine functions for every instruction and
addressing mode).

The generator originates from Tom Seddon's B2 BBC Micro emulator
(https://github.com/tom-seddon/b2) and is included in Beebium under GPL v3.

## How It Works

The generator (`6502_gen.cpp`) defines each 6502 instruction as a sequence
of bus cycles. Each cycle specifies:

- **Address**: what address appears on the bus (`pc++`, `ad`, `1.ad+index`,
  `ial+x`, etc.)
- **Data**: what the CPU does with the data bus value (`adl`, `adh`, `data`,
  etc.)
- **Action**: whether the instruction function is called at the end of
  this cycle (`call`, `maybe_call`, etc.)
- **Read type**: what kind of bus cycle this is (`ReadData`,
  `ReadInstruction`, `ReadAddress`, `ReadUninteresting`, etc.)

For example, the `LDA (zp),Y` addressing mode is defined as:

```
Ri("pc++", "ial", nullptr)           // T2: fetch ZP operand
Ra("ial", "adl", nullptr)            // T3: read pointer low byte
Ra("ial+1", "adh", nullptr)          // T4: read pointer high byte
Rn("1.ad+index", "data!", "maybe_call")  // T5: read from (possibly uncorrected) addr
Rd("2.ad+index", "data", "call")     // T6: read from corrected addr (if page crossed)
```

The shorthand macros map to `Cycle::Type` values:

| Macro | Type | M6502ReadType | Purpose |
|-------|------|---------------|---------|
| `Rd` | ReadData | Data (1) | CPU will use this value |
| `Rn` | ReadDataNoCarry | Data (1) or Data+1 (2) | Data if no page cross, else fixup |
| `Ri` | ReadInstruction | Instruction (2) | Instruction stream byte |
| `Ra` | ReadAddress | Address (3) | Indirect address fetch |
| `Ru` | ReadUninteresting | Uninteresting (4) | CPU discards the value |
| `W` | Write | 0 (write) | CPU drives the data bus |

The generator produces C functions named `CycleN_MODE_VARIANT` (e.g.
`Cycle3_R_INY_BCD_CMOS_D0`) that implement each cycle as a state transition.
These are collected into `6502_internal.inl` which is `#include`d by `6502.c`.

## Build Integration

The generator is built and run automatically during the CMake build:

```
src/6502/
    third_party/b2/           <-- Tom Seddon's code (GPL v3)
        CMakeLists.txt            Builds b2_shared_lib + 6502_gen
        src/6502/
            6502_gen.cpp          The generator (1700 lines C++)
            6502_gen.inl          Enum definitions
        src/shared/               Minimal subset of B2's shared library
    src/
        6502.c                    Input: hand-written 6502 emulation
    include/6502/
        6502.h                    Public API
    CMakeLists.txt                Invokes generator, builds 6502_lib
```

The build sequence is:

1. CMake builds `b2_shared_lib` (B2's shared utility library, minimal subset)
2. CMake builds `6502_gen` (the generator executable, links `b2_shared_lib`)
3. A custom command runs `6502_gen -o 6502_internal.inl -c 6502.c`
4. CMake compiles `6502.c` (which `#include`s the generated `.inl`)
5. The result is `6502_lib` (static C library)

The generated `6502_internal.inl` is written to the build directory, not the
source tree.

## Generator Input

The `-c` flag tells the generator to scan `6502.c` for function names. The
generator uses this to produce a lookup table mapping function names to
pointers, used for debugging and state inspection. The actual instruction
cycle definitions are hard-coded in `6502_gen.cpp` itself.

## M6502ReadType

Each bus cycle is classified with an `M6502ReadType` value, defined in
`6502.h`:

| Value | Name | Meaning |
|-------|------|---------|
| 0 | (write) | CPU is writing |
| 1 | Data | CPU will use the read value |
| 2 | Instruction | Non-opcode instruction stream byte |
| 3 | Address | Indirect address fetch |
| 4 | Uninteresting | CPU will discard the value |
| 5 | Opcode | Opcode fetch (first byte of instruction) |
| 6 | Interrupt | Dummy fetch during interrupt entry |

The `ReadDataNoCarry` type (`Rn` macro) generates a conditional read type:

```c
s->read = M6502ReadType_Data + s->acarry;
```

When there is no page cross (`acarry=0`), the read type is `Data` (1).
When there is a page cross (`acarry=1`), the read type is `Instruction` (2).
This means page-cross fixup reads are currently classified as `Instruction`
rather than `Uninteresting`.

The caller (e.g. `ParasiteCpu::tick()`) can inspect `cpu_.read` to determine
what kind of bus cycle occurred and route the memory access accordingly.

## Attribution and Licensing

The generator and its shared library dependency are Copyright (C) 2016-2024
Tom Seddon, licensed under GPL v3. The full license text is at
`src/6502/third_party/b2/COPYING`. Attribution details and contribution
instructions are in `src/6502/third_party/b2/ATTRIBUTION.md`.

The directory structure within `third_party/b2/` mirrors B2's `src/` layout
to facilitate diffing against upstream and contributing changes back:

```
Beebium                              B2
src/6502/third_party/b2/             src/
    src/6502/6502_gen.cpp        <-> 6502/6502_gen.cpp
    src/6502/6502_gen.inl        <-> 6502/6502_gen.inl
    src/shared/h/shared/*.h      <-> shared/h/shared/*.h
    src/shared/c/*.cpp           <-> shared/c/*.cpp
```

## Regenerating

To regenerate after modifying the generator, a normal build is sufficient:

```bash
cd build
make 6502_lib
```

CMake tracks the dependency: if `6502_gen.cpp`, `6502_gen.inl`, or `6502.c`
changes, the generator is rebuilt and the `.inl` is regenerated automatically.

To regenerate manually (e.g. to inspect the output):

```bash
./build/src/6502/third_party/b2/6502_gen \
    -o /tmp/6502_internal.inl \
    -c src/6502/src/6502.c
```
