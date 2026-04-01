# Acorn User Port Real Time Clock Module

## Overview

The Acorn Econet Level 3 File Server Real Time Clock Module is a small potted
unit that plugs into the BBC Micro's User Port. It provides:

1. **Real-time clock** -- the Level 3 file server reads the current date and
   time from this device to timestamp files and directories.
2. **Hardware dongle** -- the file server verifies the module's presence at
   startup by writing and reading back a known value. Without the module (or
   its emulation), the server reports "Clock failure" and refuses to start.

The module was sold by Acorn as part of the Econet Level 3 File Server package.
Its internal chip remained unidentified for roughly 38 years until the Stardot
community reverse-engineered it and identified it as a **Signetics/Philips
SAF3019P** CMOS clock/timer IC.


## The SAF3019P Chip

The SAF3019P is a CMOS real-time clock/timer in a 16-pin DIP package (SOT-38D),
manufactured by Signetics (later Philips). It is designed for battery-backed
timekeeping with a 32.768 kHz crystal oscillator.

### Capabilities

- Counts **minutes, hours, days, and months** with automatic carry and correct
  month lengths (28/29/30/31 days)
- 8 registers: 4 time counters + 4 alarm ("time") registers
- All values stored in BCD
- Battery backup (1.3V--2.6V on VSS1) for timekeeping when powered off
- Comparator output (COMP) for alarm matching
- 1 pulse/second and 1 pulse/minute outputs

### Limitations

- **No seconds counter** accessible via the serial interface (internal only)
- **No year counter** -- there is no year register at all
- **No day-of-week**
- The alarm registers (registers 1, 3, 5, 7) are freely writable, so Acorn
  repurposes them for year storage and dongle detection

### Pinout

| Pin | Name | Function                                              |
|-----|------|-------------------------------------------------------|
| 1   | NODA | Comparator mode select input                          |
| 2   | F50  | 50 Hz mains frequency input (grounded in Acorn design)|
| 3   | COMP | Comparator output (alarm)                             |
| 4   | DATA | Bidirectional serial data (CBUS, open-drain)          |
| 5   | DLEN | Data line enable input (CBUS)                         |
| 6   | CLB  | Clock burst input (CBUS)                              |
| 7   | POWF | Power failure output                                  |
| 8   | VSS2 | Ground                                                |
| 9   | MIN  | 1 pulse per minute output                             |
| 10  | SEC  | 1 pulse per second output                             |
| 11  | FSET | Frequency setting output (128 Hz)                     |
| 12  | TEST | Test mode input (must be grounded for normal operation)|
| 13  | OSCI | Crystal oscillator input                              |
| 14  | OSCO | Crystal oscillator output                             |
| 15  | VSS1 | Negative battery supply (1.5V typical)                |
| 16  | VDD  | Positive supply (+5V)                                 |


## Hardware Connections to the User Port

The module connects the SAF3019P to the BBC Micro's User VIA (6522) Port B:

| SAF3019P Pin | SAF3019P Signal | User VIA Bit | Direction (from BBC) |
|--------------|-----------------|--------------|----------------------|
| 4            | DATA            | PB0          | Bidirectional        |
| 6            | CLB             | PB1          | Output (BBC clocks)  |
| 5            | DLEN            | PB2          | Output (BBC enables) |
| 3            | COMP            | PB5          | Input (alarm status) |
| 7            | POWF            | PB7          | Input (power fail)   |

Other connections:

- Pins 13/14: 32.768 kHz crystal with 10 pF load capacitors
- Pin 15 (VSS1): 1.5V battery (SR44 silver oxide or similar)
- Pin 16 (VDD): +5V from User Port
- Pin 8 (VSS2): Ground
- Pin 12 (TEST): Grounded (critical -- floating TEST causes erratic oscillator
  behaviour, a finding from Ken Lowe's recreation project)
- Pin 2 (F50): Grounded (crystal mode, not mains frequency)


## CBUS Serial Protocol

The SAF3019P uses a proprietary 3-wire serial protocol called **CBUS** (not I2C,
not SPI):

- **DATA** (pin 4): Bidirectional, N-channel open-drain
- **DLEN** (pin 5): Data Line Enable -- HIGH indicates a transmission is active
- **CLB** (pin 6): Clock Burst -- the BBC provides clock pulses; data is
  sampled on CLB edges

### Word Formats

The CBUS defines three word types:

**TIME ADDRESS** (register selection for reading):
- 4 CLB pulses: start bit + 3 address bits (S, A0, A1)
- Followed immediately by a TIME READ

**TIME READ** (returns register data):
- 8 CLB pulses: start bit + 7 BCD data bits
- Data is driven by the SAF3019P (BBC reads PB0)

**TIME SET** (write a value to a register):
- 12 CLB pulses: start bit + 3 address bits + 7 BCD data bits + 1 bit

### Protocol Sequence (Read)

1. BBC sets DDRB to `&A7` (PB0/1/2 as outputs, PB5/7 as inputs)
2. DLEN goes HIGH (PB2 = 1)
3. BBC clocks out the 3-bit register address on DATA (PB0), toggling CLB (PB1)
4. BBC switches DDRB to `&06` (PB0 becomes input for reading)
5. BBC clocks CLB while reading DATA (PB0) for 7 bits
6. Load pulse: CLB LOW while DLEN goes LOW
7. Minimum 2 us busy period before next transmission

### Protocol Sequence (Write)

1. BBC sets DDRB to `&A7`
2. DLEN goes HIGH
3. BBC clocks out 3 address bits + 7 BCD data bits on DATA, toggling CLB
4. Load pulse and busy period as above

### Timing Parameters

| Parameter                  | Min    | Max     |
|----------------------------|--------|---------|
| CLB pulse width HIGH/LOW   | 4 us   | --      |
| Data setup (before CLB)    | 1 us   | --      |
| Data hold (after CLB)      | 2 us   | --      |
| Enable setup (DLEN before CLB) | 2 us | --    |
| CLB frequency              | 0      | 100 kHz |
| Busy time after load pulse | 2 us   | --      |


## Register Map

The SAF3019P has 8 registers addressed by 3 bits (S, A0, A1). Registers 0/2/4/6
are live time counters; registers 1/3/5/7 are alarm registers repurposed by
Acorn:

| Reg | S,A0,A1 | Datasheet Name      | BCD Range | Acorn's Use                       |
|-----|---------|---------------------|-----------|-----------------------------------|
| 0   | 0,0,0   | Month counter       | 01--12    | Current month                     |
| 1   | 1,0,0   | Month alarm register| 00--19    | Year (offset from 1981)           |
| 2   | 0,1,0   | Date counter        | 01--31    | Current day of month              |
| 3   | 1,1,0   | Date alarm register | 01--31    | Previously stored month (rollover)|
| 4   | 0,0,1   | Hours counter       | 00--23    | Current hour                      |
| 5   | 1,0,1   | Hours alarm register| 00--23    | Unused (or extended year bits)    |
| 6   | 0,1,1   | Minutes counter     | 00--59    | Current minute                    |
| 7   | 1,1,1   | Minutes alarm register| 00--59  | Dongle detection value            |

### Register Peculiarities

**Month counter (register 0):** Writing `0` does not set the month to zero.
Instead it resets the internal prescaler and seconds counter. If the seconds
counter was between 30 and 59, the minutes counter is incremented (a coarse
time correction feature).

**Month alarm register (register 1):** Only accepts values 0--19. Values 20+
are wrapped using a complex modular scheme (see the wrapping behaviour section
below). Acorn stores the year here as an offset from 1981, giving a range of
1981--2000.

**Minutes alarm register (register 7):** Used by the Level 3 file server for
dongle detection (see below).

### BCD Value Wrapping

When writing to registers, the SAF3019P wraps out-of-range values rather than
rejecting them. The wrapping rules differ by register type:

- **Month counter/alarm (regs 0, 1):** Values wrap modulo 20 with specific
  range-dependent offsets
- **Date/hour registers (regs 2--5):** Values wrap modulo 40
- **Minute registers (regs 6, 7):** Values wrap modulo 80/100

BeebEm's implementation encodes the exact wrapping behaviour observed through
testing, which follows a repeating pattern of subtract-offset-modulo operations.


## BBC Software Interface

The BBC accesses the User Port RTC by bit-banging the CBUS protocol through
OS calls:

- **OSBYTE 150** (write to User VIA register): Used to set DDRB and ORB
- **OSBYTE 151** (read from User VIA register): Used to read IRB

### DDR Configuration

```
Address phase:  DDRB = &A7  (PB0,1,2,5,7 = output; PB3,4,6 = input)
Read phase:     DDRB = &06  (PB1,2 = output; PB0 = input for reading DATA)
```

### Bit-Banging Encoding

Each bit is sent as a two-byte sequence to ORB (User VIA register &60):

| Bit Value | First Write | Second Write | Effect                    |
|-----------|-------------|--------------|---------------------------|
| 1         | &07         | &05          | DATA=1, CLB=1 then CLB=0  |
| 0         | &06         | &04          | DATA=0, CLB=1 then CLB=0  |

The end-of-transmission sequence is: `&00, &02, &00, &00`.


## Dongle Detection (Copy Protection)

The Level 3 File Server performs a hardware presence check at startup:

1. Write `&13` to register 7 (minutes alarm)
2. Read register 7 and verify it equals `&13`
3. Write `&00` to register 7
4. Read register 7 and verify it equals `&00`

If any step fails, the server displays "Clock failure" and halts. This is
effectively a hardware dongle -- without the physical module (or emulation),
the software will not run.

The Level 3 File Server source code (version 1.06 onwards) includes a
build-time `DONGLE` flag that can disable this check, presumably used by Acorn
during development. Patched versions of the file server ROM exist that bypass
the check, but emulating the hardware is the correct approach for running
unmodified software.


## Existing Emulation in Other Emulators

### BeebEm (Full Emulation)

BeebEm has complete SAF3019P emulation, implemented by Ken Lowe in 2004:

- **Files:** `Src/UserPortRTC.h`, `Src/UserPortRTC.cpp`
- **Approach:** Protocol-level emulation at the VIA port write/read level
- **Time source:** Host system clock for time counters (registers 0, 2, 4, 6)
- **Alarm registers:** Stored in a persistent array `UserPortRTCRegisters[8]`
- **Persistence:** Register state saved to `Preferences.cfg`
- **Toggle:** `Hardware > User Port RTC Module` menu option

The implementation intercepts three User VIA operations:

1. **`UserPortRTCWrite(value)`** -- called on ORB write (VIA register 0).
   Detects falling edges on PB1 (CLB). When PB2 (DLEN) is high, shifts in
   command/address bits. When DLEN goes low, either writes to a register
   (if 11 bits received) or reads a register (otherwise), using the host
   system clock for time counter values.

2. **`UserPortRTCReadBit()`** -- called on IRB read (VIA register 0). Returns
   the next bit from the shift register, LSB first.

3. **`UserPortRTCResetWrite()`** -- called on DDRB write (VIA register 2).
   Resets the bit counter when all 3 low bits are set as outputs.

### B2

No User Port RTC emulation. Only emulates the MC146818 for Master 128.

### B-Em

No User Port RTC emulation. Only emulates the HD146818 for Master 128.


## Beebium Implementation Considerations

### Integration Point

Beebium's `ViaPeripheral` abstract interface is the natural integration point.
A `UserPortRtcPeripheral` class would implement `update_port_b()` to intercept
the CBUS protocol, exactly as BeebEm does at its User VIA write/read hooks.

```
class UserPortRtcPeripheral : public ViaPeripheral {
    uint8_t update_port_b(uint8_t output, uint8_t ddr) override;
};
```

The `update_port_b()` method receives the output register value and DDR on
every port interaction, providing the falling-edge detection needed for the
CBUS clock. The return value provides the input pin state including the DATA
bit read back.

### Design Choices

1. **Protocol-level vs register-level emulation:** BeebEm's protocol-level
   approach (detecting clock edges in port writes) is appropriate. The BBC
   software bit-bangs at a pace many orders of magnitude slower than the
   emulation, so timing precision is not critical -- only the logical sequence
   of edges matters.

2. **Time source:** Use the host system clock for time counter registers
   (0, 2, 4, 6), as BeebEm does. This avoids the need to simulate a 32.768 kHz
   crystal oscillator and gives the user accurate real-world time.

3. **Alarm register persistence:** The year (register 1) and other alarm
   register values must persist across emulator sessions. This could be handled
   through the gRPC service layer or a configuration file.

4. **gRPC exposure:** A new RTC-related service or extension to SystemService
   could allow frontends to enable/disable the User Port RTC and configure the
   initial register state (particularly the year).

5. **Wrapping behaviour:** The BCD value wrapping in BeebEm's implementation
   appears to be derived from empirical testing rather than the datasheet.
   Beebium should replicate this exactly, since the Level 3 file server's
   time-setting code may depend on wrapping behaviour.


## Year 2000 Problem

The original Acorn design stores the year as a 4-bit BCD offset from 1981 in
register 1, supporting only 1981--2000 (values 0--19).

### Community Workarounds

**BeebMaster's Y2KFIX:** Stores year bits 0--3 in register 1 and bits 4--6 in
register 5 (hours alarm, previously unused). The year is reconstructed as:
`year = 1981 + reg1 + (reg5 * 16)`, supporting years up to 2108.

**mm67's patched Level 3 FS (v1.26):** Stores a 7-bit binary year (0--99)
directly in register 7, but this conflicts with the dongle detection value and
requires a patched file server ROM.

### Date Rollover Bug

Setting the time to 23:59 on a month boundary when seconds > 30 can cause the
date to skip a day. The seconds reset operation (writing 0 to register 0)
increments the minutes counter when seconds >= 30, which cascades to
hours/date. The workaround is to call seconds reset before changing time/date
values.


## Ken Lowe's Hardware Recreation

Ken Lowe (Stardot forum user KenLowe) designed replacement PCBs for the
original Acorn module:

- Both SMD and through-hole variants
- Uses genuine SAF3019P chips (from remaining stock; the chip is discontinued)
- SR44 silver oxide battery (1.55V, expected ~2 years at 10 uA)
- Green LED for data traffic, red LED for 1 Hz clock pulse
- 32.768 kHz crystal with 10 pF load capacitors
- 90-degree User Port connector

**Critical finding:** Pin 12 (TEST) must be grounded. A floating TEST pin
causes erratic oscillator behaviour -- this was the key discovery that made the
recreation work.

**Battery voltage:** A 3V battery (e.g., CR2032) without a dropping diode
causes the oscillator to race. The SAF3019P is specified for 1.3V--2.6V battery
voltage.


## References

### Stardot Forum Threads

- [Clock Failure -- original investigation thread](https://stardot.org.uk/forums/viewtopic.php?t=20050)
  Discussion of the Level 3 File Server clock module, chip identification,
  register-level reverse engineering
- [Ken Lowe's RTC recreation (post #352389)](https://stardot.org.uk/forums/viewtopic.php?p=352389#p352389)
  PCB design, pin 12 grounding discovery, battery considerations
- [BBC Micro Real Time Clock Module](https://stardot.org.uk/forums/viewtopic.php?t=20108)
  Additional hardware discussion
- [Original RTC investigation (2014)](https://stardot.org.uk/forums/viewtopic.php?t=5164)
  Earlier community investigation

### Datasheets

- [SAF3019P Datasheet (Signetics)](http://www.elektronikjk.pl/elementy_czynne/IC/SAF3019P.pdf)
- [SAF3019P at Datasheet Archive](https://www.datasheetarchive.com/SAF3019P-datasheet.html)

### Emulator Source Code

- BeebEm: `Src/UserPortRTC.cpp`, `Src/UserPortRTC.h` (GPL, Ken Lowe 2004)
  Full protocol-level emulation of the SAF3019P via User VIA Port B
- BeebEm: `Help/rtc.html` -- user documentation for all three RTC types


## Beebium Peripheral Extension Implementation Notes

The User Port RTC should be implemented as a loadable peripheral extension
plugin, following the pattern established by the Acorn SCSI Host Adapter
(`acorn-scsi`) and SCSI Hard Disc (`scsi-hard-disc`) extensions.

### Extension Architecture

```
src/extensions/user-port-rtc/
    manifest.json               # Plugin metadata + CLI parameter schema
    UserPortRtcExtension.hpp    # PeripheralExtension subclass
    UserPortRtcExtension.cpp
    Saf3019p.hpp                # SAF3019P chip emulation (protocol + registers)
    Saf3019p.cpp
    user_port_rtc.proto         # gRPC service definition
    UserPortRtcService.hpp      # gRPC service implementation
    plugin_entry.cpp            # BEEBIUM_PLUGIN_EXPORT entry point
    CMakeLists.txt
```

### Extension Point: User Port

The extension framework currently has one device callback interface:
`OneMHzBusDevice` for 1 MHz bus peripherals. The User Port RTC needs a
second interface type:

```cpp
struct UserPortDevice {
    virtual ~UserPortDevice() = default;
    virtual uint8_t update_port_b(uint8_t output, uint8_t ddr) = 0;
};
```

This mirrors the existing `ViaPeripheral::update_port_b()` signature. The
extension context would provide a `UserPort` handle alongside `OneMHzBusPort`:

```cpp
// In ExtensionContext:
template<> UserPort& get<UserPort>();
```

The User VIA's peripheral callback is then bridged to the UserPort, which
dispatches to attached UserPortDevice instances.

The extension declares `attaches_to: ["user-port"]` in its manifest, and the
machine hardware provides the `"user-port"` extension point.

### Plugin Manifest

```json
{
    "name": "user-port-rtc",
    "description": "Acorn Econet Level 3 File Server Real Time Clock Module (SAF3019P)",
    "library": "user-port-rtc",
    "cli": "rtc",
    "parameters": [
        {
            "key": "time-offset",
            "type": "string",
            "description": "Offset from host wall-clock time (e.g. +1h, -30m, 1985-03-15T10:30)",
            "default": "0"
        },
        {
            "key": "y2k-mode",
            "type": "string",
            "description": "Year storage mode: 'original' (1981-2000), 'beebmaster' (1981-2108)",
            "default": "beebmaster"
        }
    ]
}
```

CLI usage: `--rtc` or `--rtc time-offset=-10y` (set clock 10 years in the past).

### SAF3019P Emulation

The `Saf3019p` class encapsulates the chip state machine and register set,
independent of the extension framework:

- **CBUS protocol state machine:** Tracks CLB edges, DLEN state, bit counter,
  shift register. Driven by `clock_edge()` and `set_dlen()` calls from the
  UserPortDevice wrapper.
- **Register storage:** 8 BCD registers with correct wrapping behaviour per
  register type (month mod 20, date/hour mod 40, minute mod 80).
- **Time source:** Time counter registers (0, 2, 4, 6) are computed from
  `host_wall_clock + time_offset` rather than free-running. This avoids
  needing a 32.768 kHz oscillator simulation and gives accurate time.
- **Alarm/year registers (1, 3, 5, 7):** Stored persistently. Register 1
  holds the year offset; register 7 holds the dongle detection value.

### gRPC Service

```protobuf
service UserPortRtcService {
    // Read/write individual SAF3019P registers (BCD values)
    rpc ReadRegister(ReadRtcRegisterRequest) returns (ReadRtcRegisterResponse);
    rpc WriteRegister(WriteRtcRegisterRequest) returns (WriteRtcRegisterResponse);

    // High-level time access
    rpc GetTime(GetRtcTimeRequest) returns (GetRtcTimeResponse);
    rpc SetTimeOffset(SetRtcTimeOffsetRequest) returns (SetRtcTimeOffsetResponse);

    // Configuration
    rpc GetConfig(GetRtcConfigRequest) returns (GetRtcConfigResponse);
    rpc SetConfig(SetRtcConfigRequest) returns (SetRtcConfigResponse);
}

message GetRtcTimeResponse {
    uint32 year = 1;        // full 4-digit year (e.g. 1985)
    uint32 month = 2;       // 1-12
    uint32 day = 3;         // 1-31
    uint32 hour = 4;        // 0-23
    uint32 minute = 5;      // 0-59
    string y2k_mode = 6;    // "original" or "beebmaster"
    int64 time_offset_seconds = 7;  // offset from host wall-clock
}
```

### Time Offset Model

The emulated BBC Micro's perception of current time is:

    emulated_time = host_wall_clock_utc + time_offset

The offset can be set via:
- CLI parameter at startup (`--rtc time-offset=-10y`)
- gRPC `SetTimeOffset` RPC at runtime (for frontend date/time picker UIs)
- Absolute time specification (`--rtc time-offset=1985-03-15T10:30`) which
  computes the offset as `specified_time - now`

When ADFS or the Level 3 File Server reads time registers, the Saf3019p
class decomposes `emulated_time` into month/day/hour/minute and returns the
appropriate BCD values. Writes to time counter registers adjust the offset
to match.

### IRQ Considerations

The SAF3019P has no interrupt connection to the BBC Micro through the User
Port. The COMP (alarm comparator) output connects to PB5 and POWF (power
fail) connects to PB7, but these are active-low and typically read as high
(no alarm, no power fail). No IRQ routing through OneMHzBusDevice is needed
-- this is purely a polled device accessed through VIA Port B.

### Testing Strategy

Unit tests for the SAF3019P should cover:
- CBUS protocol bit-banging (clock edges, read/write sequences)
- Register read/write with BCD wrapping
- Dongle detection sequence (write &13, read back, write &00, read back)
- Time counter computation from offset
- Y2K mode year encoding/decoding

Integration tests (Python, following the ADFS test pattern) should:
- Boot with Level 3 File Server ROM and verify "Clock failure" does NOT appear
- Verify the file server reads correct date/time
- Test the gRPC time access API
