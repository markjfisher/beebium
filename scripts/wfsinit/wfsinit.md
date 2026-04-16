# WFSINIT - Acorn Level 3 File Server Disc Initialiser

## Overview

WFSINIT (version 0.90, copyright Acorn Computers plc 1985) is a BBC BASIC program
that prepares a SCSI hard disc for use with the Acorn Level 3 File Server (L3FS).
It repartitions an existing ADFS hard disc by shrinking the ADFS partition and
creating an L3FS filesystem (referred to as "AFS" internally) in the reclaimed
space. It also sets up the root directory structure, password file, and optionally
copies standard utility and welcome files from floppy disc.

The program is named `WFSInitFul` in its REM header (line 5), suggesting "Winchester
File Server Initialiser (Full version)".

## Requirements

- BBC Micro with a 65C02 second processor (checked at line 70 via OSBYTE &EA)
- ADFS-formatted SCSI hard disc
- Optionally, floppy discs containing the L3FS master files (Welcome, Library, Utils)

**Important:** The disc MUST be fully compacted before running WFSINIT. The program
determines the start of the L3FS partition from the first free sector in the ADFS
free space map. If the free space is fragmented, ADFS data beyond the first free
sector will be overwritten. It is not possible to resize the Econet partition once
a disc has been initialised.

## User Interaction

WFSINIT runs in MODE 7 (teletext) and prompts the user for:

1. **Drive number** - the SCSI drive to initialise
2. **Disc name** - up to 16 characters, no spaces, validated for printable ASCII
3. **Next drive** - the drive number of the next logical drive (used to compute the
   "addition factor" for multi-drive file server configurations)
4. **Date** - in dd/mm/yy format, stored in a packed 16-bit format
5. **Password file (Y/N)** - whether to create the password file with user accounts
6. **User names** - up to 13 user accounts (if password file was requested)
7. **Copy master directories (Y/N)** - whether to copy Welcome, Library, and Utils
   from floppy

## Program Flow

### Phase 1: Initialisation (lines 10-220)

Sets up OS call entry points (OSWORD, OSBYTE, OSFILE, OSGBPB, OSCLI, OSFIND),
allocates memory buffers, and defines constants for the L3FS on-disc data structures:

| Constant     | Value | Meaning                              |
|-------------|-------|--------------------------------------|
| `pssz%`     | &100  | Password file sector size (256 bytes)|
| `drsz%`     | &200  | Directory size (512 bytes)           |
| `bufsze%`   | &900  | File transfer buffer size (2304 bytes)|
| `namelength%`| 20   | Max name length in password entries  |
| `passlength%`| 6    | Password field length                |
| `free%`     | 26    | Offset of free space in password entry|
| `option%`   | 30    | Offset of status byte in password entry|

Directory object entry field offsets (each entry is 26 bytes):

| Field       | Offset | Size    | Content                    |
|------------|--------|---------|----------------------------|
| `objname%` | 2      | 10 bytes| Object name (space-padded) |
| `objload%` | 12     | 4 bytes | Load address               |
| `objexec%` | 16     | 4 bytes | Exec address               |
| `objacc%`  | 20     | 1 byte  | Access byte                |
| `objdate%` | 21     | 2 bytes | Date (packed format)       |
| `objsin%`  | 23     | 3 bytes | System Internal Name (SIN) |
| `objsze%`  | 26     | --      | (stride to next entry)     |

### Phase 2: Drive Geometry (line 360)

`FNdrive()` issues an OSWORD &72 SCSI command to read the drive characteristics.
The result encodes:

- **Cylinder count** (`disccylinders%`) - lower 16 bits
- **Head count** (`hds%`) - bits 16-23
- **Sectors per cylinder** = heads x 33

Each sector is 256 bytes.

### Phase 3: ADFS Partition Shrinking (lines 560-810)

Reads sector 0 (the ADFS free space map) and calculates the new partition
boundaries. The algorithm, step by step:

1. Read the first free sector number from the ADFS free space map (first 3 bytes of
   sector 0). This represents the total number of sectors currently used on the disc.
2. Multiply by 256 to get used space in bytes.
3. Add &4000 (16384 bytes) for ADFS structural overhead (2 sectors for free space
   map + 5 sectors for root directory = 7 sectors, rounded up with margin).
4. Convert back to a sector count: `tfree% = adfsdirsize% + 2 + fssze% / 256`,
   where `adfsdirsize%` is 5 (root directory sectors) and 2 is for the free space
   map sectors.
5. Round up to a whole cylinder boundary: `stcyl% = ceiling(tfree% / sectorspcyl%)`.
   The L3FS partition always starts at the beginning of a new cylinder.

```
fssze% = (first 3 bytes of sector 0) * 256 + &4000
tfree% = 7 + fssze% / 256     (sectors needed for ADFS)
stcyl% = ceiling(tfree% / sectorspcyl%)
```

Two "disc info" sectors are placed at known positions:

```
sec1% = stcyl% * sectorspcyl% + 1    (first info sector)
sec2% = sec1% + sectorspcyl%          (second info sector, one cylinder later)
```

The actual disc addresses of `sec1%` and `sec2%` are always one more than the start
of their respective cylinders, because sector 0 of each cylinder is reserved for
the L3FS free space bitmap.

The ADFS free space map (sector 0) is patched:

| Offset | Value                      | Purpose                              |
|--------|----------------------------|--------------------------------------|
| &F6    | sec1%                      | Pointer to first L3FS info sector    |
| &100   | adfsdiscsize% - old_size   | Adjusted free space in second half   |
| &1F6   | sec2%                      | Pointer to second L3FS info sector   |
| &FC    | stcyl% * sectorspcyl%      | New ADFS disc size (in sectors)      |

The pointers at &F6 and &1F6 are stored in the "reserved bytes" area of sectors 0
and 1 respectively (ADFS stores two 256-byte sectors in the first physical sector).

Both 256-byte halves of sector 0 have their checksums recalculated using the
end-around-carry algorithm (`FNchecksum`) and the modified sector 0 is written
back to disc.

### Phase 4: Bitmap Initialisation (`PROCinitmaps`, lines 3520-3640)

The L3FS uses a per-cylinder sector bitmap to track free space. The first sector
(sector 0) of each cylinder in the L3FS partition is given over to a bitmap of the
sectors in that cylinder.

Each sector is allocated a single bit:
- Bit = 1 means the sector is free
- Bit = 0 means the sector is allocated

Byte 0 of the bitmap covers the first 8 sectors (bit 0 = sector 0, bit 7 =
sector 7), byte 1 covers sectors 8-15, and so on. A single 256-byte bitmap can
track up to 2048 sectors per cylinder.

For each cylinder from `stcyl%` to the end of the disc:
- Bit 0 (sector 0, the bitmap itself) is always 0 (allocated)
- All other sectors (1 through sectorspcyl%-1) are initially marked free (bit = 1)
- The bitmap is written to sector 0 of the cylinder

A **cylinder map** (`cymap%`) is maintained in memory during initialisation:

- Bytes 0-2: total free sector count across all cylinders (24-bit little-endian)
- Then 2 bytes per cylinder: free sector count for that cylinder (16-bit LE)

### Phase 5: Reserve Info Sectors (`PROCfreeinfospace`, lines 3660-3800)

The two disc info sectors (`sec1%` and `sec2%`) are marked as allocated by clearing
their respective bits in their cylinder bitmaps, and the cylinder map counts are
decremented.

### Phase 6: Write Disc Info Blocks (lines 820-980)

A 256-byte disc information block is constructed with the following layout:

| Offset | Size   | Field           | Content                                   |
|--------|--------|----------------|-------------------------------------------|
| 0-3    | 4      | `fourchars%`   | Magic identifier "AFS0"                   |
| 4-19   | 16     | `dname%`       | Disc name (space-padded)                  |
| 20-21  | 2      | `nocyls%`      | Number of cylinders on the disc           |
| 22-24  | 3      | `nosecs%`      | Total number of sectors on the disc       |
| 25     | 1      | `ndscs%`       | Number of physical discs (always 1)       |
| 26-27  | 2      | `secpcyl%`     | Sectors per cylinder                      |
| 28     | 1      | `szobtmp%`     | Sectors per bitmap (always 1)             |
| 29     | 1      | `addfact%`     | Next drive addition factor                |
| 30     | 1      | `drinc%`       | Drive increment (always 1)                |
| 31-33  | 3      | `TheSinOfroot%`| SIN of root directory                     |
| 34-35  | 2      | `date%`        | Creation date (packed format)             |
| 36-37  | 2      | `startcyl%`    | First cylinder of L3FS partition          |

All multi-byte values are stored least significant byte first.

This block is written identically to both `sec1%` and `sec2%` for redundancy.

The root directory SIN stored is `root% - 1` (i.e. one less than the value returned
by `FNablk`), because `FNablk` returns the SIN + 1. The SIN points to the map sector,
and the data starts in the sector after the map sector.

### Phase 7: Root Directory and Password File (`PROCsetup`, lines 2060-2350)

**Root directory creation:**
- Allocates a 512-byte (2 sector) directory block via `FNablk(drsz%)`
- Creates the "$" root directory with `PROCmake_dir`

**Directory structure** (default 512 bytes, 2 sectors):

| Offset | Size   | Content                                              |
|--------|--------|------------------------------------------------------|
| 0-1    | 2      | Pointer to first entry in directory (in-use list head)|
| 2      | 1      | Master Sequence Number                               |
| 3-12   | 10     | Directory name (space-padded)                        |
| 13-14  | 2      | Pointer to first free entry (free list head)         |
| 15-16  | 2      | Number of entries in the directory                   |
| 17+    | 26 each| Directory entries (linked list nodes)                |
| last   | 1      | Copy of Master Sequence Number (must match byte 2)   |

Directories may hold up to 255 entries. The default 2-sector (512 byte) directory
is sufficient for 19 entries. The size formula is: 18 + (number_of_entries x 26)
bytes.

The directory uses two linked lists: one for in-use entries (maintained in
alphabetical order by the pointer chain) and one for free entry slots. New entries
are allocated from the free list and spliced into the in-use list in sorted position.

`PROCmake_dir` (lines 2710-2760) initialises the free list by iterating forward
through slots from offset &11 to &1E5 in steps of 26 (`objsze%`), setting each
slot's next pointer to the previous slot's offset. The free list head (`freep%`,
at offset 13) is set to &1E5 — the **highest** slot offset. This means the free
list chains from high to low: &1E5 → &1CB → &1B1 → ... → &2B → &11 → 0. Entries
are therefore popped from the high end first, so the first created entry occupies
the last physical slot, the second entry the penultimate slot, and so on.

If the Master Sequence Number at byte 2 does not match the copy at the final byte
of the directory, the file server reports "Broken Directory" (FS Error 42).

**Password file creation** (optional):
- Creates a "Syst" entry with system privilege flag (&40 in status byte)
- Adds "Boot" and "Welcome" as password-only entries (from DATA at line 3930);
  these are user accounts, **not** directories — no URDs are created for them
- Prompts for up to 13 user names, validating each:
  - Must start with a letter
  - Remaining characters must be alphanumeric, `!`, or `-`
  - Maximum 10 characters
- For each interactively-entered user (lines 2270-2310):
  1. Allocates a User Root Directory (URD) via `FNablk(drsz%)` — 2 sectors
  2. Creates the directory with `PROCmake_dir(name$, block1%, dir%)`
  3. Adds the directory to root `$` with `PROCenter_dir(name$, &30, ...)` — access
     &30 = directory + locked
  4. Adds a password entry with `FNenter_name`

  Note: only interactively-entered users get URDs. The three built-in accounts
  (Syst, Boot, Welcome) are password-only — they exist in the passwords file but
  have no corresponding directory in `$`.

**Password entry format** (31 bytes per entry):

| Offset | Size | Content                                                     |
|--------|------|-------------------------------------------------------------|
| 0-19   | 20   | User identifier (up to 10 chars, terminated with &0D)       |
| 20-25  | 6    | Password (up to 6 chars, terminated with &0D if shorter)    |
| 26-29  | 4    | Free space allocation for user (LSB first)                  |
| 30     | 1    | Status byte                                                 |

**Status byte:**

| Bit | Value | Meaning                                    |
|-----|-------|--------------------------------------------|
| 7   | &80   | User ID in use (if clear, entry is ignored) |
| 6   | &40   | System privilege                            |
| 5   | &20   | Privileged user                             |
| 1-0 | 0-3   | Boot option: 0=off, 1=load, 2=run, 3=exec  |

WFSINIT initialises the free space allocation to &40404 (263,172 bytes, approximately
256 KB) for all users including Syst. This is notably restrictive for large discs --
the Syst user can adjust individual allocations later using
`*SETFREE <user> <space in hex>`.

The password file is allocated as a single 256-byte sector (sufficient for
approximately 8 users). The file length must always be a complete number of sectors.
The "Passwords" entry is added to the root directory with access byte &0 (not
directly accessible as a regular file).

### Phase 8: File Copying (`PROCcopyfiles`, lines 4070-4330)

If the user chooses to copy master directories, three directories are created and
populated from floppy disc:

| L3FS Directory | Source  | Contents                                              |
|---------------|---------|-------------------------------------------------------|
| Welcome       | :0.$.W  | Alpha, Batball, Biorthm, Bpart2, Calc, Clock, Help, Index, Keybd, Kingdom, Message, Music, Pattern, Phone, Photo, Poem, Sketch, Welcome |
| Library       | :2.$.L  | Close, Discs, Flip, Free, Fs, Lcat, Lex, Notify, Prot, Ps, RdFree, Remote, SetFree, Unprot, Users, View |
| Utils         | :0.$.U  | Archive, L2to3, Netmgr, GetBack, Init, LogCopy, SetTime |

For each file, the sequence is:
1. Switch to filing system 4 (DFS/floppy) via `PROCfrom`
2. Read file catalogue info with OSFILE 5 (load address, exec address, size)
3. Switch to filing system 8 (ADFS/SCSI) via `PROCto`
4. Allocate L3FS sectors for the file with `FNablk(hsz%)`
5. Create a directory entry with `PROCenter_dir`
6. Transfer data with `PROCtransfile`, which reads from floppy via OSGBPB 4 in
   chunks of `bufsze%` (2304 bytes) and writes to L3FS sectors via OSWORD &72

Filing system switching is done via OSBYTE &8F with service call &12 (select
filing system): value 4 = DFS, value 8 = ADFS.

**Missing file handling (bug):** `PROCtransfile` (line 4370) handles missing files
gracefully -- if `FNopen` returns 0 it prints "File <name> not found" and returns
without error. However, steps 2-5 above have already executed unconditionally
before `PROCtransfile` is called. The OSFILE 5 call at line 4210 does not check its
return value (A% = 0 means "not found"), so if a file is missing, `hla%`, `hea%`,
and `hsz%` retain stale values from the previous file's OSFILE call. The code then
allocates disc space based on that stale size and creates a directory entry with
the wrong load/exec addresses. The result is a phantom directory entry that consumes
disc space but contains no data. The program does not crash or abort, but the L3FS
directory is left in an inconsistent state for that entry.

## Key Data Structures

### System Internal Name (SIN)

Each object on the L3FS partition is identified by a System Internal Name, which is
a 3-byte (24-bit) disc address pointing to the object's **map sector** (not the
start of the data itself). The map sector describes where the file's data sectors
are located on disc.

### Map Sector ("JesMap")

Files on L3FS do not need to be contiguous. Each file has a map sector that records
the locations of its data extents. The map sector format (256 bytes):

| Offset | Size   | Content                                              |
|--------|--------|------------------------------------------------------|
| 0-5    | 6      | Magic string "JesMap"                                |
| 6      | 1      | Map sequence number                                  |
| 7      | 1      | Unused                                               |
| 8      | 1      | LSB of object size (bytes used in the last sector)   |
| 9      | 1      | Unused                                               |
| 10-12  | 3      | Start sector of first data extent (LSB first)        |
| 13-14  | 2      | Number of sectors in first extent                    |
| 15-17  | 3      | Start sector of second data extent                   |
| 18-19  | 2      | Number of sectors in second extent                   |
| ...    | 5 each | Further extents (groups of 5 bytes)                  |
| 255    | 1      | Copy of map sequence number (must match byte 6)      |

If byte 6 and byte 255 do not match, the file server reports "Broken Directory"
(FS Error 42). WFSINIT always sets the sequence number to 0.

The extent chain continues until a zero start sector is encountered. Each extent
is a (start_sector, length) pair giving a contiguous run of data sectors.

### Directory Entry

Each 26-byte directory entry contains:

| Offset | Size | Content                                         |
|--------|------|-------------------------------------------------|
| 0-1    | 2    | Pointer to next entry (0 = end of list)         |
| 2-11   | 10   | Object name (space-padded)                      |
| 12-15  | 4    | Load address (LSB first)                        |
| 16-19  | 4    | Execution address (LSB first)                   |
| 20     | 1    | Access byte                                     |
| 21-22  | 2    | Date (packed format)                            |
| 23-25  | 3    | System Internal Name (SIN)                      |

**Access byte:**

| Bit | Value | Meaning      |
|-----|-------|-------------|
| 5   | &20   | Directory    |
| 4   | &10   | Locked       |
| 3   | &08   | Owner write  |
| 2   | &04   | Owner read   |
| 1   | &02   | Public write |
| 0   | &01   | Public read  |

In WFSINIT, directories are created with access &30 (directory + locked) and
files with access &15 (locked + owner read + public read).

## Key Algorithms

### Block Allocation (`FNablk`, lines 1650-1930)

Allocates sectors for a file or directory, potentially spanning multiple cylinders:

1. Scans the cylinder map to find the cylinder with the most free space
2. Finds the first free bit in that cylinder's bitmap
3. Allocates the first sector as the **map sector** (JesMap)
4. Allocates subsequent contiguous sectors from the same cylinder
5. If more sectors are needed, moves to the next cylinder with free space (`PROCgetnext`)
6. Records each contiguous extent as a 5-byte (start SIN, length) pair in the map sector

The function returns `sin% + 1` (the map sector address + 1), so the SIN stored in
directory entries and the disc info block points to the map sector itself.

### Directory Entry Insertion (`PROCenter_dir`, lines 2790-2990)

Entries are maintained in a sorted linked list within the directory. Insertion:

1. Walks the in-use list using `FNcompare` to find the alphabetical insertion point
2. Removes an entry slot from the free list
3. Splices the new entry into the in-use list at the correct position
4. Fills in name, load/exec addresses, access, date, and SIN
5. Increments the object count

### String Comparison (`FNcompare`, lines 2480-2670)

Case-insensitive comparison used for sorted directory insertion. Converts both
strings to uppercase (AND &DF) and compares character by character. Space characters
in the on-disc name are treated as CR (string terminator). Returns TRUE if the
first string sorts before the second.

### Date Encoding (`FNget_date_User`, lines 3070-3330)

Packs a dd/mm/yy date into a 16-bit value:

```
encoded = ((year - 81) * 4096) + (month * 256) + day + ((year - 81) AND &F0) * 2
```

Year values less than 81 have 100 added (handling years 2000+). The base year is
1981, consistent with the Econet file server date epoch.

### ADFS Checksum (`FNchecksum`, lines 3420-3500)

Standard ADFS 255-byte end-around-carry checksum. Starting from &FF, iterates
backwards through bytes 254 to 0, accumulating with carry wraparound. The checksum
byte is stored at offset 255 of each 256-byte half of sector 0.

## SCSI Disc Access

All raw disc I/O uses OSWORD &72, the ADFS SCSI interface. The transfer control
block (`trcb%`) is structured as:

| Offset | Field         | Content                                   |
|--------|--------------|-------------------------------------------|
| 0      | Result       | 0 on entry, error code on return          |
| 1-4    | Address      | Memory address for data transfer (32-bit) |
| 5      | Command      | 8 = read, &A = write                      |
| 6      | Address high | High sector bits OR (drive * 32)          |
| 7-8    | Address mid/low | Sector address (big-endian)            |
| 9-10   | Padding      | Zero                                      |
| 11-14  | Length       | Transfer length in bytes (32-bit)         |

The sector address is 24-bit, with the drive number encoded in the upper 3 bits of
the high byte (bits 5-7), allowing drives 0-7.

## On-Disc Layout After Initialisation

```
Cylinder 0                          ADFS partition
  ...                               (shrunk to stcyl% cylinders)
Cylinder stcyl%-1                   End of ADFS partition
Cylinder stcyl%, sector 0           L3FS bitmap for this cylinder
Cylinder stcyl%, sector 1 (=sec1%)  L3FS disc info block (copy 1)
Cylinder stcyl%, sector 2+          L3FS data (root dir map, root dir, etc.)
Cylinder stcyl%+1, sector 0         L3FS bitmap for this cylinder
Cylinder stcyl%+1, sector 1 (=sec2%) L3FS disc info block (copy 2)
Cylinder stcyl%+1, sector 2+        L3FS data
  ...
Last cylinder                       L3FS data
```

Each cylinder's sector 0 contains the free-space bitmap for that cylinder.
The disc info block is stored redundantly at `sec1%` and `sec2%` (in different
cylinders) for fault tolerance.

## Bootable File Server Option

Lines 460-550 contain code to make the disc bootable by copying the file server
binary (`fs` from drive :0) to `$.!Boot` on the target disc. However, line 460
contains `GOTO 560`, skipping this code entirely in this version. The comment reads
"Don't make bootable".

## Multi-Drive Configurations

The "addition factor" (`addfact%`) and "drive increment" (`drinc%`) fields in the
disc info block support multi-drive file server configurations where the L3FS
spans multiple physical SCSI drives. The addition factor is the difference between
the "next drive" number and the current drive number. The drive increment is
always set to 1 in this version.

## Allocation Strategy

`FNablk` (lines 1650-1930) combines map-block and data-sector allocation in a
single pass. It picks the cylinder with the most free space (`FNDCY`-equivalent
scan at lines 1720-1740), allocates the first free sector as the JesMap map block,
then allocates contiguous data sectors from the **same cylinder**. Only if more
sectors are needed does it spill to the next-best cylinder via `PROCgetnext`.

This means every object's map block and data start on the same cylinder, and each
new object (root directory, user home directory, passwords file) goes on a
**different** cylinder — whichever has the most free space at allocation time.
For a freshly-initialised disc with two users, the typical layout is:

| AFS Cylinder | Object                      |
|-------------|-----------------------------|
| 0           | Info sector copy 1          |
| 1           | Info sector copy 2          |
| 2           | Root directory (map + data)  |
| 3           | First user's home directory |
| 4           | Second user's home directory|
| 5           | Passwords file (map + data) |

This distribution is deliberate: each user's home directory starts on a cylinder
with nearly all sectors free, giving maximum room for in-place growth before
fragmentation occurs. The L3FS server's `FNDCY` allocator (Uade11:916) then
naturally keeps each user's files co-located with their URD.

## Password File Sizing

The password file is allocated as exactly one sector via `FNablk(pssz%)` where
`pssz% = &100 = 256` (line 80). This means `BILB = 256 MOD 256 = 0` in the
JesMap map block, indicating all 256 bytes of the sector are meaningful. A
single 256-byte sector can hold 8 complete 31-byte password entries with 8 bytes
of trailing padding (256 / 31 = 8 remainder 8). The L3FS server reads as many
complete entries as fit and ignores the trailing fragment.

## Known Quirks

- **DIM inside a procedure**: `FNopen` (line 4920) contains `DIM name%-1` which
  allocates memory from the heap on every call but never frees it. This is a
  common BBC BASIC pattern but wastes memory if called repeatedly.
- **GOTO between DEFs**: `PROCread` and `PROCwrite` share common code via a GOTO
  at line 1070 that jumps into the middle of `PROCwrite`.
- **Restrictive free space**: The default &40404 byte (~256 KB) allocation per
  user is very limiting on large discs. Later versions of the file server addressed
  this.
- **No backup path**: The L3FS partition cannot be backed up using ADFS utilities
  (e.g. CFBackup). Backups must be done over the network from the file server.
- **Stray CR in directory names (length ≥ 8)**: `PROCmake_dir` (line 2720) writes
  the directory name using BBC BASIC's `$` string indirection:
  `$(dir%+3)=name$+"          "`. This writes the name + 10 spaces + a CR (&0D)
  terminator, placing the CR at byte offset `13 + LEN(name$)`. For names of
  **7 characters or fewer**, the CR falls at offset ≤ 20 and is overwritten by
  the free-list initialisation at line 2740 (`dir%!&11=j%` writes 4 bytes at
  offsets 17–20, zeroing the CR). For names of **8 characters or more**, the CR
  falls at offset ≥ 21, in the gap between the first slot's 4-byte link-pointer
  write (17–20) and the second slot's start (43), so it **persists** in the first
  entry slot's name field. The affected byte is at offset `name[N-8]` within the
  first entry slot (e.g. "MORIARTY" → `name[2]` at absolute offset 21). Since the
  slot is free-listed and the server ignores name bytes in free slots, this has no
  functional effect — but it is a detectable fingerprint of WFSINIT-created
  directories. Line 2720's second statement (`dir%?(3+LEN(name$))=SPACE%`) only
  overwrites the byte at the end of the actual name, not the CR that BASIC placed
  after the space padding.
- **No pre-compaction**: WFSINIT does not compact the ADFS partition before
  repartitioning. It reads the existing free space map and places the L3FS
  partition at the first cylinder boundary beyond the used data. The manual states
  that the disc "MUST bee fully compacted before running WFSINIT"; if it is not,
  ADFS data beyond the first free sector will be silently overwritten. Line 270
  contains a commented-out call to compact: `REPEAT:REMPROCoscli("Compact 30 4C")`.
- **Padding in partition boundary**: The formula for computing the ADFS/AFS boundary
  (lines 570-710) adds &4000 (16 KB = 64 sectors) of structural padding beyond the
  ADFS used space, plus 7 overhead sectors (5 root directory + 2 free space map).
  This is generous — the actual ADFS structural overhead is well under 64 sectors —
  but ensures the AFS region never encroaches on ADFS metadata even on discs where
  compaction left small gaps.

## References

- "Understanding the Acorn Level 3 File Server Structure" by ISW (2010) --
  detailed reverse-engineering of the on-disc format
- BeebMaster Level 3 File Server guide (beebmaster.co.uk/Econet/Level3.html)
- Stardot forum discussions on L3FS capacity and WFSINIT versions
