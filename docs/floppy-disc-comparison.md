# Floppy Disc Image Format Comparison

A survey of disc image format support across BBC Micro emulators: B-Em, b2, jsbeeb, beebjit, BeebEm, and Beebium.

## Format Support Matrix

| Format | Extension | B-Em | b2 | jsbeeb | beebjit | BeebEm | Beebium |
|--------|-----------|------|-----|--------|---------|--------|---------|
| SSD (DFS single-sided) | `.ssd` | R/W | R/W | R/W | R/W | R/W | R/W |
| DSD (DFS double-sided) | `.dsd` | R/W | R/W | R/W | R/W | R/W | R/W |
| SDD (DDFS single-sided) | `.sdd` | R/W | R/W | - | - | - | - |
| DDD (DDFS double-sided) | `.ddd` | R/W | R/W | - | - | - | - |
| ADF (ADFS auto-detect) | `.adf` | R/W | R/W | R | - | R/W | - |
| ADS (ADFS Small) | `.ads` | R/W | R/W | - | - | - | - |
| ADM (ADFS Medium) | `.adm` | R/W | R/W | R | - | - | - |
| ADL (ADFS Large) | `.adl` | R/W | R/W | R | R/W | R/W | - |
| IMG (generic/DOS) | `.img` | R/W | - | - | - | R/W | - |
| DOS (MS-DOS) | `.dos` | - | - | - | - | R/W | - |
| FSD (preservation) | `.fsd` | - | - | - | R | R | - |
| HFE (HxC Floppy Emulator) | `.hfe` | R | - | R/W | R/W | - | - |
| FDI (Formatted Disc Image) | `.fdi` | R | - | - | - | - | - |
| IMD (ImageDisk) | `.imd` | R/W | - | - | - | - | - |
| KryoFlux raw stream | `.raw` | - | - | - | R | - | - |
| SuperCard Pro | `.scp` | - | - | - | R | - | - |
| DiscFerret Image | `.dfi` | - | - | - | R | - | - |
| Raw Flux Image | `.rfi` | - | - | - | R | - | - |

R = read-only, R/W = read/write, - = not supported.

## Disc Controllers Emulated

| Controller | B-Em | b2 | jsbeeb | beebjit | BeebEm | Beebium |
|-----------|------|-----|--------|---------|--------|---------|
| Intel 8271 | Yes | - | Yes | Yes | Yes | - |
| WD1770 (Acorn) | Yes | Yes | Yes | Yes | Yes | Yes |
| WD1770 (Master 128) | Yes | Yes | Yes | Yes | Yes | - |
| WD1772 | - | - | - | Yes | - | - |
| Opus variants | Yes | Yes | - | Yes | - | - |
| Watford DDFS | Yes | Yes | - | - | - | - |
| Solidisk | Yes | - | - | - | - | - |

## Format Detection Strategies

All six emulators use **file extension** as the primary detection mechanism. None use magic bytes for initial format selection, though some validate headers after dispatch.

| Emulator | Primary | Secondary | Notes |
|----------|---------|-----------|-------|
| B-Em | Extension | DFS catalogue validation, "Hugo" marker for ADFS, file size | Most sophisticated detection; reads disc metadata to disambiguate ADF variants |
| b2 | Extension | File size | Size-based geometry selection for multi-geometry formats (SDD, DDD, ADF) |
| jsbeeb | Extension | None | Falls back to SSD for unknown extensions |
| beebjit | Extension | Header magic validation | Loaders validate format headers (HFE, FSD, DFI, SCP) after dispatch |
| BeebEm | Extension | File size for double-sided SSD | SSD files >200KB treated as non-interleaved double-sided |
| Beebium | Extension | File size | Strict size matching: rejects images that don't match expected SSD/DSD sizes |

## Implementation Strategies

The emulators fall into two distinct architectural camps based on how they represent disc data internally.

### Sector-Level Emulation

**B-Em** (SDF formats), **b2**, **BeebEm** (standard formats), and **Beebium** store disc data as flat arrays of sector bytes. The FDC implementation translates controller commands directly into sector read/write operations against this array.

Advantages:
- Simple implementation
- Low memory overhead
- Direct file write-through for persistence

Disadvantages:
- Cannot represent non-standard track layouts
- No support for copy protection schemes that rely on track structure
- Read Track / Write Track commands require synthetic gap/header generation or are incomplete

**Beebium's approach**: Entire image loaded into `std::vector<uint8_t>`. Sector offset calculated from geometry. Writes are immediate file write-through via `fstream`. The WD1770 implementation is cycle-accurate with proper motor spin-up delays and inter-byte timing, but Read Track returns raw concatenated sector data without synthesised track structure.

**b2's approach**: Two implementations -- `MemoryDiscImage` (primary, full image in memory with copy-on-write and SHA1 hashing) and `DirectDiscImage` (file-based, opens on motor spin-up, closes on spin-down). Supports ZIP auto-extraction.

### Pulse-Level Emulation

**jsbeeb** and **beebjit** convert all formats on load into pulse-level track representations. Even simple SSD/DSD images are synthesised into complete FM/MFM-encoded tracks with sync patterns, address marks, gap bytes, CRC fields, and data. The FDC reads individual magnetic pulses from the simulated disc surface.

**B-Em** uses this approach for its advanced formats (FDI, HFE, IMD) while using sector-level access for SDF formats, making it a hybrid.

**BeebEm** uses a hybrid approach as well: sector-level for standard formats, but FSD images are loaded with full track metadata including sector IDs, error codes, and variable sector sizes.

Advantages:
- Faithful reproduction of real hardware behaviour
- Natural support for copy protection (weak bits, CRC errors, non-standard layouts)
- FDC implementation is format-agnostic -- it just reads pulses
- Format conversion between any supported types is straightforward

Disadvantages:
- Higher memory usage (track data is much larger than sector data)
- More complex implementation
- Write-back to sector formats requires extracting sectors from pulse data

**beebjit's approach**: All formats are converted to pulse streams. The disc drive simulation includes 300 RPM rotation timing, sub-track head positioning, and quasi-random pulse generation for unformatted regions. Supports format conversion between any combination of SSD/DSD, ADL, and HFE via `--convert-*` flags.

**jsbeeb's approach**: Very similar to beebjit (jsbeeb's FDC code was translated from beebjit). Uses `TrackBuilder` for track synthesis and `FmReader`/`MfmReader` for decoding. HFE v3 saving preserves weak bit regions via RAND opcodes.

## Copy Protection Support

| Feature | B-Em | b2 | jsbeeb | beebjit | BeebEm | Beebium |
|---------|------|-----|--------|---------|--------|---------|
| Weak/fuzzy bits | Via FDI/HFE | - | Yes (HFE v3 RAND, drive random) | Yes (FSD, HFE v3, drive random) | - | - |
| CRC errors | Via FDI/HFE/IMD | - | Yes (per-sector) | Yes (FSD error codes) | FSD only | - |
| Deleted data marks | Via FDI/HFE/IMD | - | Yes | Yes | FSD only | - |
| Non-standard sector layouts | Via FDI/HFE/IMD | - | Yes (pulse-level) | Yes (pulse-level) | FSD (metadata) | - |
| Variable sector sizes | IMD | - | Yes (pulse-level) | Yes | FSD | - |
| Raw flux capture | - | - | - | Yes (KryoFlux, SCP, DFI, RFI) | - | - |

## Geometry Support

### Sector Sizes

| Emulator | 128 | 256 | 512 | 1024 | 2048+ |
|----------|-----|-----|-----|------|-------|
| B-Em | IMD | All | IMG/IMD | IMG/IMD | IMD |
| b2 | - | All | - | - | - |
| jsbeeb | Pulse-level | All | Pulse-level | Pulse-level | - |
| beebjit | Pulse-level | All | Pulse-level | Pulse-level | - |
| BeebEm | FSD | All | FSD/DOS | FSD/IMG | FSD |
| Beebium | - | All | - | - | - |

### Track Counts

| Emulator | 40-track | 80-track | Auto-detect |
|----------|----------|----------|-------------|
| B-Em | Yes | Yes | By file size and format |
| b2 | Yes (SDD/DDD) | Yes | By file size |
| jsbeeb | Yes (double-step) | Yes | Default 80 |
| beebjit | Yes (double-step) | Yes | Default 80 |
| BeebEm | Yes | Yes | By file size |
| Beebium | Yes | Yes | By file size |

### Density Modes

| Emulator | FM (single) | MFM (double) |
|----------|-------------|--------------|
| B-Em | 8271 | 1770 |
| b2 | - | 1770 |
| jsbeeb | 8271 | 1770 |
| beebjit | 8271 | 1770 |
| BeebEm | 8271 | 1770 |
| Beebium | - | 1770 |

## Format Conversion

| Emulator | Conversion Support |
|----------|-------------------|
| B-Em | FDI to raw bitstream (internal only) |
| b2 | Save-as between supported sector formats |
| jsbeeb | SSD/DSD write-back from pulse data; HFE v3 save |
| beebjit | `--convert-hfe`, `--convert-ssd`, `--convert-adl` between any loaded format |
| BeebEm | No explicit conversion |
| Beebium | No conversion |

## Blank Disc Creation

| Emulator | Can Create Blank Discs |
|----------|----------------------|
| B-Em | Yes (DFS and ADFS variants, Watford, Solidisk) |
| b2 | Yes (DFS and ADFS blanks with proper catalogue) |
| jsbeeb | No |
| beebjit | No |
| BeebEm | Yes (ADFS via `CreateADFSImage()`) |
| Beebium | No |

## Additional Features

| Feature | B-Em | b2 | jsbeeb | beebjit | BeebEm | Beebium |
|---------|------|-----|--------|---------|--------|---------|
| ZIP auto-extraction | - | Yes | Yes | - | - | - |
| Disc drive sounds | - | - | Yes | - | Yes | - |
| Write protection | File perms | Boolean flag | Per-format | Per-format | Per-format | File perms |
| Multiple drives | 2 | 2 | 2 | 4 (cyclable) | 2 | 2 |
| Hot-swap discs | Yes | Yes | Yes | Yes (4 per drive) | Yes | Yes (via gRPC) |
| Disc cataloguing tool | - | - | - | Yes (`disc_tool.c`) | - | - |

## Architectural Comparison Summary

**beebjit** has the most comprehensive format support (11 formats including 4 raw flux capture formats) and the most principled architecture -- everything is converted to pulse-level representation, enabling format-agnostic FDC emulation and inter-format conversion.

**B-Em** supports the most sector-level formats (11 including SDF variants) through a hybrid approach, with track-level formats (FDI, HFE, IMD) handled separately from sector-level formats (SDF).

**jsbeeb** follows beebjit's pulse-level approach (its FDC code is translated from beebjit) but supports fewer formats. Its HFE v3 write support is notable.

**b2** focuses on sector-level formats with clean abstractions (DiscImage interface, geometry detection, ZIP support) but lacks advanced format or copy protection support.

**BeebEm** has pragmatic format support with the FSD format providing basic copy protection handling, but its dual-controller implementation (separate 1770 and 8271 codepaths) leads to format restrictions per controller.

**Beebium** currently supports only SSD and DSD formats with a sector-level approach. The WD1770 implementation is cycle-accurate with proper timing, but the lack of track-level synthesis means Read Track returns raw sector data. There is no 8271 controller, no ADFS format support, no copy protection handling, and no preservation format support.

## Recommendations for Beebium

Based on this survey, the most impactful additions would be:

1. **ADFS format support (ADF/ADL/ADM/ADS)** -- supported by all other emulators; essential for Master 128 and ADFS-era software.

2. **Track-level synthesis for Read Track** -- documented as a TODO in the codebase. Synthesising standard DFS/ADFS track structure from sector data would improve compatibility without requiring a full pulse-level rewrite.

3. **HFE format support** -- the de facto standard for disc preservation and hardware emulators (Gotek, HxC). Supported by B-Em, jsbeeb, and beebjit. HFE v3 with its opcode system handles weak bits and variable bitrates.

4. **FSD format support (read-only)** -- the Stardot community's preservation format for copy-protected BBC Micro software. Supported by beebjit and BeebEm. Relatively simple to implement as a read-only loader.

5. **Intel 8271 controller** -- required for accurate Model B emulation with DFS 0.9 and some early software. Currently emulated by B-Em, jsbeeb, beebjit, and BeebEm.

6. **Pulse-level representation** -- a longer-term architectural consideration. Both jsbeeb and beebjit demonstrate that converting all formats to a common pulse-level representation simplifies the FDC implementation, enables format conversion, and provides natural copy protection support. This would be a significant refactor but would make adding new formats trivial.
