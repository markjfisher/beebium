# DFS *CAT Regression: Investigation and Resolution

## Summary

After integrating the pulse-level disc subsystem (PulseWD1770, PulseDiscDrive), the DFS `*CAT` end-to-end integration test failed. DFS read the disc catalogue data correctly but never displayed the output, appearing to stall inside its sideways ROM code. This was a regression from the sector-level WD1770 which passed the same test.

**Status: RESOLVED.** The root cause was identified and fixed.

## Root Cause

PulseWD1770 set DRQ and INTRQ simultaneously on the last byte of a Read Sector command. The MOS NMI handler reads the status register first (`LDA $FE84`), which clears INTRQ as a side effect. It then checks the DRQ bit, reads the data byte, and returns. Because INTRQ was cleared by the status read, DFS never received the separate command-completion NMI it relies on to know the sector transfer is finished.

On real WD1770 hardware, command completion (INTRQ) is signalled after the host acknowledges the last data byte by reading the data register (clearing DRQ). There is a temporal separation between the last DRQ and the INTRQ. The old sector-level WD1770 also had this separation because `complete_command()` was called on a different tick from the DRQ assertion.

### Before fix (broken):

```
Tick N: drq_=true, data_=last_byte, complete_command() -> intrq_=true
        nmi_pending() = true (both DRQ and INTRQ set)
        NMI fires -> MOS reads status (clears INTRQ), reads data (clears DRQ)
        After RTI: both DRQ and INTRQ clear, no further NMI
        DFS never sees command-completion INTRQ
```

### After fix (correct):

```
Tick N:   drq_=true, data_=last_byte
          NMI fires -> MOS reads status, reads data (clears DRQ)
Tick N+1: drq_ was cleared by host, byte_counter_ >= data_size_
          complete_command() -> intrq_=true
          NMI fires -> MOS reads status, sees no DRQ (command complete)
          DFS processes completion
```

## Fix Applied

In `PulseWD1770::tick_read_sector_phase()`, the `ReadingSectorData` phase was restructured. Instead of calling `complete_command()` immediately when setting up the last byte's DRQ, the completion is deferred to the next tick cycle after the host clears DRQ by reading the data register. The phase handler checks `byte_counter_ >= data_size_` at the top of each entry (after the DRQ-wait guard) and calls `complete_command()` at that point.

## Additional Fixes Discovered During Investigation

1. **Bus stretch disc controller ticking** (`Machine::tick_stretch_cycle`): The disc controller was not being ticked during 1MHz bus stretch cycles. When the CPU accessed the WD1770's I/O registers ($FE84-$FE87), bus stretching halted the CPU for extra cycles, during which the disc controller stopped advancing. This caused timing drift in the pulse stream. Fixed by calling `poll_nmi()` on 1MHz edges during stretch cycles.

2. **ID field CRC off-by-one** (`PulseWD1770::ReadingIdField`): The running CRC included the first CRC byte (byte index 4 of the ID field) in the CRC computation, causing all ID CRC checks to fail. The condition `id_byte_count_ < 6` should have been `id_byte_count_ <= 4` since `id_byte_count_` is post-incremented. Without this fix, no sectors could be found.

3. **Incorrect disc control register values in tests**: Integration tests wrote `CTRL_MOTOR_ON | CTRL_NMI_ENABLE` (0x50) to the disc control register, which set density to MFM (bit 3 = 0) instead of FM. The pulse-level WD1770 uses density to select between FM and MFM pulse decoding, so this caused complete failure to read FM-encoded SSD discs. Fixed to match DFS 2.26's actual control register write: `CTRL_DRIVE0 | CTRL_DENSITY | CTRL_RESET` (0x29).

4. **Test cycle count**: The *CAT test timeout was 50M cycles (~25 seconds emulated). The pulse-level WD1770 has realistic disc rotation timing (~200K cycles per sector read), so DFS processing takes longer than with the instant sector-level WD1770. Increased to 200M cycles.

## Investigation Method

The bug was found through systematic test construction, progressively narrowing the failure domain:

| Test | Result | What It Eliminated |
|------|--------|--------------------|
| Pulse Read Sector matches raw SSD bytes | PASS | Data integrity through pulse encode/decode pipeline is correct |
| NMI fires during Read Sector (simple handler) | PASS | Basic NMI mechanism works for DRQ-driven byte transfer |
| Reading status register in NMI handler | PASS | MOS-style `LDA $FE84` / `LDA $FE87` handler pattern works |
| Consecutive Read Sector via polling | PASS | PulseWD1770 handles back-to-back commands correctly |
| Consecutive NMI-driven sector reads | PASS | NMI edge detection works across command boundaries |
| DFS *CAT with 50M cycles | FAIL | DFS stalls after reading catalogue |
| DFS *CAT with 200M cycles (pre-INTRQ-fix) | PARTIAL | Catalogue appears but with wrong data -- timing was insufficient AND INTRQ was lost |
| DFS *CAT with 200M cycles (post-INTRQ-fix) | PASS | 516 NMIs, correct catalogue output |

The breakthrough came from command-level instrumentation of the *CAT test, which showed:
- 4 WD1770 commands issued (boot probe, Restore, Read Sector 0, Read Sector 1)
- 514 NMI handler entries at $0D00 (both sectors fully transferred)
- DFS was stuck in MOS output code at $DE/$DF (OSWRCH), not in an error loop

This revealed that (a) DFS WAS reading both sectors, disproving the "second read fails" hypothesis, and (b) with more cycles, the output DID appear -- but with corrupted content. Examining NMI #258 showed `drq=1 intrq=1` (simultaneous), confirming the INTRQ was being set on the same tick as the last DRQ and lost during the MOS handler's status register read.

## Files Modified

- `src/core/include/beebium/disc/PulseWD1770.hpp` -- INTRQ deferral in ReadingSectorData, Density enum, ID field CRC fix
- `src/core/include/beebium/Machine.hpp` -- tick disc controller during bus stretch cycles
- `tests/test_disc_integration.cpp` -- 7 new diagnostic tests, fixed control register values, increased timeout

## Lessons Learned

1. **Simultaneous signal assertion breaks edge-triggered NMI**: On the 6502, NMI is edge-triggered. When two signals (DRQ and INTRQ) are OR'd to produce NMI, they must fire at distinct times to produce separate edges. Setting both on the same tick produces only one edge.

2. **The MOS NMI handler clears INTRQ by reading status**: This is a side effect that's harmless when DRQ and INTRQ are temporally separated, but destructive when they coincide. The real WD1770 naturally separates them because the host must read the data register (clearing DRQ) before the controller advances to the completion state (setting INTRQ).

3. **Sector-level emulation hid timing dependencies**: The old WD1770 had no disc rotation timing, so it naturally separated DRQ and INTRQ across different tick() calls. The pulse-level WD1770 processes the last byte and completes the command in the same phase handler invocation, creating the simultaneous assertion.

4. **Test cycle counts must account for realistic timing**: Pulse-level disc access is orders of magnitude slower than sector-level. A 50M cycle timeout that was generous for the old WD1770 was barely enough for the new one. The disc rotation at 300 RPM (200K cycles per revolution) means even a simple two-sector read takes ~500K cycles for the data transfer alone, plus MOS overhead for display.

5. **Custom NMI handler tests can mask the bug**: Our custom handler `INC / LDA $FE87 / RTI` doesn't read the status register, so it doesn't clear INTRQ. The simultaneous DRQ+INTRQ assertion didn't matter because the handler only cared about DRQ. Only the MOS-style handler (status read first) exposed the bug.
