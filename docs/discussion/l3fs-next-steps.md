# L3FS Configuration: Current State and Next Steps

Status as of 2026-04-09. Branch: `single-threaded-tube`.

## What's Done

### Tube R3 byte-doubling fix (commit 2bbdd06)
BASIC SAVE over DFS with the Tube active now works correctly. The fix
addresses cross-process R3 FIFO timing: sticky `data_available` flag,
PNMI `count == 0` for p2h_space, io_pending wakeup on R3 reads, and
spin-wait in `dequeue_r3_p2h` when M flag is set. CE2023 and Tube SAVE
tests both pass.

### Integration test infrastructure (commit 811cf12)
`integration_tests/tube-save/` — reproduces and verifies the fix. Tests
LOAD/SAVE round-trip fidelity with and without Tube.

### adfs-disc-tools package (commit e435373)
`scripts/adfs-disc-tools/` — standalone uv package with CLI and bundled
BBCHDDs.zip. Creates blank ADFS SCSI images:
```bash
cd scripts/adfs-disc-tools && uv run adfs-disc-tools create-image 8 /tmp/scsi.dat
```

### L3FS documentation (commit 4eb7886)
`docs/level-3-file-server-setup.md` — comprehensive guide covering ROM
configuration, command line, SCSI disc provisioning, and automated
testing path.

### WFSINIT extraction
`scripts/wfsinit/` contains a clean extraction of WFSINIT v0.90
(Acorn Computers plc, 1985):

- `wfsinit.ssd` — DFS image saved via the fixed Tube (clean, verified)
- `WFSINIT` — tokenised BBC BASIC binary (15053 bytes, load=&0800,
  exec=&8023), extracted from the SSD via oaknut-dfs
- `WFSINIT.bas` — detokenised source (497 BASIC lines)

Source: L3FS-ISW.adl (Initial Software disc) from BeebEm disc images.

Note: the "Level 3 FS Utilities Disc v1.06" at
`discs/l3fs/Level 3 FS Utilities Disc v106.dsd` does NOT contain
WFSINIT. It contains user utilities (NETMGR, LogCopy, Archive, etc.)
and Welcome programs. WFSINIT is only on the Initial Software disc.

### Blank 8 MB SCSI image
`tests/assets/scsi/blank-8mb.dat` + `.dsc` — ADFS-formatted, ready for
WFSINIT initialisation.

### WFSINIT runs to completion (commit d806300)
Fixed Tube R3 paired transfer synchronisation which caused WFSINIT to
hang during the "Please wait" phase. WFSINIT now successfully:
- Reads disc geometry via OSWORD &72
- Shrinks the ADFS partition
- Writes per-cylinder bitmaps
- Creates the root directory and password file
- End-to-end completion verified by integration test

### Blank SCSI image creation via oaknut-dfs
`scripts/adfs-disc-tools/` replaced by oaknut-dfs for creating blank
ADFS SCSI images of any size.

---

## Broader Forward Plan

### Phase 1: Understand and fix WFSINIT -- COMPLETE
WFSINIT analysis is documented in `scripts/wfsinit/wfsinit.md`.
The emulation fixes (Tube R3 synchronisation) resolved the stall.
WFSINIT runs to completion and successfully partitions the disc.

### Phase 2: Populate the disc and start the fileserver

The goal is to produce a fully initialised SCSI disc image containing:
the fileserver executable on the ADFS partition, and the Library/Utils
files on the AFS partition. Then boot the fileserver.

#### Step 2a: Place the fileserver on ADFS before partitioning

The fileserver executable must be on the ADFS partition *before*
WFSINIT runs, because WFSINIT shrinks the ADFS partition and the
L3FS partition is not accessible via ADFS tools afterwards. Use
oaknut-dfs or direct sector writes to place the FS binary at a known
location on the ADFS disc (e.g. `$.!Boot` or `$.FS3v126`).

Note: WFSINIT v0.90 has commented-out code (lines 460-550) to copy a
fileserver binary from floppy to `$.!Boot` on the SCSI disc, but this
is skipped via `GOTO 560`. The binary must be placed manually before
partitioning.

#### Step 2b: Extract library/utility files from the PiEB archive

The source files are in `discs/l3fs/libraries/econet-fs.tar`, intended
for PiEconetBridge. Extract preserving the PiEB xattr metadata:

```bash
tar -xvf econet-fs.tar --xattrs
```

Each file carries xattr metadata encoding Acorn file attributes:

| xattr key          | Meaning                          | Example      |
|--------------------|----------------------------------|--------------|
| `user.econet_load` | Load address (hex, 32-bit)       | `FFFF0E23`   |
| `user.econet_exec` | Exec address (hex, 32-bit)       | `FFFF0E23`   |
| `user.econet_perm` | Access permissions (hex)         | `15`         |
| `user.econet_birth`| Creation date (packed)           | `4AD3185028` |
| `user.econet_owner`| Owner ID                         | `0000`       |
| `user.econet_homeof`| Home-of field                   | `0000`       |

The archive contains four directories:

| Directory   | Contents                                              |
|-------------|-------------------------------------------------------|
| Library     | 21 utilities (CLOSE, Date, Discs, FS, Free, etc.)     |
| Library1    | 12 utilities (Bas128, BasObj, Set, View, etc.)        |
| ArthurLib   | 3 utilities (SetFree, SetStation, Users)              |
| Utils       | 3 utilities (CopyFiles, SetStation, TreeCopy v1.63a)  |

#### Step 2c: Copy files to ADFS floppy disc images (new oaknut-dfs capability)

oaknut-dfs currently supports creating blank images and extracting
files. A new capability is needed: writing files *to* a DFS/ADFS disc
image while preserving metadata (load address, exec address, access
permissions) read from the PiEB xattrs.

This is the bridge between the host filesystem (with xattr metadata)
and the BBC Micro world (DFS catalogue entries with load/exec/length).
The files need to be on floppy disc images that can be mounted in
Beebium's floppy drives.

#### Step 2d: Copy files from DFS floppy to AFS via TreeCopy

With the fileserver running (see Step 2f), use TreeCopy (v1.63a,
included in the archive at `Utils/TreeCopy`) to copy files from
ADFS floppy to the NFS partition:

- TreeCopy is a Level 3 utility that copies directory trees between
  filing systems, preserving access permissions and directory structure
- Reference: https://stardot.org.uk/forums/viewtopic.php?t=4907
- The workflow: mount the ADFS floppy in Beebium, log in as SYST on
  the fileserver, and use TreeCopy to copy from the floppy (filing
  system 4/8) to NFS (filing system 5)
- The Library, Library1, ArthurLib, and Utils directories need to be
  copied to the appropriate locations on the AFS partition

#### Step 2e: Configure user filesystem quotas

WFSINIT sets a default free-space allocation of &40404 bytes (~256 KB)
for every user including SYST. This is very restrictive, especially
for large discs. After initialisation:

- Use `*SETFREE <user> <space in hex>` to increase quotas
- The SYST user in particular needs a generous allocation to hold the
  Library and Utils directories
- Quota values are stored in the password file (bytes 26-29 of each
  31-byte entry, LSB first)
- Investigate whether quotas can be patched directly in the password
  file sector on the disc image, or whether they must be set via the
  running fileserver

#### Step 2f: Boot the fileserver

Once the SCSI disc is initialised with WFSINIT and the fileserver
binary is on the ADFS partition:

1. Boot with the initialised SCSI image on HDD 0
2. Load the fileserver (from ADFS or floppy) — transfers to 65C02
   parasite
3. L3FS reads the disc bitmap, prompts for drives/stations
4. Displays "Starting - Ready"

The fileserver must be running before Step 2d (TreeCopy) can proceed.

### Phase 2 (alternative): Direct AFS support in oaknut-dfs

Rather than relying on WFSINIT running inside the emulator, oaknut-dfs
could construct AFS partitions directly. This would allow building a
fully initialised SCSI disc image -- ADFS partition with fileserver
binary, AFS partition with Library/Utils files and password file -- in
a single host-side tool invocation, with no emulator in the loop.

The AFS0 on-disc format is fully documented in `scripts/wfsinit/wfsinit.md`
(disc info blocks, per-cylinder bitmaps, JesMap extent chains, directory
linked lists, password file format). This is a self-contained specification
sufficient to implement a writer.

This approach would also eliminate the TreeCopy step (2d) since files
could be written directly to AFS sectors with correct metadata, and
would make quota configuration (2e) trivial by setting password file
entries at image creation time.

Under investigation.

### Phase 3: Automated Econet testing
1. Launch L3FS server instance
2. Launch client instance(s) with `--station N --aun-port 10N`
3. Exercise NFS operations: `*I AM SYST`, `*CAT`, file load/save
4. Verify via gRPC screen reading

---

## Key Files

| File | Purpose |
|------|---------|
| `scripts/wfsinit/WFSINIT.bas` | Detokenised WFSINIT source (start here) |
| `scripts/wfsinit/WFSINIT` | Tokenised binary (15053 bytes) |
| `scripts/wfsinit/wfsinit.ssd` | Clean DFS image with WFSINIT |
| `docs/level-3-file-server-setup.md` | Comprehensive setup guide |
| `scripts/adfs-disc-tools/` | Blank ADFS image creation tool |
| `tests/assets/scsi/blank-8mb.dat` | Blank 8 MB ADFS SCSI image |
| `tests/assets/scsi/blank-8mb.dsc` | Geometry sidecar for above |
| `src/extensions/acorn-scsi/` | SCSI controller emulation |
| `integration_tests/tube-save/` | Tube SAVE round-trip tests |
| `discs/l3fs/libraries/econet-fs.tar` | Library/Utils files with PiEB xattr metadata |
| `discs/l3fs/libraries/` | Extracted archive (Library, Library1, ArthurLib, Utils) |

## Key External Resources

| Resource | Location |
|----------|----------|
| L3FS v1.26 source | `/Users/rjs/Code/L3V126/` |
| L3FS boot disc | `/Users/rjs/Code/L3V126/FS3v126.ssd` |
| L3FS-ISW.adl (WFSINIT source) | `/Users/rjs/Code/beebem-windows/UserData/DiscIms/L3FS-ISW.adl` |
| L3FS Utilities v1.06 | `discs/l3fs/Level 3 FS Utilities Disc v106.dsd` |
| BeebEm disc images | `/Users/rjs/Code/beebem-windows/UserData/DiscIms/` |
| AFS0 format spec | `http://mdfs.net/Docs/Comp/Disk/Format/AFS0` |
| Tube App Note 004 | `docs/datasheets/Tube_Application_Note_004.pdf` |
| ADFS 1.30 disassembly | `/Users/rjs/Code/acornaeology/acorn-adfs/versions/adfs-1.30/` |
| ANFS 4.18 disassembly | `/Users/rjs/Code/acornaeology/acorn-nfs/versions/anfs-4.18/` |
| Tube client disassembly | `/Users/rjs/Code/acornaeology/acorn-6502-tube-client/` |
| BeebMaster L3 setup | `https://www.beebmaster.co.uk/Econet/Level3.html` |
| Stardot L3FS threads | `https://stardot.org.uk/forums/viewtopic.php?t=27782` |
| Stardot TreeCopy thread | `https://stardot.org.uk/forums/viewtopic.php?t=4907` |
