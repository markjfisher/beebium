RTC and Timekeeping in the Acorn File Servers
==============================================

Three Timekeeping Modes
-----------------------

The file server has a compile-time flag `Dongle` (`Uade01` line 59) that selects
between two fundamentally different timekeeping strategies:

| `Dongle` value | Meaning              | Timekeeping method                                                      |
|----------------|----------------------|-------------------------------------------------------------------------|
| `0`            | RTC dongle *present* | SAF3019P on User Port, bit-banged via OSBYTE 150/151                    |
| `1`            | RTC dongle *absent*  | Manual date/time entry at startup; MOS OSWORD 1/2 timer for time-of-day |

The FileStore (CMOS/65C102 path, `$CMOS`) uses a third method: the onboard
146818 RTC chip accessed via MOS OSWORD 1 and 2 calls (`Getdte`/`Setdte` at
`Uade04` lines 385-392). The 146818 has its own year counter, so no software
tricks are needed.

At boot, the Level 3 path in `STRTFS` (`Uade04` line 50) branches on the
`DONGLE` flag (`Uade04` lines 65-77): if the dongle is expected, `JSR RTC`
tests for its presence and halts with "Clock Failure" if absent; otherwise
`JSR STDATE` prompts the operator for a manual date entry.


Initial Year Setting: The First-Use Problem
--------------------------------------------

Since the SAF3019P has no year counter, and its alarm registers (used to store
the year) power up in an undefined state, the question arises: how was the year
set for the very first time?

### No Local Date Prompt with Dongle Present

When assembled with `DONGLE=0`, the boot sequence (`ASKDAT`, `Uade04` lines
64-78) is:

1. `JSR RTC` -- test dongle presence
2. If present, fall through to `RDTIME` (line 78)
3. `RDTIME` when `DONGLE=0` (`Uade04` line 815) is simply a label that falls
   through to `PRDTE`, which calls `RDDONG` to read the dongle and `SCDATE`
   to display the result
4. Continue to "Number of drives:" prompt, then start the file server

There is **no "Date (DD/MM/YY) =" prompt** in this path. That prompt exists
only in `STDATE` (`Uade04` lines 294-344), which is assembled exclusively when
`DONGLE=1` (dongle absent). The `A` option at the boot menu
(`Command: S, A, * ONLY`) re-enters `ASKDAT`, but with `DONGLE=0` this simply
re-reads the dongle and redisplays -- it does not prompt for a date either.

The dongle path trusts unconditionally that alarm register 1 already contains a
valid year. There is no range check, no "is this a sensible year?" validation,
and no fallback to a manual prompt. (Contrast with the `Thisyr` constant at
`Uade01` line 271, which rejects years before 1985 -- but this check appears
only in `STDATE`, the manual-entry path.)

### Setting the Year Over the Network

The only mechanism available to set the dongle's year is **Econet function code
28** (`CPSETD`), a network command sent from a privileged client station. This
command is present in all versions from v1.06 onwards (originally at `Uade17`
line 1085 with the comment `** 20/9/83 **`, later moved to `Uade17A` line 539
in the v1.33 shared codebase).

The client sends a 5-byte date/time packet:

```
Byte 0: Day of month (with year high bits in &E0, in 7-bit versions)
Byte 1: Year-offset/month (upper nibble = year, lower nibble = month)
Byte 2: Hours
Byte 3: Minutes
Byte 4: Seconds
```

The `CPSETD` handler (`Uade17A` lines 539-583 in v1.33):

1. Checks the caller is logged on and has system privilege (`Uade17A` lines
   540-545)
2. Saves the current date (in case validation fails)
3. Copies the received date bytes into `DATE` and `DATE+1`
4. Calls `CHKDTE` to validate
5. Range-checks hours (< 24), minutes (< 60), and seconds (< 60)
6. On success, calls `SETTME` (which writes all values to the dongle,
   including the year to alarm register 1) and `PRDTE` (to update the display)
7. On failure, restores the saved date and returns error code `DTERR`

There is also a corresponding read command, **function code 16** (`CPDATE`),
which returns the current date and time to the requesting client.

### The Installation Scenario

On a fresh system with a new dongle (or after a dongle battery replacement),
alarm register 1 would contain an undefined value. The file server would read
this garbage, interpret it as a year offset, and display a nonsensical date.
The server would still start and operate -- the date would simply be wrong.

To correct this, a privileged user on a client station would need to run a
program that sends function code 28 with the correct date. The README mentions
that the installation process involved an "install routine" that applied the
serial number when copying the binary to disc; it is plausible that a similar
installation or first-run utility also set the initial date via function code
28, though no such utility survives in the source repository.

Alternatively, an operator could use the `*` boot menu option to drop to the OS
command line and manually poke the dongle registers using OSBYTE 150/151, but
this would require knowledge of the bit-bang protocol and register layout -- not
a designed workflow.

In practice, the dongle's 1.5V battery backup meant that once the year was set,
it would persist across power cycles indefinitely (the SAF3019P draws only a few
microamps in battery mode). The year only needed to be set once -- at
installation -- and the `REDFIX` month-comparison logic would handle subsequent
year rollovers automatically. A fresh dongle or dead battery would be the
exceptional case requiring re-initialisation.


How the Year Was Tracked Without a Year Counter
------------------------------------------------

This is the central cleverness. As confirmed by the SAF3019P datasheet, the
chip's time counter provides only **minutes, hours, day-of-month, and month** --
no year. But it also has a **time register** (intended for alarm or remote
switching), which is a 24-bit memory accessible via the same CBUS interface.
The file server repurposes these alarm registers as general-purpose RAM.

The comment block at `Uade04` lines 986-1003 documents the register allocation:

```
; TIME SET REGISTERS (the chip's actual counters):
;     0=MONTH      2=DATE    4=HOURS     6=MINS

; RAM REGISTERS (alarm registers, repurposed):
;  1=YEAR in lower 7 bits
;  3=OLDMONTH and bit &10 to indicate LEAP pending
;  5=Not Used
;  7=Used to determine presence of chip
```

**The year is stored in alarm register 1** as a 7-bit value. This value is the
offset from `Baseyr` (81, i.e. 1981, defined at `Uade01` line 270). The DATE
memory area encodes both the month (low nibble of `DATE+1`) and year (high
nibble of `DATE+1` plus 3 high bits of `DATE`), giving a 7-bit year field
total and a range of 1981-2108.


### Year Encoding in Memory

The `SETYR` routine (`Uade04` lines 973-984) splits the 7-bit year offset
across two bytes:

```
SETYR ASLA          ; year * 2
 STA Rem
 ASLA
 ASLA
 ASLA              ; year << 4 (bits 3-0 of year into high nibble of DATE+1)
 ORA DATE+1
 STA DATE+1
 LDA Rem
 ANDIM &E0          ; bits 6-4 of year into top 3 bits of DATE
 ORA Date
 STA Date
 RTS
```

- **Bits 0-3** of the year occupy the upper nibble of `DATE+1`
- **Bits 4-6** of the year occupy the upper 3 bits of `DATE` (masked `&E0`)
- The lower nibble of `DATE+1` holds the month (1-12)
- The lower 5 bits of `DATE` hold the day-of-month (1-31)

The `YR` display routine (`Uade04` lines 853-878) reverses the process:

```
YR  LDA Date        ; get high 3 bits of year from DATE
    ANDIM &E0
    STA Rem
    LDA DATE+1
    LSRA
    LSRA
    LSRA            ; shift year nibble down
    ORA Rem         ; combine with high bits
    LSRA            ; final shift to form 7-bit year offset
    CLC
    ADCIM BASEYR    ; add 81 to get actual 2-digit year
```

Century display logic follows: if the result is >= 100, subtract 100 and print
"20"; otherwise print "19".


### Year Rollover Detection

Since the SAF3019P's month counter wraps from 12 back to 1 autonomously (it
handles 28/29/30/31-day months internally per the datasheet's Table 1), but has
no year counter, the software must **detect** when a year boundary has been
crossed.

The solution: store the **old month** in alarm register 3, then on each read,
compare the current month against it.


#### On Write: `SETMFX` (`Uade04` lines 1044-1069)

Called from `SETTME` (`Uade04` line 1025) whenever the dongle is written:

- The current month is stored in the low nibble of register 3
- Bit 4 (`&10`) is set as a "leap year pending" flag if the current year is a
  leap year AND we are before February 29th (January, or February with day < 29)

The leap year test (`Uade04` lines 1058-1061) is:

```
 CLC
 ADCIM :LSB:(BASEYR*16)    ; add base year offset scaled to nibble position
 ANDIM 3*16                 ; test divisibility by 4 (in nibble position)
 BNE LHNOF                  ; not leap: clear the flag
```

If it is a leap year and we are in the danger zone (before Feb 29), the flag
is set by OR'ing `&10` into the stored month value (`Uade04` line 1064).


#### On Read: `REDFIX` (`Uade04` lines 1161-1225)

Called from `RDDONG` after reading all dongle registers (`Uade04` line 1156).
At this point:

- `DATE+1` has the perceived month (low nibble) and year (high nibble)
- `OTIME+2` has the old month and leap flag (read from alarm register 3)

The logic:

1. **Month rollover check** (`Uade04` lines 1167-1185): Extract current month
   from `DATE+1` and old month from `OTIME+2`. If current month < old month,
   a **year rollover** has occurred: increment the year nibble in `DATE+1` by
   `&10` (one unit in the high nibble), with carry propagated into the high-order
   bits of `DATE`.

   The comment at `Uade04` lines 1177-1179 is endearingly honest:

   ```
    ADCIM &10     ; Increment year will loop sometime
                  ; but who cares as product should
                  ; be dead by then
   ```

2. **Leap year fixup** (`Uade04` lines 1187-1225): If no rollover occurred but
   the leap flag (`&10`) is set, and it is now March or later, apply a date
   correction: decrement the day by 1. This is necessary because the SAF3019P
   thinks February always has 28 days (see datasheet Table 1: day counter wraps
   28 -> 01 when month = 2), but in a leap year February should have 29 days.
   The chip's month-rollover to March happened one day early, so the software
   must compensate.

   If decrementing the day produces zero, the code rolls back into the previous
   month with the correct final day (28, 29, 30, or 31 depending on month),
   checked against the `MW30D` table (`Uade04` line 397, also at `Uade03`
   line 1518).

3. After any correction, `CHKDTE` validates the result (`Uade04` lines
   400-449) and `SETTME` rewrites the corrected values back to the dongle
   (`Uade04` line 1225).


Evolution of the Year Representation
-------------------------------------

The 7-bit year encoding described above is the final form, present in the v1.33
shared codebase (`FileServer/SRC/FileServer/`). It was not always this way. The
original Acorn code used only **4 bits** for the year, and the expansion to 7
bits was a third-party patch by J.G. Harston (JGH), not an Acorn change.


### Original Acorn Format: 4-Bit Year (v0.90 through v1.24)

In the original source code (v1.06, the earliest version with surviving source),
the year occupied only the **upper nibble of `DATE+1`** -- 4 bits, giving a
range of just 16 years (1981-1996). The comment in the dongle register
allocation block read:

```
;  1=YEAR in lower 4 bits                        (Was Months)
```

(`Uade04` in git commit `52d4cf4`, v1.06 source)

The corresponding `SETYR` routine was a simple 4-bit shift:

```
SETYR ASLA
 ASLA
 ASLA
 ASLA
 ORA DATE + 1
 STA DATE + 1    ;Year is top nibble of DATE +01
 RTS
```

And `YR` (the display routine) simply shifted the nibble back down:

```
YR LDA DATE + 1
 LSRA
 LSRA
 LSRA
 LSRA
 CLC
 ADCIM BASEYR
```

This format was shared with the on-disc directory entry date field (`DRDATE`,
2 bytes per entry, defined at `Uade02` line 59). The `OUTDAT` routine in
`Uade0E` (v1.06, lines 499-537) extracted the year identically -- shift
`DATE+1` right by 4, add `BASEYR`, print as decimal. No masking of `DATE` was
needed because the day occupied the full 8 bits of `DATE` byte 0 (values 1-31
only ever use the low 5 bits, but the upper 3 were unused/zero, not
repurposed).

With `Baseyr = 81` and 4 bits, the representable years were 1981 (offset 0)
through 1996 (offset 15). The `Thisyr` constant (`Uade01` line 251 in v1.06)
was set to 85 (1985), rejecting year entries before 1985 -- consistent with
the file server's release era. There was no century handling at all; years
above 1999 could not be represented.

This 4-bit format was used unchanged through all Acorn-released versions: v0.90,
v1.01, v1.03, v1.04, v1.06, v1.07, and v1.24. The on-disc date format in
directory entries (`DRDATE`) remained 4-bit throughout, and the dongle alarm
register 1 stored only a 4-bit value.


### The Y2K Problem

By the mid-1990s, the 4-bit year was about to roll over. With offset 15
representing 1996, there was no way to encode 1997 or later. The README notes:
"With all binaries that use the date from the RTC dongle, the year is displayed
incorrectly."


### J.G. Harston's Y2K Patch: 7-Bit Year (v0.92, v1.25)

J.G. Harston (JGH) produced a fix, first released in **December 1998** for
v0.90 (creating the patched v0.92), and again in **January 2018** for v1.24
(creating v1.25). These are not Acorn releases but third-party patches.

The v0.92 README (`Level3/SRC/README` in git commit `c6622c1`) states:

> J.G.Harston released a fix in Dec 1998 for v0.90 to allow the use of dates
> after 1996. This patched the above mentioned v0.90 Pre-Release IV.05 or
> "BARSON COMPUTERS" binary.

The v1.25 README (`Level3/SRC/README` in git commit `99f1093`) states:

> This is NOT the Acorn released v1.25. It is the Acorn released v1.24 with a
> date fix patch applied.
>
> J.G.Harston released a fix in Jan 2018 for v1.24 to allow the use of dates
> after 1996. This patched the above mentioned v1.24 binary file.

Both versions introduce the `Y2KPAT` assembly flag (`Uade01`):

```
Y2KPAT * 0 ; no(=0) Apply the JGH Patch (=1)
```

The JGH patch extended the year from 4 bits to 7 bits by borrowing the top 3
bits of `DATE` byte 0 (which had bits 7-5 unused, since day-of-month only needs
bits 4-0). This increased the range from 1981-1996 to 1981-2108.

In v0.92 and v1.25, the patch is applied conditionally. When `Y2KPAT=1`:

- `GetYear` (v1.25, `Uade04` lines 859-870) extracts the 7-bit year by
  combining `DATE &E0` (shifted) with `DATE+1` upper nibble
- `PutYear` / `PutDate` (v1.25, `Uade04` lines 871-884) masks the day to 5
  bits and reconstructs the year across both bytes
- `YR` calls `GetYear` instead of the simple 4-bit shift
- `OUTDAT` in `Uade0E` is similarly patched to mask `DATE` with `&1F` for the
  day and extract the year high bits from `&E0`

Critically, the `SETYR` routine itself was **not changed** in v0.92 or v1.25 --
it remains 4-bit. The JGH patch works by modifying the *callers* and display
routines rather than the core encoding routine. This is because the patch was
originally a binary patch applied to an existing ROM image, and the approach
minimised the bytes that needed changing.

In v1.25, the dongle code path is also replaced: `RTC` jumps straight to
`RDDONG` which now uses **OSWORD 14** (read real-time clock, `Uade04` lines
1007-1049 in git commit `99f1093`) instead of bit-banging the SAF3019P. After
OSWORD 14 returns the BCD date fields, the code manually packs the 7-bit year
into the DATE bytes, including the `&E0` high bits.

Note: v0.92 was assembled with `Dongle=1` (no dongle), so the dongle bit-bang
code was not included. The Y2K fix in v0.92 only affects the manual-entry and
display paths.


### The v1.24 "Clock Hack" Variant

A separate patch exists as the "v1.24c" or "Clock Hack" variant (git commit
`aa6edfc`). This is unrelated to the JGH Y2K fix. It uses a `CHACK` flag:

```
CHACK * 1 ; no(=0) Apply the Clock Hack Patch (=1)
```

When `CHACK=1`, the `RTC` routine is replaced with code that simply pokes a
fixed date (1 January 1984 at 12:30:00) directly into the file server's memory
variables, bypassing the dongle entirely. The `SETTME` routine returns
immediately without writing to the dongle. This was a workaround for systems
where the dongle had failed or was unavailable -- it allowed the file server to
start with a plausible (if incorrect) date rather than halting with "Clock
Failure".

The v1.24c README (`Level3/SRC/README` in git commit `aa6edfc`) describes it as:

> Clock hack to set the clock to 1/1/1984 at 12:30:00

Confusingly, the v1.24c source has the dongle register comment updated to say
"lower 7 bits" but the actual `SETYR` code remains 4-bit. This appears to be
a comment error introduced during the reconstruction of the source code for
this repository.


### Integration into the v1.31/v1.33 Shared Codebase

The v1.33 shared codebase (`FileServer/SRC/FileServer/`, git commit `16ebefb`)
represents the final form. Here the 7-bit year is fully integrated:

- `SETYR` (`Uade04` lines 973-984) performs the full 7-bit split across both
  DATE bytes, using the `&E0` mask and `Rem` temporary
- `YR` (`Uade04` lines 853-878) extracts from both bytes
- `OUTDAT` in `Uade0E` (lines 539-571) masks day with `&1F` and extracts year
  high bits from `&E0`
- `CHKDTE` (`Uade04` lines 400-449) masks day with `ANDIM 31` before
  validation
- `SETTME` dongle write (`Uade04` lines 1006-1042) extracts the full 7-bit
  year from `DAYS` (using `&E0` mask at line 1008) before writing to alarm
  register 1
- `REDFIX` year increment (`Uade04` lines 1181-1184) propagates carry from
  the year nibble in `DATE+1` into the high-order bits of `DATE`
- `SETMFX` leap year test (`Uade04` lines 1058-1061) uses
  `ADCIM :LSB:(BASEYR*16)` and `ANDIM 3*16` to test divisibility by 4 in the
  nibble position

All the `**24/2/88**` and `**24/3/88**` date comments in the v1.33 source mark
these changes. The dates suggest they were made in February-March 1988, which
would place them during original Acorn development -- however, these same
comments also appear in the v1.24 and v1.25 reconstructed sources where the
underlying code is demonstrably still 4-bit. This indicates the date comments
were added by the repository maintainer during source reconstruction to mark
where changes were needed or applied, not necessarily reflecting the original
Acorn development timeline.


### Version Comparison

| Version | Git commit | Year bits | Year range  | `SETYR` | Dongle comment     | Origin          |
|---------|------------|-----------|-------------|---------|--------------------|-----------------| 
| v0.90   | `dac1708`  | 4         | 1981-1996   | 4-bit   | "lower 4 bits"     | Reconstructed from v1.06 source + v0.90 binary |
| v0.92   | `c6622c1`  | 4+3 (conditional) | 1981-2108 | 4-bit (callers patched) | "lower 4 bits" | JGH Y2K patch (Dec 1998) on v0.90 |
| v1.06   | `52d4cf4`  | 4         | 1981-1996   | 4-bit   | "lower 4 bits"     | Original Acorn source |
| v1.24   | `5c03492`  | 4         | 1981-1996   | 4-bit   | "lower 7 bits" (comment error) | Reconstructed from v1.31 source + v1.24 binary |
| v1.24c  | `aa6edfc`  | 4         | 1981-1996   | 4-bit   | "lower 7 bits" (comment error) | Clock Hack patch on v1.24 |
| v1.25   | `99f1093`  | 4+3 (conditional) | 1981-2108 | 4-bit (callers patched) | "lower 7 bits" | JGH Y2K patch (Jan 2018) on v1.24 |
| v1.31   | `6f54848`  | 7         | 1981-2108   | 7-bit   | "lower 7 bits"     | Shared codebase, integrated |
| v1.33   | `16ebefb`  | 7         | 1981-2108   | 7-bit   | "lower 7 bits"     | Shared codebase, integrated |


Alarm Register Bit Widths and the Year 2001 Problem
-----------------------------------------------------

The SAF3019P datasheet describes the time register (alarm) as a "24-bit
memory." This 24-bit total is divided across the four fields according to the
BCD storage requirements of each:

| Field   | Counter range | BCD range | Storage bits | Unused positions |
|---------|---------------|-----------|--------------|------------------|
| Minutes | 00-59         | 00-59     | 7 (LA-UC)    | none             |
| Hours   | 00-23         | 00-23     | 6 (LA-UB)    | UC = 0           |
| Date    | 01-31         | 01-31     | 6 (LA-UB)    | UC = 0           |
| Month   | 01-12         | 01-12     | 5 (LA-UA)    | UB = 0, UC = 0   |

Total: 7 + 6 + 6 + 5 = 24 bits.

This can be confirmed from Table 2 of the datasheet: the TIME READ for time
register month (S=1, A0=0, A1=0) shows LA through UA as data bits (D), with
UB and UC always reading back as 0.

**Register 1 (alarm month, used for year storage) has only 5 data bits.** This
gives a BCD range of 00-19, corresponding to decimal values 0 through 19. Year
offsets 0-19 (years 1981-2000) can be stored and retrieved correctly. Year
offset 20 (year 2001, BCD &20 = 0010 0000) requires the UB bit, which is not
stored; it would silently read back as BCD 00 (year offset 0, i.e. 1981).

### Impact on Each Version

**Original Acorn code (v0.90-v1.24):** The 4-bit year encoding produces offsets
0-15, comfortably within the 0-19 hardware limit. The `SETTME` dongle write in
v1.06 (`Uade04` in git commit `52d4cf4`) shifts `MUNTHS` right by 4 to extract
only the upper nibble -- a 4-bit value. The comment "lower 4 bits" matches
reality. There is no evidence the Acorn developers were aware of the 5-bit
register width per se; they simply never came close to it.

**JGH Y2K patches (v0.92, v1.25):** JGH was clearly aware that the 7-bit year
could not safely round-trip through the dongle. This is evident from two
deliberate design choices:

- In v1.25 (`Uade04` in git commit `99f1093`), when `Y2KPAT=1`, `SETTME` is
  replaced with a single instruction:

  ```
  SETTME ROUT
   [ Y2KPAT =1
   RTS ; Never write to RTC
  ```

  The dongle is never written to. The 7-bit year exists only in the in-memory
  `DATE`/`DATE+1` variables.

- `RDDONG` is also completely replaced: instead of bit-banging the SAF3019P, it
  uses **OSWORD 14** to read the BBC Master's built-in RTC, which does have a
  year counter (`Uade04` lines 1011-1050 in git commit `99f1093`). The Master
  RTC year is then packed into the 7-bit in-memory format.

- v0.92 avoids the issue entirely by assembling with `Dongle=1` (no dongle),
  so the SAF3019P code is not included at all.

**v1.31/v1.33 shared codebase:** The integrated 7-bit code **does** write the
full year value to the dongle. `SETTME` (`Uade04` lines 1006-1017 in git
commit `16ebefb`) extracts all 7 bits of the year from `DAYS` and `MUNTHS` and
writes the result to alarm register 1 via `JSR #95`. For year offsets >= 20
(year 2001+), this would write a BCD value that overflows the 5-bit register.
On the next read (`RDDONG`), the upper bits would be lost, and the year would
wrap back into the 1981-2000 range.

There are no comments in the v1.33 source acknowledging this limitation. The
"who cares as product should be dead by then" comment (`Uade04` line 1178)
refers to a different overflow -- the year nibble in `REDFIX` wrapping around
after 16 years of rollover corrections -- not to the register width.

The most likely explanation is that the v1.31/v1.33 integrated codebase was
developed for this repository by merging the in-memory 7-bit year format (from
JGH's patches or independent development) into the dongle write path without
testing against real SAF3019P hardware. Since no Level 3 file server with an
original dongle was likely still in service after 2000, the bug would never
have been encountered in practice.

### Effective Year Ranges on Real Hardware

| Version   | In-memory range | Dongle round-trip range | Limiting factor           |
|-----------|-----------------|-------------------------|---------------------------|
| v0.90-v1.24 | 1981-1996    | 1981-1996               | 4-bit year encoding       |
| v0.92     | 1981-2108       | N/A (no dongle)         | Assembled with Dongle=1   |
| v1.25     | 1981-2108       | N/A (OSWORD 14)         | Dongle writes disabled    |
| v1.31/v1.33 | 1981-2108    | 1981-2000               | 5-bit alarm register      |


Dongle Presence Detection
--------------------------

The `RTC` routine (`Uade04` lines 1104-1122) verifies the dongle exists by
exploiting the properties of alarm register 7:

```
RTC  LDXIM 7
     LDAIM &71
     JSR #95          ; write &71 to register 7 (via BCD conversion)

     LDXIM 7
     JSR #00          ; read register 7 back
     JSR #70          ; convert BCD to binary
     EORIM &0D        ; "devious eh"
     BNE RTCX         ; fail if not &0D

     LDXIM 7
     JSR #95          ; write zero
     LDXIM 7
     JSR #00          ; read back
     CMPIM 0
RTCX RTS              ; returns EQ if dongle present
```

The write routine `#95` (`Uade04` line 1369) first converts the value to BCD
via `#85` (`Uade04` lines 1398-1407), so binary 71 becomes BCD `&71`. On
readback, `#70` (`Uade04` lines 1385-1396) converts BCD back to binary by
counting -- but the register truncates or wraps the 7-bit alarm field, yielding
`&0D` (13 decimal) after conversion. The second write/read cycle with zero
confirms the register is genuinely writable.


### BCD Conversion Routines

Both conversion routines use the 6502's decimal (BCD) mode flag:

**Binary to BCD** (`#85`, `Uade04` lines 1398-1407): Counts up from 0 in
decimal mode, decrementing the binary input until exhausted:

```
85 TAY              ; binary value in Y
   BEQ #94          ; zero is zero in either representation
   LDAIM 0
   SED              ; enter decimal mode
90 CLC
   ADCIM 1          ; increment in BCD
   DEY              ; decrement binary count
   BNE #90
94 CLD              ; leave decimal mode
   RTS
```

**BCD to Binary** (`#70`, `Uade04` lines 1385-1396): The inverse -- counts up
in binary while subtracting 1 in decimal mode:

```
70 LDYIM 0          ; binary accumulator
   TAX
   BEQ #75          ; zero is zero
80 CLD
   INY              ; increment binary count
   SED
   SEC
   SBCIM 1          ; decrement in BCD
   BNE #80
   CLD
75 TYA
   RTS
```


CBUS Bit-Banging Protocol
--------------------------

The SAF3019P's CBUS interface uses three signals: DATA (bidirectional), DLEN
(enable), and CLB (clock burst). These are mapped to User VIA Port B bits and
accessed indirectly through MOS calls.

The workspace variables for the VIA shadow registers are allocated only when
`Dongle=0` (`Uade02` lines 392-395):

```
 [ Dongle=0
ORB # 1
IRB # 1
DDRB # 1
 ]
```

### VIA Access

All VIA access goes through OSBYTE rather than direct hardware reads/writes:

- **OSBYTE 151** (write to Sheila): X=`&60` for Port B output (ORB), X=`&62`
  for Data Direction Register B (DDRB). Used at `Uade04` lines 1327-1337 (ORB
  write, routine `#45`) and lines 1351-1367 (DDRB write, routine `#55`).
- **OSBYTE 150** (read from Sheila): X=`&60` for Port B input (IRB). Used at
  `Uade04` lines 1339-1349 (routine `#50`).

### Write Sequence

The write routine `#95` (`Uade04` lines 1369-1383):

1. Converts the value to BCD via `#85`
2. If writing to register 0 (months), OR's in bit 6 (`&40`) -- this is the UC
   bit from the SAF3019P datasheet's Table 3, which controls the comparison
   mode (compare with date: UC=0 and NODA=LOW; compare daily: UC=1 or
   NODA=HIGH)
3. Calls `#05` (`Uade04` lines 1279-1290) to set DDRB to `&A7` and shift out
   the 4-bit register address
4. Shifts out the 7 data bits plus UC via `#30` (`Uade04` lines 1286-1290)
5. Completes the transfer with a clock/load sequence

### Read Sequence

The read routine `#00` (`Uade04` lines 1228-1277):

1. Sends the 4-bit TIME ADDRESS (register number)
2. Sets DDRB to `&06` (lines 1231-1233) -- only bits 1 and 2 are outputs
   (clock lines); bit 0 becomes input (DATA). Bits 7 and 5 are the POWERFAIL
   and ALARM flags from the chip.
3. Clocks out 7 data bits one at a time (loop at lines 1247-1268): toggles CLB
   high then low, reading DATA (bit 0 of IRB) on each cycle, accumulating via
   ROR into the result byte


Reading the Dongle: `RDDONG` (`Uade04` lines 1124-1158)
---------------------------------------------------------

This routine reads all dongle registers in sequence and populates the
in-memory date/time variables:

```
RDDONG
 LDXIM 6          ; read minutes (register 6)
 JSR #00
 JSR #70          ; BCD to binary
 STA MINS

 LDXIM 4          ; hours (register 4)
 JSR #00
 JSR #70
 STA HRS

 LDXIM 2          ; days (register 2)
 JSR #00
 JSR #70
 STA DATE

 LDXIM 0          ; months (register 0)
 JSR #00
 JSR #70
 STA DATE+1

 LDXIM 3          ; oldmonth + leap flag (register 3, alarm)
 JSR #00
 JSR #70
 STA OTIME+2      ; "somewhere but where"

 LDXIM 1          ; year (register 1, alarm)
 JSR #00
 JSR #70
 JSR SETYR        ; pack into DATE/DATE+1

 ; falls through to REDFIX for year/leap correction
```


Non-Dongle Timekeeping (`Dongle=1`)
-------------------------------------

When assembled without dongle support:

### Date Entry at Boot

`STDATE` (`Uade04` lines 294-344) prompts `Date (DD/MM/YY) =` and parses
day, month, and year separated by `/`. Year entry accepts 2-digit values with
century correction (lines 336-337): if `year - BASEYR` produces a borrow
(year < 81), it is treated as 20xx by adding 100.

### Time Entry at Boot

`RDTIME` (`Uade04` lines 497-535) prompts `Time (HH:MM:SS) =` and parses
hours, minutes, and seconds separated by `:`.

### Time of Day Tracking

`SETTME` (`Uade04` lines 536-565) converts the entered time to the MOS 5-byte
centisecond timer format by computing `((hours * 60 + minutes) * 60 + seconds)
* 100` using the multiplication routines `M60` (lines 595-610) and `M100`
(lines 612-638), then writes it via OSWORD 2.

### Time Display and Midnight Rollover

`PRTIM` (`Uade04` lines 669-735) reads the MOS timer via OSWORD 1, divides
successively by 100, 60, 60, and 24 (using `TDVD` at line 737 and the division
entry points `DIV100`/`DIV60`/`DIV24` at lines 796-811) to extract hours,
minutes, and seconds. This mode **does** display seconds (HH:MM:SS), unlike the
dongle mode which only shows HH:MM.

**Midnight detection** (lines 708-734): When all TIME bytes OR to non-zero but
the division yields zero hours (meaning the timer has not yet been reset for a
new day), the code subtracts `&0083D600` (8,640,000 centiseconds = 24 hours)
from the MOS timer, writes the adjusted value back via OSWORD 2, and calls
`INCDAY`.

### Manual Day Increment: `INCDAY` (`Uade03` lines 1446-1516)

This routine, assembled only when `Dongle=1`, manually advances the calendar:

- If day < 28, simply increment (`Uade03` line 1450)
- For day >= 28, check against month-specific limits:
  - February: check for day 29 (leap year) by testing `(year + BASEYR) AND 3`
    (`Uade03` lines 1506-1515)
  - 30-day months (4, 6, 9, 11): checked against `MW30D` table (`Uade03`
    lines 1460-1466, table at line 1518)
  - All other months: 31-day limit (`Uade03` lines 1467-1469)
- Month overflow at December rolls the year nibble in `DATE+1` (`Uade03`
  lines 1487-1501)


FileStore / BBC Master RTC (`$CMOS` Path)
-------------------------------------------

The FileStore uses the onboard 146818 CMOS RTC chip. The MOS handles all
hardware communication; the file server simply calls OSWORD 1 (read) or
OSWORD 2 (write) with a pointer to the 5-byte `DATE` buffer.

The `Getdte` and `Setdte` routines (`Uade04` lines 385-392) are trivially
simple:

```
Getdte ROUT
 LDAIM 1
 BRA #10

Setdte LDAIM 2
10 LDXIM :LSB:Date
 LDYIM :MSB:Date
 JMP OSword         ; OSWORD 1 or 2 as appropriate
```

The 5-byte date/time vector format for the FileStore (`Uade02` lines 409-412):

```
DATE # 5            ; Date and time values
HRS * DATE+2        ; byte 2 = hours
MINS * DATE+3       ; byte 3 = minutes
SECS * DATE+4       ; byte 4 = seconds
                    ; byte 0 = day, byte 1 = year-offset/month
```

This contrasts with the Level 3 layout (`Uade02` lines 391-407) which uses
separate 3-byte DATE and 5-byte TIME areas, with individual byte variables
for SECS, MINS, HRS, DAYS, and MUNTHS.

The 146818 has a built-in year counter, and its non-volatile CMOS RAM is also
used to store file server configuration including station ID, maximum users/
drives, printer name, and maintenance user ID (initialisation table `CMintb`
at `Uade04` lines 110-122, CMOS address offsets defined at `Uade01` lines
283-295).


Date Validation: `CHKDTE` (`Uade04` lines 400-449)
----------------------------------------------------

Used by all paths to validate a date after entry or correction:

- Day must be 1-31 (isolates lower 5 bits with `ANDIM 31` at line 405)
- Month must be 1-12 (isolates lower nibble with `ANDIM &F` at line 414)
- For February, day 29 is permitted only in leap years (lines 436-444):

  ```
   LDA DATE+1
   ADCIM :LSB:(BASEYR*16)   ; add base year, scaled to nibble position
   ANDIM 3*16                ; test if divisible by 4
   BNE #40                   ; not leap: reject day 29
  ```

- For 30-day months (April, June, September, November), day 31 is rejected
  using the `MW30D` table (lines 423-429)

Returns zero in A for a valid date, non-zero for invalid.


Date Display: `SCDATE` (`Uade04` lines 822-841)
-------------------------------------------------

Displays the full date in a text window at the top of the Mode 7 screen:

- `DATE1` (`Uade04` lines 926-970): Prints the day with ordinal suffix
  ("st", "nd", "rd", "th"), looked up from the suffix table at lines 960-968
- `MONTH` (`Uade04` lines 881-922): Prints the full month name from the string
  table at lines 897-908, indexed via `MTAB1` (lines 910-922)
- `YR` (`Uade04` lines 853-878): Prints the 4-digit year with century prefix

The window management (`DWIND`/`MWIND` at lines 842-840) saves and restores
the cursor position and defines a text window at the top of the screen for
date/time display, separate from the main file server status area.


Summary
--------

| Feature           | Dongle (SAF3019P)           | No Dongle                  | FileStore (146818)  |
|-------------------|-----------------------------|----------------------------|---------------------|
| Year source       | Alarm register 1 (RAM)      | Manual entry               | CMOS RTC chip       |
| Year rollover     | Month comparison trick      | `INCDAY` on midnight       | Hardware            |
| Leap year         | Flag in alarm register 3    | Calculated in `INCDAY`     | Hardware            |
| Seconds display   | No (HH:MM only)            | Yes (HH:MM:SS)             | Yes                 |
| Time source       | Chip counters               | MOS centisecond timer      | OSWORD 1/2          |
| Communication     | Bit-bang via User VIA PB    | N/A                        | MOS OSWORD          |
| Assembly flag     | `Dongle=0`                  | `Dongle=1`                 | `$CMOS`             |


Source File Reference
----------------------

All paths relative to `FileServer/SRC/FileServer/`:

| File    | Content relevant to timekeeping                                           |
|---------|---------------------------------------------------------------------------|
| `Uade01` | `Dongle` flag (line 59), `Baseyr`/`Thisyr` constants (lines 270-271), CMOS address offsets (lines 283-295) |
| `Uade02` | Variable definitions: `DATE`, `TIME`, `OTIME`, `NTIME`, `SECS`, `MINS`, `HRS`, `DAYS`, `MUNTHS`, `ORB`, `IRB`, `DDRB` (lines 389-413) |
| `Uade03` | `INCDAY` routine for non-dongle day advancement (lines 1446-1516), `MW30D` 30-day month table (line 1518) |
| `Uade04` | All other RTC code: initialisation and boot flow (lines 50-77), `STDATE` manual date entry (lines 294-344), `RDTIME` manual time entry (lines 497-535), `SETTME` time-to-centiseconds (lines 536-565), `PRTIM`/`PRTIME` time display and midnight rollover (lines 669-735), `CHKDTE` date validation (lines 400-449), `SCDATE`/`DATE1`/`MONTH`/`YR` date display (lines 822-878, 881-970), `SETYR` year encoding (lines 973-984), `SETTME` dongle write (lines 1006-1042), `SETMFX` old-month and leap flag (lines 1044-1069), `PRTIM` dongle time display (lines 1071-1102), `RTC` dongle presence detection (lines 1104-1122), `RDDONG` dongle read (lines 1124-1158), `REDFIX` year/leap correction (lines 1161-1225), CBUS bit-bang routines (lines 1228-1408), `Getdte`/`Setdte` FileStore OSWORD interface (lines 385-392) |
