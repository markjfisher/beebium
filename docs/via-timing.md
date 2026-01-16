# VIA 6522 Timing and System Integration

This document describes the timing characteristics of the 6522 Versatile Interface Adapter (VIA) and its integration with the 6502 CPU in the BBC Micro. The information is based on research from the stardot.org.uk forums, particularly the thread on VIA emulation quality, and validated through comprehensive timing tests.

## Two-Phase Clock Model

The VIA operates on a two-phase clock system synchronized with the 6502 CPU:

```
        ┌───────┐       ┌───────┐       ┌───────┐
PHI2    │       │       │       │       │       │
    ────┘       └───────┘       └───────┘       └────
        ↑               ↑               ↑
        leading         leading         leading
        edge            edge            edge
                ↑               ↑               ↑
                trailing        trailing        trailing
                edge            edge            edge
```

- **Trailing edge (PHI2 falling)**: Timer counters decrement, port pins are sampled
- **Leading edge (PHI2 rising)**: IRQ flags are set, timeout states are evaluated

This separation is critical for correct emulation of same-cycle behaviors.

## Timer Counter Sequences

### T1 Timer (One-Shot Mode)

When T1CH is written, the timer loads and begins counting:

```
Cycle   T1 Counter   Events
─────   ──────────   ──────
  0     [latch]      T1CH write loads counter from latch
  1     N            Decrement
  2     N-1          Decrement
  ...
  N     1            Decrement
  N+1   0            Decrement
  N+2   0xFFFF       Timeout: IRQ flag set on leading edge
  N+3   [latch]      Counter reloads from latch (but t1_pending=false)
```

Key observations:
- The counter passes through 1, then 0, then 0xFFFF before the IRQ fires
- In one-shot mode, `t1_pending` is cleared after the first timeout
- The counter continues free-running but generates no further IRQs

### T1 Timer (Continuous Mode)

In continuous mode (ACR bit 6 = 1):
- After timeout, `t1_pending` remains true
- Counter reloads from latch and continues generating periodic IRQs
- PB7 toggles on each timeout (if ACR bit 7 = 1)

### T2 Timer

T2 operates similarly but only in one-shot mode for clock counting. It also supports pulse counting mode (see below).

## Same-Cycle IRQ Acknowledgment

A critical timing subtlety exists when reading the timer counter low byte during the same cycle that an IRQ fires:

```
Cycle N (trailing edge): T1 decrements to 0xFFFF, t1_timeout set
Cycle N (leading edge):  IFR.T1 set to 1
```

If the CPU reads T1CL during cycle N:
- The read occurs during the CPU's PHI2 high phase
- The IRQ flag was just set on the leading edge
- **The read should NOT clear the IRQ flag**

This is the "500ns delay" behavior. The IRQ flag can only be cleared by a read in a subsequent cycle.

### Implementation

We use timeout flags (`t1_timeout`, `t2_timeout`) to protect against same-cycle clearing:

```cpp
case REG_T1CL:
    if (!state_.t1_timeout) {
        state_.ifr.bits.t1 = 0;  // Only clear if not timeout cycle
    }
    return state_.t1 & 0xFF;
```

## T1LH Write Behavior

Writing to T1LH (the latch high byte) clears the T1 IRQ flag, but with an exception:

- Normal case: Writing T1LH clears IFR.T1
- Exception: If T1 is timing out this same cycle, the flag is NOT cleared

This allows software to reload the latch without accidentally clearing a pending IRQ.

## T1CH Write During Timeout

When T1CH is written during the same cycle as a timeout:

1. The timeout's leading edge processing runs first
2. IFR.T1 is set by the timeout
3. The T1CH write then starts a new timer period
4. The IRQ from the old timeout is still delivered

We track this with a `t1_started` flag:

```cpp
// Leading edge processing
if (state_.t1_timeout) {
    if (!state_.t1_started) {
        // Only modify t1_pending if this isn't a manual restart
        state_.t1_pending = state_.acr.bits.t1_continuous;
    }
    state_.ifr.bits.t1 = 1;
}
state_.t1_started = false;  // Clear after every leading edge
```

## T2 Pulse Counting Mode

When ACR bit 5 is set, T2 counts falling edges on PB6 instead of the 1MHz clock:

```
PB6     ────┐     ┌─────┐     ┌─────
            │     │     │     │
            └─────┘     └─────┘

T2      N   N     N-1   N-1   N-2
```

### Implementation Details

1. **Counting flag**: `t2_count` controls whether T2 decrements on trailing edge
2. **Edge detection**: Compare current PB6 with `old_pb` to detect falling edges
3. **Initial state**: When T2CH is written in pulse mode, `t2_count` is set to false (wait for first edge)

```cpp
case REG_T2CH:
    // ...
    state_.t2_count = !state_.acr.bits.t2_count_pb6;
    break;
```

### Edge Detection Flow

```
Trailing edge:
  1. Sample PB6 from peripheral into port_b.p
  2. If t2_count && !t2_reload: decrement T2

Leading edge:
  1. Detect falling edge: (old_pb & 0x40) && !(port_b.p & 0x40)
  2. If falling edge detected: t2_count = true
  3. Update old_pb = port_b.p
```

## Port Pin Updates

The VIA must correctly handle peripheral-driven port pins. A common bug is overwriting port values after the peripheral has set them:

```cpp
// WRONG: This overwrites peripheral values
port.p = ~port.ddr | (port.or_ & port.ddr);

// CORRECT: Let update_port_pins() handle it before edge detection
update_port_pins();  // Sets port.p from peripheral
tick_control_phi2_trailing_edge(port, ...);  // Uses port.p for edge detection
```

## IFR/IER RS Latch Behavior

The Interrupt Flag Register uses RS latch semantics:

- **IFR write**: Writing 1 to a bit clears that flag (acknowledges interrupt)
- **Hardware set**: Timer timeout, control line edge, etc. sets the flag

When both occur in the same cycle, the hardware set wins:

```cpp
// IFR write (clear) happens first in CPU cycle
// But if timer times out this cycle, flag is set again on leading edge
if (state_.t1_timeout) {
    state_.ifr.bits.t1 = 1;  // Timer wins
}
```

## 1MHz Bus Stretching

The VIA operates at 1MHz while the CPU runs at 2MHz. When the CPU accesses a VIA register, the bus controller inserts wait cycles to synchronize with the VIA's slower clock. This affects the timing relationship between CPU reads/writes and VIA counter values.

### How It Affects VIA Timing

Consider a sequence of VIA accesses:

```
Instruction 1: STA $FE6B    ; Write to ACR (1MHz access)
Instruction 2: LDA $FE64    ; Read T1CL (1MHz access)
```

Without bus stretching, these would execute with normal instruction timing. With stretching:

1. Each VIA access adds 1-2 extra cycles (depending on phase alignment)
2. During the extra cycles, the VIA timer continues to decrement
3. The CPU read sees the timer value after synchronization completes

### Pre-Tick Model

Beebium implements "Option B" bus stretching:

1. CPU executes until the memory access cycle
2. Before the VIA read/write completes, the VIA is pre-ticked for the stretch duration
3. The memory operation then completes, with the CPU seeing the post-synchronization state
4. Timer counter reads use raw values (skip the normal end-of-cycle prediction)

This ensures that `LDA $FE64` returns the timer value that would be present after the 1MHz bus synchronization, matching real hardware.

### Impact on Timer Reads

When reading T1CL or T2CL during a stretched access:

- The timer decrements during the stretch cycles
- The value returned reflects the post-stretch counter state
- The `skip_next_timer_prediction()` mechanism prevents double-counting

See [Clock Architecture](clock-architecture.md#1mhz-bus-stretching) for the full timing model.

## System Integration

### BBC Micro Memory Map

| Address Range | Device |
|--------------|--------|
| 0xFE40-0xFE4F | System VIA (active bits 0-3) |
| 0xFE60-0xFE6F | User VIA (active bits 0-3) |

Both VIAs are active on 16-byte boundaries with mirroring.

### IRQ Delivery to CPU

```cpp
void Machine::step() {
    // Tick clock (CPU, VIAs, video)
    system_clock_.tick(cycle_count);

    // Poll VIA IRQ outputs
    uint8_t irq_mask = memory_.poll_irq();

    // Update CPU IRQ line
    M6502_SetDeviceIRQ(&cpu_, kViaIrqDeviceMask, irq_mask ? 1 : 0);

    ++cycle_count;
}
```

The IRQ is polled after the clock tick, ensuring the VIA's leading edge processing (which sets IFR) has completed before the CPU sees the IRQ.

### IRQ Handler Requirements

When handling a VIA timer IRQ, the handler must:

1. Read T1CL or T2CL to clear the interrupt flag
2. Or write to IFR to explicitly clear the flag
3. Execute RTI to return from interrupt

Failure to clear the flag results in immediate re-entry to the IRQ handler after RTI.

## Timing Test Categories

Our test suite validates these behaviors:

| Category | Tests | Description |
|----------|-------|-------------|
| A | 4 | Timer counter value sequences |
| B | 5 | Same-cycle IRQ acknowledgment |
| C | 3 | T1LH write behavior |
| D | 5 | T2 pulse counting mode |
| E | 7 | Integrated CPU+VIA timing |

## Reset Behavior

Per the [MOS 6522 datasheet](http://archive.6502.org/datasheets/mos_6522_preliminary_nov_1977.pdf):

> "Reset clears all R6522 internal registers to logic 0 (except T1 and T2 latches and counters and the Shift Register)"

### Registers Cleared at Reset

| Register | Value After Reset |
|----------|-------------------|
| ORA, ORB | 0x00 |
| DDRA, DDRB | 0x00 |
| IFR | 0x00 |
| IER | 0x00 |
| ACR | 0x00 |
| PCR | 0x00 |

### Registers NOT Cleared at Reset (Undefined)

The datasheet explicitly excludes these registers from clearing, meaning their values are undefined:

| Register | Beebium Value | Rationale |
|----------|---------------|-----------|
| T1 counter | 0xFFFE | Matches jsbeeb for differential testing |
| T1 latch | 0xFFFE | Matches jsbeeb for differential testing |
| T2 counter | 0xFFFE | Matches jsbeeb for differential testing |
| T2 latch | 0xFFFE | Matches jsbeeb for differential testing |
| Shift Register | 0x00 | Both jsbeeb and Beebium use 0 (technically incorrect but consistent) |

### Differential Testing Compatibility

Where the datasheet specifies undefined values, Beebium matches jsbeeb's choices to minimize divergence during differential testing. jsbeeb uses an internal 17-bit counter representation (0x1FFFE) for timers; the lower 16 bits (0xFFFE) are used in Beebium.

The timer `_pending` flags are initialized to `false`, equivalent to jsbeeb's `t1hit = t2hit = true`, preventing spurious timer interrupts before MOS initializes the timers.

### Control Lines at Reset

The CA1, CA2, CB1, CB2 control lines are inputs (except when configured as outputs via PCR). Their state after reset depends on external hardware, not the VIA itself. In the BBC Micro:

- **CA1**: Connected to vertical sync (negative edge triggers interrupt)
- **CA2**: Connected to keyboard (positive edge triggers interrupt)
- **CB1**: Connected to A/D conversion complete
- **CB2**: Connected to light pen strobe

The IFR flags for these lines will be set when transitions occur on the external signals, not at reset.

## References

- [Stardot Forum: VIA Emulation Quality](https://stardot.org.uk/forums/viewtopic.php?t=16138)
- [Stardot Forum: VIA Initialization and PB7 Behavior](https://stardot.org.uk/forums/viewtopic.php?t=16081) - Discussion of ORB/DDRB initialization differences between emulators; T1 PB7 output behavior when configured as input (relevant to Snapper); comparison of MAME, b-em, jsbeeb, and beebem 6522 implementations
- [Stardot Forum: VIA/IRQ Timing Validation](https://stardot.org.uk/forums/viewtopic.php?t=25427) - Discussion of using "Nightshade" and other timing-sensitive software to validate VIA+CPU interrupt timing accuracy; includes test program for measuring variable interrupt latency
- [jsbeeb VIA Tests](https://github.com/mattgodbolt/jsbeeb/blob/master/tests/integration/via.js) - Hardware-validated timing tests by @scarybeasts
- MOS 6522 Versatile Interface Adapter Datasheet
- BBC Microcomputer Service Manual

## See Also

- [Clock Architecture](clock-architecture.md) - Overall timing model including 1MHz bus stretching
