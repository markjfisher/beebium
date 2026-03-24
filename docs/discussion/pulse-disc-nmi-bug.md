# DFS *CAT Regression Investigation

## Summary

After integrating the pulse-level disc subsystem (PulseWD1770, PulseDiscDrive), the DFS `*CAT` end-to-end integration test fails. The screen shows `>*CAT` followed by blank lines -- DFS begins processing the command but never produces catalogue output. The emulator does not crash; DFS becomes stuck in a loop inside its sideways ROM code.

This is a regression: the old sector-level WD1770 passed this test.

## What Works

The following have all been verified with dedicated tests:

1. **Pulse encoding fidelity**: Sectors read via PulseWD1770 match the raw SSD file bytes exactly, byte-for-byte. Test: "Pulse Read Sector matches raw SSD file bytes" reads sectors 0 and 1 from a real SSD image and compares against the raw file. Both match.

2. **NMI fires correctly**: A custom NMI handler at $0D00 (installed via MOS ROM vector patching) counts 256 DRQ transitions during a Read Sector command. The handler reads $FE87 to clear DRQ, and DRQ re-asserts for each subsequent byte. Test: "NMI fires during Read Sector".

3. **MOS-style status-then-data pattern works**: An NMI handler that reads $FE84 (status register) before $FE87 (data register) -- the pattern used by real MOS -- works correctly. 256 DRQ transitions, 256 PC entries to the handler address. Test: "Reading status register in NMI handler does not break DRQ transfer".

4. **Direct register polling works**: The `read_sector` helper (polling STATUS for DRQ, reading DATA) successfully reads complete sectors. Test: "Model B+ can read sector from inserted disc".

5. **INTRQ fires on command completion**: Force Interrupt with I3=1 correctly sets INTRQ, and reading the status register clears it. Test: "Model B+ NMI from WD1770".

6. **Bus stretch ticking**: The disc controller is now ticked during 1MHz bus stretch cycles (fix applied to Machine::tick_stretch_cycle). Without this, the disc head stalled during every WD1770 register access.

## What Fails

The DFS `*CAT` test loads real ROMs (MOS 2.0, DFS 2.26, BASIC 2), boots the emulator, types `*CAT`, and checks for catalogue output on screen.

### Observed Behaviour

- Boot completes normally: "Acorn OS 64K", "Acorn 1770 DFS", "BASIC" all appear
- `*CAT` is typed and accepted (visible on screen as `>*CAT`)
- **258 NMI handler entries** at $0D00 (MOS NMI vector) during the wait period
  - 258 = 256 data bytes (one sector) + 2 INTRQ completions
  - This means only ONE sector is read, not two
- **4 WD1770 commands** issued (detected by monitoring BUSY transitions):
  1. Cycle 0, PC=$8E8B (during boot -- likely DFS initialisation probing)
  2. Cycle 2,394,159, PC=$8E7A (Restore or Seek -- after *CAT typed)
  3. Cycle 2,454,484, PC=$8E55 (Read Sector 0 -- this one succeeds)
  4. Cycle 2,494,079, PC=$0D34 (Read Sector 1 -- this one appears to fail)
- After 50M cycles (~25 seconds emulated), the CPU is stuck in sideways ROM slot 11 (DFS code), never returning to the BASIC prompt
- The screen remains blank after `>*CAT` -- no catalogue output

### Key Deduction

DFS issues two Read Sector commands for the catalogue (sectors 0 and 1 on track 0). The first succeeds (256 bytes transferred via NMI). The second appears to fail, causing DFS to enter an error-handling or retry loop.

258 NMIs = 256 data bytes + 2 command-completion INTRQs. If both reads succeeded, we'd expect 256 + 256 + 2 = 514 NMIs (or 256 + 2 + 256 + 2 = 516 if each command's INTRQ generates an NMI). Getting only 258 confirms the second read never delivers data.

## Hypotheses

### Proven False

- **NMI mechanism broken**: Disproven by custom NMI handler tests (256 DRQ transitions, handler enters 256 times).
- **Status register read interferes with DRQ**: Disproven by "Reading status register in NMI handler" test.
- **Data corruption in pulse encoding**: Disproven by byte-exact match against raw SSD.
- **Bus stretch causes missed ticks**: Fixed (disc controller now ticked during stretch cycles), but fix alone didn't resolve *CAT.
- **Wrong NMI vector address in test**: Fixed (MOS 2.0 uses $0D00, not $0AD48).

### Unproven (Active Investigation)

1. **Second Read Sector times out (RNF)**: After the first Read Sector completes, the PulseWD1770's sector search for sector 1 may fail to find the correct ID field within 5 revolutions. This would set the RNF status bit and complete the command without delivering data. DFS would then interpret this as a disc error.

   Evidence: Only 258 NMIs (one sector's worth) despite 4 commands issued.

   Possible sub-causes:
   - Head position after first read leaves the search starting at an inconvenient point
   - Track register mismatch: after first Read Sector, the track register might not match the ID fields on the disc
   - Sector register not updated correctly between commands
   - The `complete_command()` at the end of the first read leaves some state that prevents the second search from working

2. **DFS reads the WD1770 status after the first Read Sector and sees an unexpected value**: The old WD1770's status register after Read Sector completion would have had specific bits set. The PulseWD1770 might report different status bits, causing DFS to take a different code path (error handler, retry loop).

3. **The first Read Sector command is actually a Restore or Seek, not a data read**: The 258 NMIs might come from a different combination of commands. Need to identify exactly which of the 4 commands generates data.

4. **DFS's NMI handler modifies workspace that affects the second command**: DFS stores the sector count, buffer pointer, and other state in its workspace at $0D00-$0DFF. The MOS NMI handler at $0D00 is in the SAME page as DFS workspace. If the NMI handler code or DFS workspace overlap (they do on real hardware -- MOS uses $0D00 for NMI dispatch, and DFS uses the NMI workspace at $0D01 onwards), the interaction might be timing-dependent.

5. **INTRQ from the first Read Sector's completion fires an NMI that disrupts DFS's setup for the second command**: Between the first Read Sector completing (INTRQ) and DFS issuing the second Read Sector, the INTRQ NMI fires. If DFS's NMI handler doesn't expect INTRQ at that point (or handles it incorrectly), it could corrupt DFS's internal state.

## Fixes Applied So Far

1. **Bus stretch ticking** (Machine::tick_stretch_cycle): poll_nmi() and disc controller tick now occur during bus stretch cycles. This was a genuine bug -- the old WD1770 didn't need ticking during stretch because it had no timing dependency, but the pulse-level WD1770 does.

2. **Correct control register values in tests**: Integration tests now write `CTRL_DRIVE0 | CTRL_DENSITY | CTRL_RESET` (0x29) matching what real DFS writes, instead of `CTRL_MOTOR_ON | CTRL_NMI_ENABLE` (0x50) which left density as MFM (wrong for FM SSD discs) and reset as active.

3. **ID field CRC computation fix**: The ReadingIdField phase had an off-by-one that included the first CRC byte in the running CRC, causing all ID CRC checks to fail. Fixed to only CRC-add the first 4 ID bytes (track, side, sector, size), not the CRC bytes themselves.

## Diagnostic Plan

### Immediate Next Steps

1. **Consecutive sector read test**: Write a test that issues Read Sector 0 then Read Sector 1 via direct register access (no DFS, no NMI). This isolates whether two consecutive reads work. If this fails, the bug is in PulseWD1770's state management between commands. If it passes, the bug is in the NMI-driven path or DFS's command sequencing.

2. **Consecutive sector read via NMI**: Same as above but using a custom NMI handler (like the existing tests). Issue Read Sector 0, wait for completion, then issue Read Sector 1. Count DRQ transitions for each. Expected: 256 + 256 = 512.

3. **WD1770 status after command completion**: After the first Read Sector completes, read and log the status register value. Compare against what the old WD1770 would have returned. DFS checks specific status bits to determine success/failure.

### Results of Consecutive Read Tests

Both consecutive read tests PASS:
- **Direct polling**: Read Sector 0 then Read Sector 1 via register polling. Both return 256 correct bytes.
- **NMI-driven**: Read Sector 0 then Read Sector 1 with custom NMI handler. 256 + 256 DRQ transitions. NMI edge detection works across command boundaries.

This proves the PulseWD1770 handles consecutive commands correctly in both polling and NMI modes. The bug is NOT in the FDC's state management between commands. The issue is specific to DFS's actual code path through the MOS NMI dispatcher.

### Remaining Hypothesis

The DFS/MOS NMI handling code does something our custom handler doesn't. The most likely candidates:
- DFS reads additional WD1770 registers (track, sector) during or between commands
- DFS's NMI workspace at $0D00-$0DFF overlaps with the MOS NMI handler code in a way that's timing-dependent
- DFS checks specific status register bits that the PulseWD1770 reports differently from the old sector-level WD1770
- The MOS NMI dispatcher has a re-entrancy or nested-NMI path that fails

### Next Steps

1. **Status register comparison**: After each command, compare the exact status byte returned by PulseWD1770 against what the old WD1770 would have returned. DFS checks specific bits to determine success/failure.

2. **DFS workspace dump**: After the *CAT stalls, dump $0D00-$0DFF to see DFS's internal error state.

3. **PC trace during *CAT**: Log the first ~100 unique PCs executed by DFS after the *CAT command is processed. Compare against a trace from the old WD1770 to find the divergence point.

### If Consecutive Reads Fail (N/A -- they pass)

4. **Track register state**: Check the track register value after the first Read Sector. The PulseWD1770 doesn't modify the track register during Read Sector (it uses it for matching). If it's wrong, the second sector search will compare against the wrong track number.

5. **Head position after read**: After the first Read Sector completes, log the drive's head position. The second search needs to scan from wherever the head stopped. If the head is at the end of sector 0's data + CRC, it should find sector 1 within a few hundred byte positions (GAP3 + GAP + sector 1's ID).

6. **Index pulse count**: Monitor how many index pulses the second Read Sector's search encounters. If it reaches 5, it gives up with RNF. Log the number to confirm whether it's a timeout.

### If Consecutive Reads Pass

7. **NMI-driven consecutive reads**: Test two consecutive reads through NMI. The handler must handle the command-completion INTRQ between reads correctly (read status to clear INTRQ, then set up for the next read).

8. **DFS workspace inspection**: After the *CAT stalls, dump the DFS workspace at $0D00-$0DFF to see if any state variables indicate an error condition, retry count, or unexpected state.

9. **DFS code path tracing**: Add PC monitoring during the *CAT to trace which DFS subroutines execute. Compare against a known-good trace (from the old WD1770) to find the divergence point.

## Test Summary

| Test | Result | What It Proves |
|------|--------|---------------|
| Pulse Read Sector matches raw SSD | PASS | Data integrity through pulse encode/decode pipeline |
| NMI fires during Read Sector | PASS | NMI mechanism works for DRQ-driven byte transfer |
| Reading status in NMI handler | PASS | MOS-style status-before-data pattern works |
| Model B+ can read sector (polling) | PASS | Direct register-driven read works |
| Model B+ NMI from WD1770 | PASS | INTRQ correctly generates NMI, status read clears it |
| DFS *CAT command | FAIL | Full DFS ROM stack fails to display catalogue |

## Files Modified

- `src/core/include/beebium/Machine.hpp` -- tick disc controller during bus stretch
- `src/core/include/beebium/disc/PulseWD1770.hpp` -- Density enum, ID field CRC fix
- `tests/test_disc_integration.cpp` -- new diagnostic tests, fixed control register values
