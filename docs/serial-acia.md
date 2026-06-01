# Serial Port (MC6850 ACIA + Serial ULA)

This document describes Beebium's emulation of the BBC Micro's on-board serial
hardware: the **Motorola MC6850 ACIA** at `&FE08-&FE0F` and the **Ferranti
Serial ULA (SERPROC)** at `&FE10-&FE1F`. Together these implement the BBC's
RS423 / cassette serial interface.

The emulation is a faithful bit-level model (in the style of b2's `MC6850`/`SERPROC`).

## Hardware overview

On a real BBC the serial path is:

```
CPU  <->  MC6850 ACIA (&FE08/&FE09)  <->  Serial ULA (&FE10)  <->  RS423 / cassette
```

- The **ACIA** holds the transmit/receive shift registers, the control and
  status registers, and generates the serial IRQ. It does **not** generate its
  own bit-rate clock.
- The **Serial ULA** supplies the transmit and receive bit clocks (selecting the
  baud rate), chooses between the RS423 connector and the cassette interface
  (driving the ACIA's `/DCD` input), and drives the cassette motor relay.

### Register map

| Address       | Access | Function |
|---------------|--------|----------|
| `&FE08`       | read   | ACIA Status Register |
| `&FE08`       | write  | ACIA Control Register |
| `&FE09`       | read   | ACIA Receive Data Register (RDR) |
| `&FE09`       | write  | ACIA Transmit Data Register (TDR) |
| `&FE10`       | write  | Serial ULA control latch |

The ACIA occupies `&FE08-&FE0F` mirrored on the low address bit; the Serial ULA
occupies `&FE10-&FE1F` (a write-only latch). Reads of the Serial ULA return fast
2MHz open bus (the last value on the data bus), consistent with the rest of the
SHEILA I/O region.

### ACIA control register

| Bits | Field | Notes |
|------|-------|-------|
| 0-1  | Counter divide select | `00`=/1, `01`=/16, `10`=/64, `11`=master reset |
| 2-4  | Word select | data bits / parity / stop bits (e.g. `101` = 8N1) |
| 5-6  | Transmitter control | `/RTS` level, TX IRQ enable, transmit break |
| 7    | Receive interrupt enable | |

The MOS programs `&15` for the standard configuration: /16 divide, 8N1,
`/RTS` low, RX interrupt enabled.

### ACIA status register

| Bit | Flag | Meaning |
|-----|------|---------|
| 0 | RDRF | Receive Data Register Full |
| 1 | TDRE | Transmit Data Register Empty (masked when `/CTS` high) |
| 2 | /DCD | Data Carrier Detect (1 = no carrier) |
| 3 | /CTS | Clear To Send (1 = not clear to send) |
| 4 | FE   | Framing Error |
| 5 | OVRN | Receiver Overrun |
| 6 | PE   | Parity Error |
| 7 | IRQ  | Interrupt Request |

### Serial ULA control latch

| Bits | Field |
|------|-------|
| 0-2  | Transmit baud-rate select |
| 3-5  | Receive baud-rate select |
| 6    | RS423 (1) / cassette (0) select — drives the ACIA `/DCD` input |
| 7    | Cassette motor relay |

Baud-rate select values (per the SERPROC encoding, as set via `OSBYTE 7`/`8`):

| Value | Baud  |
|-------|-------|
| 0     | 19200 |
| 1     | 1200  |
| 2     | 4800  |
| 3     | 150   |
| 4     | 9600  |
| 5     | 300   |
| 6     | 2400  |
| 7     | 75    |

## Implementation

| Component | File | Role |
|-----------|------|------|
| `Mc6850`        | `devices/Mc6850.hpp`       | Bit-level ACIA: control/status registers, TX/RX bit state machines, parity/framing/overrun, IRQ. |
| `SerialUla`     | `serial/SerialUla.hpp`     | SERPROC latch decode + byte↔bit shifter between the ACIA and the host transport. |
| `SerialSocket`  | `serial/SerialSocket.hpp`  | Owns the ACIA + ULA, exposes the two memory-mapped regions, clocking, IRQ, reset. |
| `SerialDataSource` / `SerialDataSink` | `serial/SerialDevice.hpp` | Byte-oriented host-transport seam (+ `LoopbackSerialEndpoint` for tests). |
| `HasSerialSocket` | `serial/SerialConcepts.hpp` | Detects hardware variants that carry a `serial_socket`. |

### Bit-level model

The ACIA walks a start / data / (parity) / stop-bit state machine one bit at a
time, exactly as the real device does:

- `Mc6850::update_transmit()` returns the next serial bit and its role
  (`Start`/`Data`/`Stop`/...). The `SerialUla` assembles the data bits (LSB
  first) into a byte and hands the byte to the sink on the stop bit.
- `Mc6850::update_receive(bit)` is fed one bit at a time. The `SerialUla` pulls
  a byte from the source and shifts it in as start + 8 data bits (LSB first) +
  stop. On the closing stop bit the ACIA latches RDR, sets RDRF, and raises the
  RX interrupt (subject to the receive-interrupt-enable bit).

Framing errors (bad stop bit), parity errors, and receiver overrun (a new
character arriving before RDR is read) are all modelled and surface in the
status register.

### Timing

`SerialUla::tick()` is called once per 2MHz CPU cycle from `Machine::step()`
(both the normal and bus-stretch paths), guarded by `HasSerialSocket`. The
number of ticks per serial bit is `CPU_HZ / baud` (e.g. 104 ticks/bit at 19200
baud). Transmit and receive have independent bit-clock timers derived from the
two baud-rate fields in the Serial ULA latch.

This is a faithful **bit-level** model at an emulator-friendly cadence; it is
not cycle-accurate to the ACIA's 16× sampling clock. The BBC's software (and
fujinet-nio) only depend on correct byte framing at the selected rate. This
mirrors the pragmatic timing approach already taken by the MC6854 byte-trickle
model in the Econet subsystem.

### IRQ wiring

Unlike Econet (which is an NMI source gated through the INTON/INTOFF
flip-flop), the ACIA interrupt is wired to the **shared CPU IRQ line**.
`SerialSocket::irq_pending()` is therefore added to each Model B variant's
`IrqAggregator` (bit 4) and is sampled by `Machine::step()` via `poll_irq()`
alongside the VIAs, Tube, and 1MHz bus.

### Host transport

The `SerialDataSource`/`SerialDataSink` interfaces are the seam between the
emulator core and the host transport. The core ships two endpoints:

- `LoopbackSerialEndpoint` — an in-memory queue (used by unit tests and as a
  simple echo: attach it as both source and sink and transmitted bytes come
  back round to the receiver).
- `ScriptableSerialEndpoint` — a thread-safe, two-channel endpoint (device→Beeb
  and Beeb→device queues). It is the serial analogue of the Econet
  `TestBackend`: a deterministic, in-process transport that a client can drive
  from outside the emulation thread.

A real transport — e.g. a threaded PTY/serial bridge to a running fujinet-nio
instance, reusing the existing `piconet::SerialPort` POSIX/Win32 backends —
plugs in at the application/server layer via `SerialSocket::set_source()` /
`set_sink()`. Implementations that bridge to a background I/O thread must be
internally thread-safe; the source/sink methods are called from the emulation
thread.

### gRPC service and clients

`SerialService` (`src/service/proto/serial.proto`,
`src/service/include/beebium/service/SerialService.hpp`) exposes the serial
port over gRPC. It is registered in `Server.hpp` alongside the other services
and attaches a `ScriptableSerialEndpoint` by default. RPCs:

| RPC | Purpose |
|-----|---------|
| `GetSerialStatus`   | ACIA + Serial ULA register snapshot, endpoint mode, pending byte counts. |
| `SetEndpointMode`   | Select `NONE` / `LOOPBACK` / `SCRIPTABLE` (swaps source/sink with the emulation loop paused). |
| `SendToDevice`      | Inject bytes for the BBC to receive (scriptable mode). |
| `ReceiveFromDevice` | Collect bytes the BBC has transmitted (scriptable mode). |

`SendToDevice`/`ReceiveFromDevice` operate only on the scriptable endpoint's
mutex-protected queues, so they run without pausing the machine; the emulation
thread accesses the same queues under the same locks.

The Python client wraps this as `beebium.serial.Serial` (exposed as
`bbc.serial`), with an `EndpointMode` enum and a `SerialStatus` dataclass. See
`clients/python/examples/serial_demo.py` for a runnable end-to-end demo
(loopback echo + scriptable inject/collect) and `clients/python/tests/test_serial.py`
for mock-based unit tests plus real-server integration tests.

## Tests

| Test | Coverage |
|------|----------|
| `tests/test_mc6850.cpp`       | ACIA reset/control decode, TX/RX bit framing, framing/parity errors, overrun, IRQ gating. |
| `tests/test_serial_ula.cpp`   | ULA baud/motor/RS423 decode, bit-period derivation, byte↔bit shifting through the ACIA, full loopback. |
| `tests/test_serial_socket.cpp`| `SerialSocket` behaviour and Model B memory-map integration (region mapping, mirroring, IRQ reaching the aggregator). |

## Status / future work

- **Done**: bit-level ACIA + Serial ULA, `SerialSocket`, wiring into all Model B
  variants (memory map, IRQ, reset, clocking), the `SerialDataSource`/`Sink`
  seam with loopback + scriptable endpoints, C++ unit tests, the `SerialService`
  gRPC surface, the `beebium.serial` Python client, Python unit + integration
  tests, the `serial_demo.py` example, and this document.
- **Pending**: the host PTY/serial transport backend and the CLI / config wiring
  to point it at a fujinet-nio PTY. This is where the final end-to-end
  validation (and the TX-coalescing/"debounce" tuning observed in b2) belongs.
  The `ScriptableSerialEndpoint` and `SerialService` already provide everything
  needed to demo and test the serial stack without a real FujiNet.
