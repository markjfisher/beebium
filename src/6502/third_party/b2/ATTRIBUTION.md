# B2 Emulator - 6502 Code Generator

This directory contains files from the [B2 BBC Micro emulator](https://github.com/tom-seddon/b2)
by Tom Seddon.

## Copyright

Copyright (C) 2016-2024 by Tom Seddon

## License

GNU General Public License v3. See `COPYING` in this directory.

## Contact

Tom Seddon <modelb@bbcmicro.com>
https://github.com/tom-seddon/b2

## What is included

A minimal subset of B2 needed to build the `6502_gen` code generator:

- `src/6502/6502_gen.cpp` -- The generator that produces `6502_internal.inl`
- `src/6502/6502_gen.inl` -- Enum definitions used by the generator
- `src/shared/` -- Minimal subset of B2's shared utility library

The generator reads `6502.c` (the 6502 emulator implementation) and produces
`6502_internal.inl` (cycle-accurate instruction dispatch functions).

## Contributing changes back

Changes to files in this directory should be kept minimal and isolated so they
can be contributed back to the upstream B2 project. The directory structure
mirrors B2's `src/` layout to facilitate diffing against upstream:

```
b2/src/6502/6502_gen.cpp   <->   third_party/b2/src/6502/6502_gen.cpp
b2/src/shared/h/shared/*   <->   third_party/b2/src/shared/h/shared/*
b2/src/shared/c/*           <->   third_party/b2/src/shared/c/*
```
