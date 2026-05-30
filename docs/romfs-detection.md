# Detecting a ROM Filing System in a sideways ROM

These notes are for the ROM-header inspection tool (the code behind
`SidewaysRomHeader` / `RomHeaderInfo.swift`). They explain how to add a
label that says *"this service ROM carries ROM Filing System (ROMFS)
data"*, so a paged-ROM image can be reported as e.g.

```
SNAPPER     service ROM   "(C) Acornsoft 1982"   — ROMFS, 5 files
```

The detection rules below are the same ones the `oaknut` library uses in
its ROMFS identifier (`oaknut.romfs`); they have been checked against a
corpus of real Electron and BBC ROMFS images plus images written by
`mkromfs` and `oaknut`. Every offset and constant here is verified, not
assumed.

## Implementation status

This algorithm has landed. Where it lives:

- **C++ parser** — `src/core/include/beebium/RomFsDetection.hpp` provides
  `crc16_xmodem`, `validate_cfs_header`, `find_first_block`, and
  `contains_romfs`, exactly as drafted below. The header is included by
  `SidewaysRomHeader.hpp`, which gains `contains_romfs` and
  `romfs_data_offset`, set after the standard header is recognised (the
  detector runs only when the service-entry bit is set; the language bit
  is treated as orthogonal, so Acornsoft `&C2` cartridges are caught).
- **CLI** — `describe-rom <rom>` JSON gains `contains_romfs`,
  `romfs_data_offset`, and a convenience `kinds` array (any combination of
  `"language"`, `"service"`, `"romfs"`).
- **macOS frontend** — the New Machine dialog's Memory tab appends the
  kinds to each socket's caption (e.g. `"... · language · service ·
  ROMFS"` for an Acornsoft cartridge), via `RomHeaderInfo.kinds` decoded
  from the CLI.
- **Test corpus** — `tests/assets/roms/romfs/` holds 11 reference images
  from oaknut plus an oaknut-authored `SNAPPER.rom`, exercising both the
  Acornsoft `&C2` (language + service + ROMFS) and BBC / `mkromfs` `&82`
  (service + ROMFS) patterns. The focused unit tests are in
  `tests/test_romfs_detection.cpp`; the header-parser coverage including
  Hopper's documented `&80BB` title-block offset is in
  `tests/test_sideways_rom_header.cpp`.

Still future: walking the CFS chain for file counts / completeness, and a
runtime ROMFS readout in the live-machine view. The algorithm notes
below are the design reference; treat them as background for those
follow-ons where they go beyond the above.

## Why the header alone is not enough

`SidewaysRomHeader` already says it: the standard paged-ROM header cannot
tell a ROM filing system apart from any other service ROM. That is not a
limitation of the parser — it is a fact about the format.

A ROMFS ROM is **data, not a filing system**. The ROM Filing System code
lives in the MOS, not in the ROM. The ROM simply answers two service
calls — `&0D` (RFS initialise: point the MOS's byte pointer at the data)
and `&0E` (read the next byte) — and hands the MOS a stream of bytes. The
MOS interprets that stream as a Cassette Filing System (CFS) block chain.
So at the header level a ROMFS ROM is an utterly ordinary service ROM:

- The **type byte** (offset 6) has **bit 7 set** (service entry), like
  every service ROM. ROMFS ROMs written by `mkromfs`/`oaknut` are type
  `&82` (service-only); the original Acornsoft cartridges are type `&C2`
  because they are *also* language ROMs (bit 6) so the cartridge
  auto-starts. **Do not test for service-only** — test only that bit 7 is
  set, and treat the language bit as orthogonal.
- The **copyright** is a normal `(C)...` string, already parsed.
- The **title** is a normal title; for ROMFS it doubles as the `*HELP`
  text.

None of that distinguishes ROMFS. The distinguishing evidence is in the
ROM body, *after* the header and the ROM's hand-written service handler:
a **CFS block chain**.

## The decisive test: find one valid CFS block header

A ROMFS ROM contains, somewhere after the header, a run of CFS blocks.
The presence of **even one structurally valid, CRC-correct block header**
is decisive: the header CRC is a 16-bit check over roughly 20+ bytes, so
a stray `&2A` byte in unrelated code or data essentially never produces a
valid block by chance.

You cannot assume a fixed start offset. The filing-system data follows
the ROM's service handler, whose length varies from ROM to ROM (observed
data starts include `&805D`, `&80BB`, `&810B`, `&829C`). So **scan**:

1. From offset 9 (the earliest the body can begin) to end of image,
   find each byte equal to `&2A` (`'*'`, the block **sync byte**).
2. At each candidate, try to parse a CFS block header (below) and verify
   its header CRC.
3. The **first** candidate whose CRC validates marks the start of the
   filing system. If none validates anywhere in the image, the ROM does
   **not** contain ROMFS data.

That is the whole test. If you want a richer label (file count,
completeness), walk the chain from that point — see "Walking the chain".

## CFS block-header layout

A *header block* begins with the sync byte and is followed by these
fields. Multi-byte integers are **little-endian**; the trailing header
CRC is **big-endian**.

| Offset from sync | Size | Field |
|---|---|---|
| 0 | 1 | sync byte `&2A` |
| 1 | *n*+1 | file name, ASCII, **NUL-terminated**, name ≤ 10 chars |
| 2+*n* | 4 | load address (LE) |
| 6+*n* | 4 | execution address (LE) |
| 10+*n* | 2 | block number (LE) — 0 for the first block of a file |
| 12+*n* | 2 | block data length (LE) — bytes of data in this block, ≤ 256 |
| 14+*n* | 1 | flag byte (see below) |
| 15+*n* | 4 | end-of-file address (LE) — paged-ROM address just past this file |
| 19+*n* | 2 | **header CRC (big-endian)** |

(*n* is the name length; the NUL terminator is the `+1`.)

The header CRC is computed over **the name, its NUL terminator, and the
17 fixed bytes** — i.e. everything between the sync byte and the CRC
itself (the sync byte is not included).

Immediately after the header CRC come `block data length` data bytes,
followed by a **2-byte big-endian data CRC** (omitted when the length is
0). For the first block of a file the data length is enough to validate;
you don't need the data CRC just to label the ROM.

### Flag byte

| Bit | Meaning |
|---|---|
| 7 (`&80`) | last block of this file |
| 6 (`&40`) | empty block — `mkromfs` sets it; Acorn does not. To detect an empty file test `block length == 0`, not this bit. |
| 0 (`&01`) | `*RUN`-only (a primitive copy protection: the MOS refuses `*LOAD`/`*EXEC`/`CHAIN`, only `*RUN`). The OS calls this "locked", but it is unrelated to the disc filing systems' delete-lock. |

## The CRC: CRC-16/XMODEM

Both the header CRC and the data CRC are **CRC-16/XMODEM**:

- polynomial `0x1021`
- initial value `0x0000`
- most-significant-bit first, **no** input or output reflection
- the 16-bit result is stored **big-endian** on the ROM

Check value: the CRC of `"123456789"` is `0x31C3`; the CRC of the empty
input is `0x0000`.

```cpp
inline uint16_t crc16_xmodem(std::span<const uint8_t> data) {
    uint16_t crc = 0;
    for (uint8_t byte : data) {
        crc ^= static_cast<uint16_t>(byte) << 8;
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}
```

## Detection, in code

This mirrors `SidewaysRomHeader.hpp`'s style and is meant to sit beside
it. It only needs a valid *first* block to return true; walking further
is optional.

```cpp
// Markers in the CFS block stream.
constexpr uint8_t kCfsSync       = 0x2A; // '*'  header block
constexpr uint8_t kCfsContinue   = 0x23; // '#'  data-only continuation block
constexpr uint8_t kCfsEndOfFs    = 0x2B; // '+'  end of the filing system

// Offset of the first byte that could begin the body (== title offset).
constexpr size_t kRomBodyStart = 9;

// Try to validate a CFS header block at `pos`. On success, sets
// `data_offset` to the first data byte (just past the header CRC) and
// `data_length` to this block's data byte count.
inline bool validate_cfs_header(std::span<const uint8_t> rom, size_t pos,
                                size_t& data_offset, uint16_t& data_length) {
    if (pos >= rom.size() || rom[pos] != kCfsSync) return false;

    // Name: ASCII up to a NUL, at most 10 characters.
    size_t name_start = pos + 1;
    size_t nul = name_start;
    while (nul < rom.size() && rom[nul] != 0x00) ++nul;
    if (nul >= rom.size()) return false;                 // unterminated
    if (nul - name_start > 10) return false;             // name too long

    size_t fixed_start = nul + 1;       // 17 fixed bytes follow the NUL
    size_t crc_start   = fixed_start + 17;
    size_t crc_end     = crc_start + 2; // 2-byte big-endian header CRC
    if (crc_end > rom.size()) return false;              // runs off the end

    uint16_t stored = (static_cast<uint16_t>(rom[crc_start]) << 8) | rom[crc_start + 1];
    uint16_t computed = crc16_xmodem(rom.subspan(name_start, crc_start - name_start));
    if (stored != computed) return false;

    // Fixed fields after the NUL: load(4) exec(4) block#(2) length(2) flag(1)
    // end(4). The block data length is the little-endian word at byte 10.
    data_length = static_cast<uint16_t>(rom[fixed_start + 10]) |
                  (static_cast<uint16_t>(rom[fixed_start + 11]) << 8);
    data_offset = crc_end;
    return true;
}

// True if this image carries ROM Filing System data: scan for the first
// CRC-valid CFS header block anywhere from the body start onward.
inline bool contains_romfs(std::span<const uint8_t> rom) {
    for (size_t pos = kRomBodyStart; pos < rom.size(); ++pos) {
        if (rom[pos] != kCfsSync) continue;
        size_t data_offset; uint16_t data_length;
        if (validate_cfs_header(rom, pos, data_offset, data_length)) return true;
    }
    return false;
}
```

A natural place for the result is a couple of extra fields on
`SidewaysRomHeader` (or a small sibling struct), set after the header is
recognised:

```cpp
bool   contains_romfs = false; // a CRC-valid CFS block chain is present
size_t romfs_data_offset = 0;  // image offset of the first valid block
```

## Walking the chain (optional: file count and completeness)

Once you have the first block's offset you can walk the chain to count
files and tell a complete filing system from a fragment. From the first
valid header:

- A `&2A` byte starts a **new header block**: validate it, then skip its
  `data_length` data bytes plus the 2-byte data CRC (no data/CRC when the
  length is 0). Block number 0 starts a new file; the flag's bit 7 marks
  a file's last block.
- A `&23` byte is a **continuation** (data-only) block of the current
  file: it carries exactly 256 data bytes plus a 2-byte data CRC, with no
  header.
- A single `&2B` byte **ends the filing system**. A chain that reaches
  `&2B` cleanly is **complete**.
- If the stream runs off the end of the image, or meets a byte that is
  none of `&2A`/`&23`/`&2B` where a block is expected, the chain is
  **incomplete** — either a truncated image or, legitimately, one ROM of
  a **multi-ROM filing system** that spans several adjacent sockets (the
  MOS continues into the socket below and the `&2B` appears only in the
  final ROM). You cannot tell these two apart from one image; label both
  "incomplete" and treat the ROM as read-only.

Counting distinct files = counting header blocks with block number 0. The
catalogue (`*.`) columns the MOS prints are, per file: name, *last block
number* (= block count − 1, **not** a count), total length, load, exec.

### The title block

The first file in the chain is often a zero-length **title block** whose
name is the filing-system title shown by `*.`. Acornsoft cartridges wrap
it in asterisks (`*Hopper01*` → title `Hopper01`); the BBC Master
Demonstration cartridges use a bare name (`DEMO-A`); some ROMs (e.g. the
BBC `Zalaga`) have **no** title block and go straight to the first real
file. Detect it **positionally** — a zero-length first file — not by the
asterisks. It is optional; its absence is not an error.

## Worked numbers from the corpus

For confidence when testing:

- `Electron_Hopper.rom` — Acornsoft cartridge, type `&C2` (service **and**
  language: it auto-starts as a language ROM) yet still ROMFS data. Title
  block `*Hopper01*` (zero length) at `&80BB`, several data files, ends in
  `&2B` (complete). The Acornsoft titles (Snapper, Starship Command, Tree
  Of Knowledge, Countdown To Doom) are all `&C2` with an asterisk-wrapped
  title block.
- `Zalaga.rom` (a BBC ROM) and the BBC Master Demonstration cartridges —
  type `&82` (service-only). Zalaga has **no** title block: its first
  block is the real file `ZALAGA` (length 256) at `&810B`. The Master
  demos use a bare title block (`DEMO-A` / `DEMO-B`) at `&8086`. Images
  written by `mkromfs`/`oaknut` are also `&82`.
- `Electron_Countdown_To_Doom_*` — two **independent** ROMs (`*Doom01*`,
  `*Doom02*`), each complete on its own. A `_1`/`_2` naming pair does
  **not** imply a spanning set; every pair in this corpus is two separate
  filing systems.

## Test images you can copy into your corpus

The `oaknut` checkout holds ROMFS images you can copy straight into the
Beebium test corpus:

- **A freshly authored cartridge** — `oaknut`'s own
  `/Users/rjs/Code/oaknut/SNAPPER.rom` (16 KiB, type `&82`). This is the
  game *Snapper* written onto a brand-new ROM by `oaknut`, not an original
  dump: it carries a synthesised `&0D`/`&0E` RFS service handler and a
  `&09` `*HELP` responder, and it is verified to load and run in Beebium
  (`*ROM`, `*EXEC !BOOT`). It is a good detection test because its block
  chain begins after a *generated* handler of different length from the
  original ROMs. Note it is a build artifact (regenerable, may be
  uncommitted); if it is absent, recreate it with:

  ```sh
  cd /Users/rjs/Code/oaknut
  disc create SNAPPER.rom --title Snapper
  disc romfs set-copyright SNAPPER.rom '(C) Acornsoft 1982'
  disc cp 'tests/data/images/games/Disc001-SnapperV2.ssd:$.*' SNAPPER.rom
  ```

- **The reference corpus** — the 11 original Electron and BBC ROMFS dumps
  used by `oaknut`'s own tests live in
  `/Users/rjs/Code/oaknut/tests/data/images/romfs/` (the images listed
  under "Worked numbers" above). These are committed to the `oaknut`
  repository, so they are a stable set to copy and pin.

## One thing to *not* infer

Bit 7 of the type byte and an auto-start (language) bit say nothing about
ROMFS specifically. Auto-start is the ROM being a language ROM, which is
orthogonal to carrying ROMFS data. The only reliable signal is a
CRC-valid CFS block chain in the body — that, and nothing in the header,
is what makes a ROM a ROM-filing-system ROM.
