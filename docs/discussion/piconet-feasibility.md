# Piconet Integration Feasibility

Can an emulated BBC Micro participate in a real, physical Econet network?

This document explores the feasibility of integrating [Piconet](https://github.com/jprayner/piconet) — a USB Econet interface built around a real MC68B54 ADLC — with Beebium's Econet implementation. The goal: an emulated BBC on the wire, indistinguishable from a real one at the protocol level.

## What Piconet Is

Piconet is a USB device containing a genuine ADF10 Econet module — the same daughter board that plugged into a BBC Master to provide Econet connectivity. At its heart is a real Motorola MC68B54 ADLC, the same chip that Beebium already emulates in software. A Raspberry Pi Pico microcontroller bridges between the ADLC's parallel register interface and a USB CDC serial port, exposing a text-based command protocol for sending and receiving Econet frames.

The Piconet firmware runs on two cores: Core 0 handles the USB/serial protocol and command dispatch, while Core 1 manages the time-critical Econet wire protocol — clock synchronisation, flag detection, collision avoidance, and the four-way handshake at wire speed. This division means the host software never needs to meet Econet's microsecond-level timing requirements; it operates at the frame level.

The proposition is straightforward: instead of translating Econet traffic to AUN/UDP packets (as `AunBackend` does today), route frames through a Piconet device to a real Econet cable. No protocol translation, no AUN bridge, no IP network — just raw Econet.

## Architectural Fit — NetworkBackend Is the Right Seam

Beebium's Econet stack was designed with transport independence in mind. The `NetworkBackend` abstract class (`src/core/include/beebium/econet/NetworkBackend.hpp`) defines the frame-level interface between the emulated ADLC and the outside world:

```cpp
class NetworkBackend {
public:
    virtual void send_frame(const NetworkFrame& frame) = 0;
    virtual std::optional<NetworkFrame> receive_frame() = 0;
    virtual bool is_connected() const = 0;
    virtual bool is_receiving_flags() const { return false; }
    virtual bool is_expecting_frame() const { return false; }
};
```

Three implementations exist today:

- **`AunBackend`** — production transport over UDP/AUN
- **`TestBackend`** — deterministic test double with frame injection
- Both plug into `EconetSocket::enable()` via `std::unique_ptr<NetworkBackend>`

A `PiconetBackend : public NetworkBackend` would slot in as a third production backend in the initial implementation. (Long-term, both `AunBackend` and `PiconetBackend` are intended to migrate behind a dedicated Econet Transport extension point — see [Future Direction: Econet Transport Extensions](#future-direction-econet-transport-extensions). For the first cut, in-tree alongside `AunBackend` is the right place.) The wiring point is `EconetSocket::enable()`:

```cpp
void enable(uint8_t station_id, std::unique_ptr<NetworkBackend> backend,
            bool aun_mode = false);
```

Zero changes required to `Mc6854`, `FourWayHandshake`, `EconetSocket`, or any machine-level code. The backend is a pure leaf dependency.

```
NFS ROM ─── 6502 bus ─── EconetSocket ─── Mc6854 ─── FourWayHandshake ─── NetworkBackend
                                                                                │
                                              ┌─────────────────────────────────┤
                                              │                                 │
                                         AunBackend                      PiconetBackend
                                          (UDP/AUN)                     (USB serial/Econet)
                                              │                                 │
                                         UDP socket                     /dev/tty.usbmodem*
                                              │                                 │
                                      AUN peers (IP)               Real Econet cable (wire)
```

## The Two-ADLC Architecture

With Piconet in the path, there are two MC68B54 ADLCs:

1. **Emulated** (`Mc6854` in `src/core/include/beebium/econet/Mc6854.hpp`) — handles the 6502 register interface. The NFS ROM reads SR1/SR2, writes CR1-CR4, pushes bytes into the TX FIFO, and reads bytes from the RX FIFO. This is the ADLC that the BBC's software sees.

2. **Real** (on the ADF10 module inside Piconet) — handles the Econet wire protocol. Flag generation, bit stuffing, CRC computation, collision detection, clock recovery. This is the ADLC that the Econet cable sees.

These are not redundant. They operate at fundamentally different layers: the emulated ADLC provides the register-level interface that BBC software expects, while the real ADLC provides the electrical-level interface that Econet hardware expects.

### Why Not Bypass the Emulated ADLC?

Could we forward register accesses over USB to the real ADLC, eliminating the emulated one?

No. The timing mismatch is insurmountable.

The NFS ROM's NMI handler performs sequences like: read SR1, read SR2, read RX data, write CR2 — four register accesses in roughly 10 microseconds at 2MHz. A USB round-trip (host → device → host) takes approximately 1-4 milliseconds. Register-level forwarding would be 100-400x too slow. The NFS ROM would see stale status registers, miss FIFO data, and misinterpret handshake state.

Frame-level bridging — where complete frames are exchanged between host and device — is the only feasible approach. This is exactly what Piconet's protocol provides, and exactly what `NetworkBackend` abstracts.

## The FourWayHandshake Question

This is the most interesting architectural question: how does the Piconet's wire-level handshake interact with `FourWayHandshake`'s synthetic handshake?

### Background

Econet uses a four-way handshake for reliable data transfer:

1. **Scout** — sender announces intent (destination, port, control byte)
2. **Scout Ack** — receiver acknowledges readiness
3. **Data** — sender transmits payload
4. **Final Ack** — receiver confirms receipt

The NFS ROM drives this step-by-step via the ADLC: it transmits the scout, waits for the scout ack NMI, transmits the data, waits for the final ack NMI. Each step involves multiple register accesses and status polling.

### How AunBackend Handles This

AUN's UDP protocol is two-way: a Unicast packet (data + ack), with no separate scout phase. `FourWayHandshake` bridges this gap by intercepting the raw frames from the ADLC and synthesising the missing handshake phases locally:

**TX path**: NFS ROM sends scout → `FourWayHandshake` captures it, arms a 2.5ms timer → timer fires, generates synthetic scout-ack → NFS ROM sends data → `FourWayHandshake` packs it into an AUN Unicast → `AunBackend` sends UDP → AUN Ack arrives (or timeout) → `FourWayHandshake` generates synthetic final-ack → NFS ROM sees success.

**RX path**: AUN Unicast arrives from `AunBackend` → `FourWayHandshake` constructs a scout frame, delivers it → NFS ROM sends scout-ack → `FourWayHandshake` swallows it, arms timer → timer fires, delivers data frame → NFS ROM sends final-ack → `FourWayHandshake` packs it into an AUN Ack → `AunBackend` sends UDP.

### How Piconet's Protocol Works

Piconet's `TX` command is *atomic* from the host's perspective. The host sends:

```
TX <destStation> <destNetwork> <base64data>
```

And receives one of:

```
TX_RESULT OK
TX_RESULT NO_SCOUT_ACK
TX_RESULT NO_DATA_ACK
TX_RESULT OVERFLOW
TX_RESULT UNDERRUN
TX_RESULT LINE_JAMMED
TX_RESULT NOT_LISTENING
```

Piconet's Core 1 firmware handles the entire four-way handshake on the wire: it sends the scout, waits for the real scout-ack from the real destination station, sends the data, and waits for the real final-ack. All of this happens at wire speed, governed by the real MC68B54 and Econet clock.

For receiving, Piconet sends events:

```
RX_TRANSMIT <scoutData> <dataPayload>
RX_BROADCAST <data>
RX_IMMEDIATE <data>
```

Again, the wire-level handshake has already been completed by the time the host sees the event.

### The Impedance Mismatch

The NFS ROM expects step-by-step handshake progression through the ADLC. Piconet expects atomic frame exchange over serial. This is structurally identical to the AUN impedance mismatch that `FourWayHandshake` was designed to solve.

A `PiconetBackend` would operate in `aun_mode=true`, sitting behind `FourWayHandshake` just as `AunBackend` does. The `NetworkFrame` types (`Unicast`, `Ack`, `Broadcast`, `Immediate`, `ImmReply`) map cleanly to Piconet's command/event vocabulary:

| NetworkFrame type | Piconet TX command                    | Piconet RX event                              |
|-------------------|---------------------------------------|-----------------------------------------------|
| `Unicast`         | `TX`                                  | `RX_TRANSMIT`                                 |
| `Ack`             | (implicit in `TX_RESULT OK`)          | (implicit in handshake)                       |
| `Broadcast`       | `BCAST`                               | `RX_BROADCAST`                                |
| `Immediate`       | `TX` (port 0)                         | `RX_IMMEDIATE` (MachinePeek only — see below) |
| `ImmReply`        | — (no host-driven in-handshake reply) | —                                             |

The `Immediate`/`ImmReply` mapping is not symmetric. Inbound immediate operations other than MachinePeek cannot be serviced by a host-generated reply — see [Immediate Operations Limitation](#immediate-operations-limitation).

### TX Flow (Emulated BBC Sends to Real Network)

```
NFS ROM
  │ writes scout bytes to ADLC registers
  ▼
Mc6854 (emulated)
  │ assembles raw scout frame, calls send_frame()
  ▼
FourWayHandshake
  │ captures scout, arms 2.5ms timer
  │ timer fires → generates synthetic scout-ack → Mc6854 → NFS ROM
  │ NFS ROM writes data bytes to ADLC
  │ Mc6854 assembles raw data frame, calls send_frame()
  │ FourWayHandshake packs scout+data into Unicast NetworkFrame
  ▼
PiconetBackend
  │ encodes as: TX <dest> <net> <base64(scout_extra + payload)>
  │ sends over serial to Piconet device
  ▼
Piconet (real ADLC on wire)
  │ Core 1: sends scout, receives scout-ack, sends data, receives final-ack
  │ Core 0: sends TX_RESULT OK (or error)
  ▼
PiconetBackend
  │ parses TX_RESULT
  │ OK → generates Ack NetworkFrame
  │ error → no Ack (FourWayHandshake watchdog eventually resets)
  ▼
FourWayHandshake
  │ receives Ack → generates synthetic final-ack raw frame
  ▼
Mc6854 → NFS ROM sees successful transmission
```

### RX Flow (Real Network Sends to Emulated BBC)

```
Real Econet station
  │ sends scout+data via wire handshake
  ▼
Piconet (real ADLC on wire)
  │ Core 1: receives scout, sends scout-ack, receives data, sends final-ack
  │ Core 0: sends RX_TRANSMIT <scoutData> <payload>
  ▼
PiconetBackend
  │ parses RX_TRANSMIT, constructs Unicast NetworkFrame
  ▼
FourWayHandshake
  │ constructs raw scout frame, delivers to Mc6854
  │ Mc6854 → NMI → NFS ROM reads scout, sends scout-ack
  │ FourWayHandshake swallows scout-ack, arms timer
  │ timer fires → delivers raw data frame
  ▼
Mc6854 → NMI → NFS ROM reads data, sends final-ack
  │
FourWayHandshake
  │ captures final-ack, packs into Ack NetworkFrame
  ▼
PiconetBackend
  │ (Ack is not forwarded to Piconet — the wire handshake already completed)
```

## Timing Analysis

### Where Timing Is Not a Concern

**ADLC register access** — entirely local. The emulated `Mc6854` responds to 6502 reads and writes at 2MHz with zero USB involvement. The NFS ROM's fast register polling loops run against the emulated ADLC, not the real one.

**NFS ROM polling loops** — the NFS ROM's NMI handler polls SR1/SR2 in tight loops, checking for RDA, FV, TDRA. These loops execute against `Mc6854`'s local state, with the byte trickle timer controlling delivery rate. No network latency is involved.

**FV/PSE timing cascade** — the PSE priority system, FV deferred promotion, and frame boundary tracking all operate within the emulated ADLC. These are the most timing-sensitive parts of the Econet stack, and they are completely unaffected by the choice of backend.

### Where Timing Is a Concern

**End-to-end transaction latency**: A complete TX exchange (serial TX command → Piconet wire handshake → serial TX_RESULT) takes approximately 3-6ms, dominated by USB round-trip latency. Compare this to a real BBC on Econet where a file server response arrives in ~1ms. The emulated BBC will be slightly slower at bulk transfers, but this is unlikely to be noticeable for interactive use.

**FourWayHandshake's synthetic scout-ack timing**: When the emulated BBC transmits, `FourWayHandshake` generates a synthetic scout-ack after 2.5ms (`SCOUT_ACK_TIMEOUT = 5000` ticks at 2MHz). This happens *before* the real wire handshake completes. The NFS ROM receives the fake scout-ack and immediately sends data, which `FourWayHandshake` buffers and forwards to `PiconetBackend`. The real handshake then proceeds on the wire.

The risk: if the real destination station doesn't respond (Piconet returns `TX_RESULT NO_SCOUT_ACK`), the BBC has already sent its data frame based on the fake scout-ack. `FourWayHandshake` can't un-deliver the scout-ack. The `FINAL_ACK_TIMEOUT` (5ms) and `WATCHDOG_TIMEOUT` (250ms) will eventually reset the handshake, but the NFS ROM sees an unexplained timeout rather than a clean "not listening" failure.

This is the same trade-off that exists with `AunBackend` — AUN also has no separate scout phase, so failures manifest as timeouts rather than immediate rejections. In practice, NFS operations use `OSWORD &10/&11` with generous timeouts (measured in seconds), so the additional latency is well within tolerance.

**Why timing is probably fine for practical use**: Piconet's Core 1 handles all Econet wire timing independently of USB latency. The wire-level handshake runs at full Econet speed. USB latency only affects the delay between "frame handed to PiconetBackend" and "result returned from Piconet" — and this delay is invisible to the BBC, which is already waiting for `FourWayHandshake`'s synthetic handshake events.

## Piconet Protocol Summary

Piconet communicates over USB CDC serial using a text-based, newline-delimited protocol. Binary payloads are base64-encoded.

### Commands (host → device)

| Command                                       | Description                                   |
|-----------------------------------------------|-----------------------------------------------|
| `SET_MODE STOP`                               | Disable Econet activity                       |
| `SET_MODE LISTEN`                             | Normal operation: respond to addressed frames |
| `SET_MODE MONITOR`                            | Promiscuous: capture all traffic              |
| `SET_STATION <n>`                             | Set this device's Econet station number       |
| `TX <destStn> <destNet> <scoutB64> <dataB64>` | Transmit with four-way handshake              |
| `BCAST <destNet> <dataB64>`                   | Broadcast (fire-and-forget)                   |
| `STATUS`                                      | Query device state                            |

### Events (device → host)

| Event                              | Description                              |
|------------------------------------|------------------------------------------|
| `RX_TRANSMIT <scoutB64> <dataB64>` | Received a unicast (handshake completed) |
| `RX_BROADCAST <dataB64>`           | Received a broadcast                     |
| `RX_IMMEDIATE <dataB64>`           | Received an immediate operation          |
| `MONITOR <dataB64>`                | Traffic capture (MONITOR mode only)      |
| `TX_RESULT <code>`                 | Result of a TX or BCAST command          |
| `ERROR <message>`                  | Device error                             |

### TX_RESULT Codes

`OK`, `NO_SCOUT_ACK`, `NO_DATA_ACK`, `OVERFLOW`, `UNDERRUN`, `LINE_JAMMED`, `NOT_LISTENING`, `NO_CLOCK`, `TIMEOUT`.

These map naturally to Econet failure modes. `OK` produces an `Ack` frame for `FourWayHandshake`; all others produce no response, letting the watchdog timer clean up.

## Immediate Operations Limitation

Piconet's firmware has no working support for host-driven in-handshake replies. The machinery was built and then deliberately disabled in February 2023 (commit `168d466` on `jprayner/piconet`, "WiP reply logic to unpick/revert if unnecessary"). The `REPLY` command and `needs_reply`/`reply_id` fields remain plumbed end-to-end, but `_pending_reply.valid` is never set, so the path is dead code with a live entry point.

The reason is architectural, not incidental. Between a client's scout and the server's in-handshake data frame, the line must be held in `CR2_FLAG_IDLE` (flag-fill). Every microsecond the host takes to produce a reply — USB-CDC round trip, host scheduler jitter, driver stack overhead — is time spent actively jamming the Econet segment. The firmware author's explicit fallback when the host misses the 250ms timeout is to drop flag-fill "to avoid jamming network", which means any host-side hiccup corrupts wire state. In practice, inline ack customisation with pre-canned, firmware-resident bytes is feasible (and is exactly what MachinePeek does); host-round-trip replies are not.

This removes one capability from `PiconetBackend` relative to `AunBackend`:

### What Still Works

- **Emulated BBC as client** (NFS/ADFS/printer against real servers): standard four-way traffic. Unaffected.
- **Emulated BBC as server** (emulated fileserver/printer serving real clients): Acorn's fileserver protocol already replies via a *fresh* four-way handshake to the scout's reply port, not an in-handshake splice. Maps cleanly to Piconet's `TX`/`RX_TRANSMIT`. Unaffected.
- **Broadcasts**: fire-and-forget. Unaffected.
- **Inbound MachinePeek** (immediate op, control byte `0x88`): handled entirely in Piconet firmware with a canned machine-type response. The emulated BBC never sees the peek, and peers receive a valid reply — though it will be *Piconet's* advertised machine type, not one that reflects the emulated variant (Model B / B+ / Master). If station identity matters for `*STATIONS`-style enumeration, that is a Piconet firmware concern, not a `PiconetBackend` one.

### What Does Not Work

Inbound immediate operations other than MachinePeek: `Halt`/`Continue` (`0x82`/`0x83`), `JSR` (`0x84`), `UserProcedure`, and remote `Peek`/`Poke`. These require host-generated data inside the original handshake's ack frame. With no working `REPLY` path, Piconet cannot deliver such a reply. The emulated `Mc6854` will still synthesise an `ImmReply` via `FourWayHandshake`, but `PiconetBackend` has nowhere to send it — the wire handshake completed (or failed) on the Pico before the host ever saw the event.

**Design decision**: `PiconetBackend` drops inbound `ImmReply` frames silently and logs them at debug level. It must *not* attempt to issue any Piconet command in response — there is no command that would help, and issuing a spurious `TX` would produce an unsolicited handshake to the original requester.

These immediate operations are used almost exclusively by network management tools (`NETMON`, remote debuggers, `*STATIONS` variants that probe beyond machine type). Standard filing-system and printer traffic never exercises this path. The limitation should be documented in user-facing material but does not block the general feasibility case.

### Outbound Immediate Operations

Outbound immediate ops from the emulated BBC (e.g. an emulated `NETMON` issuing `MachinePeek` at a real peer) depend on whether Piconet's `TX` surface returns the peer's ack payload. The Piconet protocol as documented returns only `TX_RESULT <code>` — a status, not a data payload. If the peer's reply bytes are not recoverable via the existing serial protocol, outbound immediate ops that expect reply data will return success-without-data to the emulated BBC. This needs to be verified against the Piconet firmware before `PiconetBackend` commits to a mapping; if the reply payload is genuinely unavailable, outbound immediates are a second bounded gap alongside the inbound one.

### Why This Does Not Undermine the Proposal

The abandoned reply feature actually *reinforces* the central architectural argument: the host must stay out of the wire-timing loop. Piconet's Core 1 firmware handling four-way handshakes at wire speed is the right division of labour precisely because USB-CDC is too slow and too jittery to participate in microsecond-scale line management. Beebium's `PiconetBackend` inherits this discipline for free — by never attempting host-in-handshake replies, it avoids the exact failure mode that forced jprayner to shelve the feature.

## Implementation Considerations

### Serial Port

POSIX `termios` for serial I/O, consistent with `AunBackend`'s use of POSIX sockets. Piconet uses 115200 baud, 8N1. The serial port appears as `/dev/tty.usbmodemXXXX` on macOS, `/dev/ttyACMX` on Linux.

### Protocol Parsing

Line-oriented text protocol with base64 binary encoding. The parser would:
- Read newline-delimited lines from the serial port
- Split on spaces to extract command/event type and fields
- Base64-decode binary payloads
- Map to/from `NetworkFrame` types

This is straightforward — perhaps 200-300 lines for the protocol layer.

### Threading

Two viable approaches:

1. **Non-blocking I/O on the emulation thread** — mirror `AunBackend`'s `select()` approach. Call `select()` with zero timeout on each `receive_frame()` to check for incoming serial data. Simple, but serial reads can return partial lines, requiring a line buffer.

2. **Dedicated I/O thread** — a reader thread blocks on serial input, parses complete lines, and pushes `NetworkFrame`s into a lock-free queue (following the `OutputQueue` pattern used for video/audio). `receive_frame()` becomes a simple queue drain. Cleaner separation, avoids partial-line complexity on the emulation thread.

The dedicated thread approach is likely better. Serial I/O is inherently bursty (lines arrive atomically from the Pico's USB CDC implementation), and a blocking reader is simpler than managing partial reads with non-blocking I/O.

### Station Number Ownership and Synchronisation

With Piconet in the path, two independent components each hold a notion of "our station number" and both are load-bearing. They must be kept in sync or unicast traffic breaks in both directions.

**Beebium side** — `EconetSocket::station_id_` (`src/core/include/beebium/econet/EconetSocket.hpp`) is the emulated equivalent of the BBC's station-ID latch (the links/DIP switches on a real Econet module). It is set via `enable(station_id, …)` / `set_station_id(…)`, read by the NFS ROM through `read_station_id()` at the latch address, and stamped by the NFS ROM into the source-station field of every scout and data frame it assembles. `write_station_id()` is a no-op, so the 6502 side cannot mutate its own station; the value is authoritative-but-fixed from host configuration.

**Piconet side** — `_listen_addresses[0]` in `board/src/econet.c` (default `0x02`, set via the `SET_STATION` command). It drives two behaviours:

1. **RX filter**: `_read_frame()` only delivers frames whose destination matches `_listen_addresses`. Traffic for any other station is dropped on the Pico before the host ever sees it. (`_listen_addresses[1] = 0xFF` is the hardcoded broadcast match.)
2. **TX source stamping**: `_tx_scout_buffer[2] = _listen_addresses[0];` and likewise for the data buffer. Piconet **overwrites** the source-station byte at frame-assembly time with its own station — whatever the NFS ROM wrote into the scout is discarded.

**Failure modes of divergence**:

- *Outbound*: the wire sees Piconet's station as the source; peers reply to Piconet's station; replies do reach Piconet but the NFS ROM sees a destination byte that doesn't match its latch and the handshake bookkeeping goes sideways.
- *Inbound*: peers addressing the emulated BBC's station send frames that Piconet's RX filter drops. The emulator is deaf to directed traffic; only broadcasts get through.

Neither is subtle — mis-sync produces total loss of bidirectional unicast.

**Authority**: Beebium's `EconetSocket::station_id_` is authoritative. It is the user-visible configuration (CLI args, gRPC), it is what the NFS ROM has been told at boot, and it is the value higher-level tooling already exposes. Piconet is slaved to it.

**Proposed mechanism**:

1. *At connect*: `PiconetBackend` issues `SET_STATION <station_id>` before transitioning Piconet to `LISTEN` mode, using the value passed to `EconetSocket::enable()`.
2. *On change*: add `virtual void on_station_id_changed(uint8_t)` to `NetworkBackend` (default no-op). `EconetSocket::set_station_id()` invokes it. `PiconetBackend` re-issues `SET_STATION`; `AunBackend` and `TestBackend` ignore it.
3. *On failure to apply* (Piconet rejects `SET_STATION`, or the serial write fails): treat as a fatal backend error — `is_connected()` goes false. Do not continue with a stale station on the wire; silently divergent state is the worst possible outcome.

**Incidental constraint**: `_listen_addresses` holds only one non-broadcast station, reconfirming that multi-station emulation on a single Piconet is not a trivial extension — see [Multi-Station Deployment](#multi-station-deployment) below.

### Multi-Station Deployment

Running multiple emulated BBC Micros on one host against a real Econet should require one physical Piconet device per emulated station, each on its own USB port. Each Beebium server process opens its own `/dev/tty.usbmodem*`, issues its own `SET_STATION`, and owns its device exclusively. This is the recommended deployment model, and it is architecturally honest: each emulated BBC's path mirrors a real BBC — one machine, one Econet module, one station ID latched in hardware. `PiconetBackend` stays a leaf with no cross-instance coordination, no shared wire-timing state, and no firmware fork. Station-number collisions become a user/config concern, not a software concern.

The alternatives are worse in instructive ways, and each reproduces a failure mode discussed elsewhere in this document:

- **`MONITOR`-mode software multiplexer** — one Piconet, many emulated stations, with the four-way handshake re-implemented in host userspace over promiscuous capture. This puts the host back in the wire-timing loop for *all* traffic, reviving the exact jam-the-line failure mode that forced `REPLY` to be abandoned upstream (see [Immediate Operations Limitation](#immediate-operations-limitation)). It also loses the benefit of Piconet's Core 1 firmware handling timing.
- **Firmware fork to accept multiple listen addresses** — `_listen_addresses` is already an array, so matching multiple stations on RX is plausible. But TX source-stamping uses `_listen_addresses[0]` unconditionally; correct multi-station behaviour needs per-frame station selection and a richer command protocol. Upstream divergence for a feature with one user, and Piconet loses its "single station on the wire" invariant.
- **Shared USB with logical channels** — raw bulk USB with a negotiated framing layer replacing CDC. A firmware and driver project in its own right, and the same class of effort jprayner cited as the only credible path to host-driven in-handshake replies. Not justified by multi-station alone.

The one-device-per-station rule is therefore the recommended position, not merely the path of least resistance. It keeps each emulated station indistinguishable from a real one at the wire level and at the firmware level, and it requires no code beyond what `PiconetBackend` needs for the single-station case.

### Configuration Surface

Backend selection is configuration, not a Peripheral Extension. Econet itself (`Mc6854`, `EconetSocket`, NFS ROM dispatch, `FourWayHandshake`) is core machinery wired into interrupts and the memory map across all three machine variants; the pluggability the user actually wants is at the transport layer, which `NetworkBackend` already provides cleanly. Adding Piconet means adding a second backend behind that abstraction and giving the user a way to pick it.

The CLI/preset shape described below is for the initial in-tree implementation. Once a second non-AUN transport exists, the configuration surface migrates to the generalised form described in [Future Direction: Econet Transport Extensions](#future-direction-econet-transport-extensions). Discipline during the in-tree build keeps that migration cheap: no Piconet-specific knowledge in `ServerMain.hpp` beyond CLI parsing, no Piconet-specific knowledge in `PresetLoader.hpp` beyond JSON parsing, and all Piconet-specific code (serial protocol, base64, command/event mapping) confined to one directory that can later be extracted wholesale.

**Selection model**: the active backend is *inferred from which flag is supplied*, with `--aun-port` and `--piconet` mutually exclusive. Supplying both is a hard error at startup, not a silent winner — divergent or ambiguous transport configuration must fail loudly before any frame moves.

#### Command Line

```
--station 32 --aun-port 32768 --aun-map 0.254:127.0.0.1:32768
--station 32 --piconet /dev/tty.usbmodem1234
--station 32 --aun-port none                # Econet hardware fitted, unplugged
                                             # neither → Econet hardware not fitted
```

The existing `--aun-port` and `--aun-map` flags are unchanged. `--piconet <device-path>` is the new flag; its argument is the POSIX device path for the Piconet's USB-CDC serial port (`/dev/tty.usbmodem*` on macOS, `/dev/ttyACM*` on Linux). `--aun-port none` retains its current meaning of "Econet board fitted but no transport attached", which corresponds to a real BBC with the Econet module installed but no clock signal — the NFS ROM sees "No Clock". Omitting both flags means the Econet board is not fitted at all (no station ID latch, no ADLC at the memory map).

#### Preset JSON

The preset file mirrors the CLI mutual exclusion. The current `econet.station` field is unchanged. The current `econet.aun_port` field is unchanged. A new sibling `econet.piconet` object holds Piconet configuration:

```json
"econet": {
  "station": 32,
  "piconet": { "device_path": "/dev/tty.usbmodem1234" }
}
```

or, for AUN:

```json
"econet": {
  "station": 32,
  "aun_port": 32768,
  "aun_map": [
    { "net": 0, "stn": 254, "ip": "127.0.0.1", "port": 32768 }
  ]
}
```

`econet.aun_port` and `econet.piconet` are mutually exclusive. Their joint presence in a preset file is a load-time error from `PresetLoader`, surfaced before the server constructs the Econet stack. The same precedence rules that govern other preset/CLI overlays apply: a CLI `--piconet` overrides a preset's `aun_port` (and vice versa) by replacing the entire transport selection — there is no merging, because the selections are categorically incompatible.

#### Backend Capability Discovery

The gRPC layer should expose which backends this server build supports, so a client can present a sensible UI rather than offering Piconet on a build that wasn't compiled with it. This is a query on the existing Econet-related service rather than a Peripheral Extension manifest entry — the backend is part of Econet, not a separate peripheral. Concretely: an `EconetBackends` RPC returning the set of compiled-in backend identifiers (`aun`, `piconet`, plus whatever `TestBackend` exposes for diagnostics). Device-path enumeration (which `/dev/tty.usbmodem*` actually look Piconet-shaped) is a separate matter and probably best left to client-side OS-specific code, not pushed across gRPC.

### Device Discovery

Two options:
- **Explicit path**: `--piconet /dev/tty.usbmodem1234` on the command line. Simple, reliable.
- **USB enumeration**: scan for devices matching Piconet's USB VID/PID. More convenient but platform-specific (IOKit on macOS, udev on Linux).

Starting with an explicit path is the pragmatic choice.

### No Existing C/C++ Driver

The Piconet project provides a TypeScript/Node.js driver (`econet-hq`) but no C or C++ library. A native implementation would be needed. The protocol is simple enough that this is not a significant obstacle — the entire serial protocol layer (connect, configure, send, receive, parse) is perhaps 500 lines of C++.

### Hot-Plug

If the serial port disconnects (USB cable removed), `is_connected()` returns false. This propagates through the stack:

- `Mc6854` sees DCD go high (no carrier)
- SR2 DCD bit is set
- NFS ROM detects "No Clock" on its next status poll
- Network operations fail gracefully

Reconnection could be handled by periodic reconnect attempts in the I/O thread, re-issuing `SET_STATION` and `SET_MODE LISTEN` on success.

## Future Direction: Econet Transport Extensions

The initial Piconet implementation lives in core alongside `AunBackend`, but this is not the long-term shape. Building specific transport knowledge (USB-CDC serial protocols, Pico firmware quirks, future Pi Econet HAT GPIO/SPI dialects) into the core is the wrong place for it. The right place is a dedicated Econet Transport extension point, modelled on the existing Peripheral Extension framework but with its own identity.

### Extension Point Shape

`EconetTransportExtension` is the new extension-point identity. The existing `NetworkBackend` abstract class becomes its implementation interface — extensions provide a factory that returns a `std::unique_ptr<NetworkBackend>` plus the lifecycle hooks the framework expects. The manifest format reuses what peripheral extensions already declare: name, version, parameter schema. For Piconet that means one required parameter (`device_path`); for AUN that means `port` and an optional `map` table.

`AunEconetTransport` ships as a built-in extension, compiled into core and registered at startup. It has no external dependencies, it is the obvious default, and it should not require a separate extension file on disk to be available. `PiconetEconetTransport` ships as a discoverable extension, loaded the same way SCSI is today. Future transports — Pi Econet HAT, PiEconetBridge over a named pipe, a record/replay transport for testing — drop in as further extensions with no core changes.

### Configuration Surface After Migration

The bespoke `--aun-port` / `--aun-map` / `--piconet` flags collapse into one composable form, shared by every transport:

```
--econet-transport piconet:device_path=/dev/tty.usbmodem1234
--econet-transport aun:port=32768,map=0.254:127.0.0.1:32768
--econet-transport aun:port=none           # hardware fitted, unplugged
                                            # absent → hardware not fitted
```

The preset JSON shifts likewise: `econet.aun_port` and `econet.piconet` are replaced by a single `econet.transport` object naming the transport and carrying its parameters. gRPC discovery becomes "list installed transport extensions and their parameter schemas", driven by manifest data rather than a compile-time enum. A client UI can render a transport picker entirely from those schemas.

### Staged Migration

Build Piconet in-tree first, behind the existing `NetworkBackend` abstraction, with the configuration surface described above ([Configuration Surface](#configuration-surface)). Use the experience of having two real transports — AUN and Piconet — to design `EconetTransportExtension` honestly, against actual implementation constraints rather than speculation. Then refactor: extract both `AunBackend` and `PiconetBackend` behind the new extension API in a single change. Two implementations is the minimum to know whether the abstraction is genuine; designing it against one is how API mistakes get baked in. After the refactor, the next transport (PiEconetBridge or HAT) is the first true external extension and confirms the boundary holds.

### Discipline During the In-Tree Build

To keep the eventual refactor cheap, the in-tree implementation of `PiconetBackend` should observe a few constraints that anticipate extraction:

- **No core code references Piconet by name beyond CLI/preset parsing.** `EconetSocket`, `Mc6854`, `FourWayHandshake` know only `NetworkBackend` — never `PiconetBackend`. This rule is already implied by the architecture; the discipline is to avoid drift.
- **All Piconet-specific code in one directory** (e.g. `src/core/src/econet/piconet/`), with one header (`PiconetBackend.hpp`) as its sole public surface. The serial protocol parser, base64 codec, command/event mapping, and reader thread all live here. Nothing leaks.
- **Parameter handling is structured, not ad-hoc.** Even though the initial CLI flag is `--piconet <device-path>`, the internal representation in `PiconetBackend`'s constructor takes a struct (`PiconetConfig { std::string device_path; }`) — not a string parsed at use site. When the parameter set grows (baud rate override, reconnect policy, monitor mode toggle), additions are local. When the extension API arrives, the manifest schema generates the same struct.
- **No Piconet-specific gRPC.** Diagnostics that Piconet wants to expose (link state, last `TX_RESULT` codes, monitor-mode capture) go through generic Econet diagnostic services with backend-tagged fields, not a `PiconetService`. A Piconet-specific gRPC service would need migration when the extension lands.

Following these keeps the refactor a mechanical extraction rather than an architectural disentanglement.

## Testing Strategy

Piconet integration is unusual for Beebium in that the production target is physical hardware: a USB device, on a real Econet segment, talking to other real (or emulated) stations. Cloud CI runners have neither the device nor the network. Even self-hosted runners with a real Piconet wouldn't necessarily have peer stations to converse with. Pure end-to-end testing against real hardware is therefore neither sufficient (too narrow a window into behaviour) nor scalable (too much physical apparatus). A layered test-double strategy is essential.

The testing strategy is also helped considerably by Piconet being fully open source. The firmware (`board/src/econet.c`, `board/src/piconet.c`), the driver (`driver/`), the hardware schematics, and the protocol documentation are all available. This means the test doubles can be built against the *actual* firmware source rather than reverse-engineered from observed behaviour. When a question arises about what real Piconet does in some edge case, the answer is in the source — we read it, encode the same behaviour into the fake, and add a hardware contract test to verify both implementations agree. There is no fidelity guesswork.

### Why Standard Backends Are Not Sufficient

`TestBackend` already exists and is the right tool for testing `Mc6854`, `FourWayHandshake`, `EconetSocket`, and the NFS ROM. But it sits at the `NetworkBackend` interface, *above* everything `PiconetBackend` adds: the serial protocol parser/writer, base64 codec, command formatting, event parsing, mode/station state machine, and the reader thread that turns serial input into queued frames. None of that code is exercised by `TestBackend`-based tests, and most of it is the kind of tedious string-handling and state-tracking that's easy to get wrong. A test double that speaks Piconet serial protocol on its front end is needed to test the new code at all.

### The Fidelity Trap

A fake Piconet that *almost* matches the real firmware is worse than no fake. Tests pass against the fake, real hardware diverges, and the divergence accumulates silently until someone runs against real hardware and discovers a backlog of subtle bugs. Two disciplines keep this from happening:

1. **A shared `piconet-protocol` library** used by both `PiconetBackend` and the fake. Single source of truth for the command/event grammar, base64 framing, `TX_RESULT` enumeration, and constants extracted directly from the firmware (timeout values, buffer sizes, default station number). When the protocol changes upstream, both sides update together or neither does. Cross-checked against the firmware source at vendor time.

2. **A small, focused hardware contract suite** marked `[piconet-hardware]`, run manually on the developer's machine and (eventually) on a self-hosted runner with real Piconet hardware. The fake is correct precisely insofar as both fake and real hardware pass the same contract tests. This is the fidelity anchor; without it the fake becomes parallel reality. The open-source firmware lets us derive the contract test expectations from `econet.c` directly rather than guessing.

### Three Test Doubles, Layered

**`MockPiconetSerial`** — a serial-port test double for unit tests of `PiconetBackend`'s protocol layer. Given a script of expected `write()` calls and a script of bytes to deliver on `read()`, it drives `PiconetBackend` through specific paths and asserts on the protocol traffic. Pure, fast, deterministic, in-process. Tests command formatting, event parsing, base64 round-trip, malformed-input handling, partial-line buffering, error-event propagation. The bulk of TDD work for the protocol layer happens here.

**`FakePiconetDevice`** — a behavioural model of the Piconet firmware. Holds mode (`STOP`/`LISTEN`/`MONITOR`), station number, and a queue of pending events. Accepts commands and emits plausible events on a configurable timeline. No real network; tests inject "wire events" directly (`receive_inbound_unicast(src, payload)` etc.) and the fake produces the corresponding `RX_TRANSMIT` to `PiconetBackend`. Models the behaviours that matter for the Beebium-side code:

- `SET_STATION` updates the internal station; subsequent `RX_TRANSMIT` filters apply.
- `TX` produces `TX_RESULT OK` after a configurable delay (or a configured failure code).
- Inbound MachinePeek (control byte `0x88`) is auto-replied with the canned machine-type response, never delivered to the host — matching the real firmware behaviour described in `econet.c`.
- Mode transitions (`STOP` → `LISTEN`) discard inbound traffic appropriately.
- Disconnect simulation (close the underlying serial channel) exercises `PiconetBackend`'s reconnect path.

This is the level used for integration tests of `Mc6854` → `FourWayHandshake` → `PiconetBackend` against a "live" Piconet without claiming wire fidelity for handshake timing. Behaviours that the fake doesn't model (real ADLC quirks, line-jamming, clock detection) are out of scope; the contract tests cover those against real hardware.

**`AunBridgePiconetDevice`** — extends `FakePiconetDevice` with an AUN back-end. Two Beebium instances each attach to their own bridge instance; the two bridges form an AUN-connected pair, so frames sent by emulator A's `PiconetBackend` reach emulator B's `PiconetBackend` via UDP. End-to-end Econet-over-Piconet between two emulators with no Piconet hardware. The AUN code is reused from `AunBackend` rather than re-implemented — the bridge is a thin adapter from Piconet's frame model to AUN's, not a new transport. Useful for multi-emulator integration scenarios where we want to exercise `PiconetBackend` on both sides of a real network conversation.

### Connection Mechanism

For `MockPiconetSerial` (level 1), the connection is in-process. `PiconetBackend` takes a `SerialPort` interface (a small abstraction over file-descriptor-style read/write/select) and the mock implements it. No real I/O, fastest possible feedback, full control over byte timing.

For `FakePiconetDevice` and `AunBridgePiconetDevice` (levels 2 and 3), **PTY pairs** (POSIX pseudo-terminals) are worth the extra complexity. The fake holds the master end; `PiconetBackend` opens the slave end as `/dev/pts/N` thinking it's `/dev/tty.usbmodem*`. This exercises the full I/O path — non-blocking reads, partial-line buffering, `select()`/`poll()` integration, blocking-read behaviour in the reader thread — that an in-process fake skips. Windows CI is awkward here (no native PTY); the practical answer is to restrict levels 2 and 3 to POSIX runners and rely on level-1 unit tests for Windows protocol-layer coverage. Hardware contract tests on Windows wait for self-hosted runners.

### Hardware Contract Tests

A small `[piconet-hardware]` test suite anchors the fidelity claim. These tests are skipped by default; they run when a real Piconet device path is supplied (`BEEBIUM_PICONET_DEVICE=/dev/tty.usbmodem...` or similar). Each contract test runs against both the real device and `FakePiconetDevice`, asserting the same outcome. Failure on real-only or fake-only signals divergence.

Initial scope, derived from reading the firmware:

- `SET_STATION` round-trip: set a station, query via `STATUS`, confirm value.
- `TX` to a non-existent station: returns `TX_RESULT NO_SCOUT_ACK` after the firmware's documented timeout.
- `TX` with `LINE_JAMMED` precondition: results in the documented error code.
- `BCAST` semantics: fire-and-forget, no `TX_RESULT`.
- Inbound MachinePeek delivery: the host receives nothing (firmware handles it inline).
- Mode transitions: `STOP` mode silences both TX commands and inbound events; `MONITOR` mode delivers all wire traffic regardless of station match.
- Disconnect/reconnect: USB cable removal produces the appropriate `is_connected()` transitions.

The suite stays small — these are *contract* tests, not exhaustive integration. New contract tests are added when a fake-vs-real divergence is discovered, with the divergence becoming a regression test for the fake.

### Shared `piconet-protocol` Library

A small library, used by both `PiconetBackend` and `FakePiconetDevice`, holds:

- Command and event grammar (encoder/decoder for each line type).
- `TX_RESULT` code enumeration with names matching the firmware's `tx_result_t`.
- Base64 codec (or a wrapper around an existing one).
- Constants extracted from the firmware: default station, listen-address array size, timeout values that affect host-observable behaviour, line-length limits.
- A protocol-version identifier that the fake reports via `STATUS`, matching whatever the upstream firmware reports, so accidental version drift is caught immediately.

Roughly 200-300 LOC. The win is that protocol changes can't desynchronise: PiconetBackend and the fake update in lockstep because they share the same code. When the upstream firmware changes (a new command, a renamed result code), the library updates in one place and both consumers follow.

### Build Order

Practical sequence for TDD:

1. **`piconet-protocol` library first**, with unit tests against fixture lines extracted from the firmware. Get the encoder/decoder honest before any consumer depends on it.
2. **`MockPiconetSerial` and `PiconetBackend` together**, TDD-style. Write a failing test against the mock, implement just enough `PiconetBackend` to pass, refactor. The protocol layer comes up under unit-test coverage from the start.
3. **`FakePiconetDevice`** once `PiconetBackend`'s protocol layer is solid. Use it for integration tests against `Mc6854`/`FourWayHandshake` and to validate `PiconetBackend`'s threading and queue behaviour under load.
4. **First hardware contract test** as soon as a developer with the device can run it — even before `PiconetBackend` is feature-complete. The earlier the contract is exercised, the smaller the gap to close if divergence appears.
5. **`AunBridgePiconetDevice`** once single-instance flow works end-to-end against `FakePiconetDevice`. Adds Beebium-to-Beebium scenarios without hardware.
6. **Self-hosted CI runner with real Piconet** is a separate piece of infrastructure work, not blocking on any of the above. Once it exists, the `[piconet-hardware]` suite runs there automatically.

This sequence keeps fast feedback throughout development while the fidelity anchor (hardware contract tests) is established as early as possible. The elaborate AUN-bridging fake is the last thing built, when its value (multi-emulator integration coverage) is unambiguous and the underlying components are stable.

## Broader Implications

### Mixed Real/Emulated Networks

With Piconet, an emulated BBC becomes indistinguishable from a real one at the protocol level. A real Acorn fileserver would see station 32 logging in, loading files, and printing — with no way to tell it's running in software. This opens up:

- **Testing**: verify Beebium's Econet behaviour against real hardware, not just AUN approximations
- **Preservation**: run preserved software on emulated hardware against preserved servers on real hardware (or vice versa)
- **Mixed networks**: real and emulated BBCs coexisting on the same Econet

### Multiple Emulated Stations

Each emulated BBC instance would need its own Piconet device, since each Piconet has a single station identity on the wire. Alternatively, Piconet's `MONITOR` mode could be used to implement a software multiplexer that handles multiple station identities over a single physical connection — but this would be a substantial undertaking and would need to re-implement the wire-level four-way handshake in software (losing the benefit of Piconet's Core 1 firmware handling it).

### Traffic Analysis

Piconet's `MONITOR` mode captures all Econet traffic, not just frames addressed to this station. Combined with Beebium's planned `SubscribeEconetEvents` gRPC stream, this could provide a real-time Econet traffic analyser — potentially more useful than the integration with normal station operation.

### Complementary to Pi Econet Bridge

The [Pi Econet Bridge](https://github.com/cr12925/PiEconetBridge) provides AUN/UDP access to a real Econet network from a Raspberry Pi. This already works with `AunBackend` (untested but architecturally compatible). Piconet provides direct wire access without the AUN translation layer. The two approaches are complementary:

- **Pi Econet Bridge + AunBackend**: emulated BBC ↔ AUN/UDP ↔ Pi ↔ real Econet. Involves protocol translation at the Pi.
- **Piconet + PiconetBackend**: emulated BBC ↔ USB serial ↔ Piconet ↔ real Econet. Native Econet frames end-to-end.

## Verdict

**Architecturally feasible.** `NetworkBackend` is exactly the right seam — the abstraction was designed for precisely this kind of transport substitution, and the interface maps cleanly to Piconet's frame-level protocol.

**FourWayHandshake already solves the key impedance mismatch.** The same synthetic handshake bridge that makes AUN work also makes Piconet work. No changes needed to the handshake logic.

**Purely additive.** A `PiconetBackend` is a new class alongside `AunBackend`. No modifications to `Mc6854`, `FourWayHandshake`, `EconetSocket`, or any machine-level code.

**Implementation effort is modest.** The main work is a C++ serial protocol driver — serial port management, line-oriented I/O, base64 encoding, and the command/event mapping. Roughly 500 lines of focused code, plus a similar amount for tests.

**Main risk is timing edge cases.** `FourWayHandshake`'s synthetic scout-ack fires before the real wire handshake completes. If the real handshake fails (destination not listening, line jammed), the BBC has already committed to sending data based on the fake ack. The failure manifests as a timeout rather than a clean rejection. This is the same trade-off that exists with AUN, and NFS ROMs handle it gracefully — but it's worth noting that the Piconet path adds one more layer where real-world wire failures can diverge from the synthetic handshake's optimistic assumptions.

**One bounded functional gap.** Inbound immediate operations other than MachinePeek cannot be serviced, because Piconet's firmware does not support host-driven in-handshake replies (the feature was built and deliberately disabled — see [Immediate Operations Limitation](#immediate-operations-limitation)). Standard filing-system, printer, and broadcast traffic is unaffected; the gap is specific to network-management tools that issue `Halt`/`JSR`/`Peek`/`Poke` immediates at the emulated station. Document the limitation; do not attempt to work around it, because the architectural reasons it was abandoned upstream apply equally to Beebium.
