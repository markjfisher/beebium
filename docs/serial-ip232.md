# IP232 serial (`ip232-serial`)

`ip232-serial` connects the BBC Micro's serial port (RS423) to a **tcpser-style
IP232 server** over TCP, so the emulated Beeb can talk to a virtual Hayes modem
and dial out to telnet BBSes and other TCP services — the classic retro
telecomms experience, with BeebEm parity.

It is one of Beebium's serial `SerialPortDevice` extensions, the network sibling
of `host-serial`: the same endpoint machinery (async reader/writer threads,
bounded queues, `/CTS` back-pressure) with a TCP socket and a small protocol
codec in place of a tty. For the shared serial architecture see
[serial-acia.md](serial-acia.md).

`ip232-serial` ships as a **dynamically-loaded plugin** (only `host-serial` is a
built-in), discovered at runtime from `<exe-dir>/extensions/ip232-serial/` — no
special setup; a packaged Beebium server includes it.

## Why it exists

The BBC's RS423 port was widely used in the 1980s to drive a modem and dial
bulletin boards. `tcpser` is a modern program that emulates a Hayes modem and
bridges the "phone line" to TCP: the emulator opens a TCP connection to `tcpser`,
the guest sends `AT` commands and dials, and `tcpser` connects onward to a telnet
host. `ip232-serial` is the Beebium end of that link.

It is also the first proof that Beebium's serial device seam is genuinely open: a
network-backed device is "just another extension" attaching to the same
`SerialPort` handle as `host-serial` / `rpc-serial` / `loopback-serial`, with no
changes to the core ACIA/ULA emulation.

```
BBC RS423  <->  ip232-serial (TCP client)  <--TCP-->  tcpser (server, :25232)  <->  telnet BBS / TCP
```

## How it works

**The emulator is the TCP client.** `ip232-serial` connects *out* to an IP232
server at `host:port` (tcpser's default port is `25232`). There are two modes.

### `ip232` mode (default)

A persistent TCP connection with a minimal in-band signalling protocol that uses
`0xFF` as an escape/flag byte (defined by tcpser and BeebEm; there is no formal
spec):

- **Outbound (Beeb → server).** Data bytes are sent verbatim, except a data byte
  of `0xFF` is doubled to `0xFF 0xFF` so it is not mistaken for a flag. When
  `handshake` is enabled, a change in the BBC's RTS line is sent as
  `0xFF 0x01` (RTS asserted) or `0xFF 0x00` (RTS deasserted).
- **Inbound (server → Beeb).** `0xFF 0xFF` is a literal `0xFF` data byte;
  `0xFF 0x01` / `0xFF 0x00` convey the modem's DTR. The BBC RS423 connector has
  **no DTR or DCD pin** (see below), so inbound DTR is decoded but informational.

### `raw` mode

A pure byte pipe with no escaping. The TCP connection follows the BBC's RTS line:
it **connects when RTS is asserted and disconnects when RTS is dropped**. Use raw
mode for a plain socket peer that does not understand the `0xFF` convention.

### Control lines: what the BBC actually has

The Acorn RS423 connector exposes only five lines: signal ground, transmitted
data, received data, **RTS** (an output from the BBC) and **CTS** (an input to
the BBC). There is **no DCD, DTR or DSR** pin. So `ip232-serial` models exactly
what the hardware has:

- **CTS (in).** While the TCP connection is down, the device reports "not clear
  to send", which makes the Serial ULA assert the ACIA's `/CTS`. The guest's
  transmit loop then busy-waits rather than transmitting into a dead socket — a
  clean stall of the *guest*, never the emulator host. When the connection comes
  up, transmission resumes.
- **RTS (out).** Driven by the MC6850 control register. In `ip232` mode with
  `handshake` it is conveyed to the server via the `0xFF` escape; in `raw` mode
  it drives the connect/disconnect lifecycle.

A serial **BREAK is not carried** by IP232 (the protocol has no break
signalling): a BBC-transmitted break is not propagated, and the BBC's receiver
is never handed one. If you need break across the link — for frame-based
protocols such as DMX512 or LIN — use the RFC 2217 endpoints, which carry it in
both directions (see [serial-rfc2217.md](serial-rfc2217.md)).

### Never stalls the emulator

All socket I/O runs on dedicated threads (a connection/reader thread and a writer
thread); the emulation thread only touches bounded, mutex-protected queues. An
unresponsive or slow peer can stall the *guest* (via real `/CTS` back-pressure)
but never the emulator host. The transmit queue is bounded by `tx_buffer` (the
`/CTS` mark) with a small hard cap above it; a dropped connection mid-transmit
loses the in-flight bytes, exactly as real hardware would. In `ip232` mode a
dropped connection is retried automatically.

## How to use it

`ip232-serial` is configured with the generic extension argument form,
`--ip232-serial key=value:key=value`. The endpoint is given either as a single
`url=` or as separate `host=`/`port=` (not both).

| key | type | default | meaning |
|-----|------|---------|---------|
| `url` | string | — | endpoint as `[scheme://]host:port`; alternative to `host`/`port` |
| `host` | string | `localhost` | IP232 server hostname / address |
| `port` | integer | `25232` | IP232 server TCP port |
| `mode` | string | `ip232` | `ip232` (escaped, persistent) or `raw` (pipe, connect on RTS) |
| `handshake` | boolean | `true` | convey RTS via the `0xFF` escape (`ip232` mode only) |
| `tx_buffer` | integer | `4096` | transmit buffer bytes; `/CTS` asserts at/above it |

`beebium-model-b describe-extension ip232-serial` prints this schema live.

Because the argument form splits on `:`, a `url` value (which contains a port
colon) must be **wrapped in double quotes**; the shell usually needs an outer
single-quote too:

```
--ip232-serial 'url="ip232://bbs.example.com:25232"'
--ip232-serial 'url="127.0.0.1:25232":mode=raw'     # scheme optional
```

An unquoted `url=ip232://host:port` is caught with a message telling you to
quote it. `url=` and `host=`/`port=` cannot be combined.

### Worked example: dial a BBS through tcpser

1. Start a tcpser virtual modem (in another terminal):

   ```
   tcpser -v 25232 -s 19200 -l 7
   ```

2. Launch a Beebium server with the IP232 bridge:

   ```
   beebium-model-b start --ip232-serial host=localhost:port=25232
   ```

3. In the emulated BBC, route the serial port and talk to the "modem". From BBC
   BASIC, send keyboard input to the RS423 output and show RS423 input on screen:

   ```basic
   *FX2,2        : REM take input from RS423
   *FX3,1        : REM send output to RS423
   ```

   Then type Hayes commands, e.g. `ATDT bbs.example.com:23` to have tcpser dial a
   telnet BBS. (`*FX2,0` / `*FX3,0` restore the keyboard and screen.)

### Raw mode

For a plain socket peer that just wants bytes, with the connection gated on RTS:

```
beebium-model-b start --ip232-serial host=127.0.0.1:port=10001:mode=raw
```

### In a preset

Like any extension, it can be pinned in a preset; `create-preset` captures the
`--ip232-serial …` flags into the preset's `extensions` array.

## Troubleshooting

- **Nothing is transmitted / the program hangs sending.** The connection is
  probably down, so `/CTS` is held and the guest's transmit loop is waiting. Check
  the server is listening on `host:port`; `describe-extension` confirms the
  defaults. A refused connection is reported on the server's stderr.
- **A `0xFF`-heavy binary transfer looks corrupted in `raw` mode.** Raw mode does
  not escape `0xFF`; use `ip232` mode (the default) against a tcpser-style server.
- **The link drops and does not come back in `raw` mode.** Raw mode follows RTS;
  it reconnects when the guest re-asserts RTS. `ip232` mode reconnects
  automatically.

## Relationship to RFC 2217

IP232 is a thin retro-comms hack (tunnel bytes + a couple of modem lines). RFC
2217 is the IETF "Telnet Com Port Control Option" — a standards-based way to
drive a *real* remote serial port (set its baud/parity, read its line states).
Beebium plans `rfc2217-client-serial` and `rfc2217-server-serial` as further
serial extensions reusing the same `beebium::net` TCP transport; see
[serial-network-ip232-rfc2217.md](discussion/serial-network-ip232-rfc2217.md).

## Implementation

- `src/extensions/ip232-serial/Ip232Codec.hpp` — the `0xFF` escape codec (pure,
  golden-vector tested against the BeebEm wire format).
- `src/extensions/ip232-serial/Ip232SerialEndpoint.{hpp,cpp}` — the
  `SerialPortDevice`: TCP transport + codec + reader/writer threads + RTS
  reaction + connection-as-`/CTS`.
- `src/extensions/ip232-serial/Ip232SerialExtension.{hpp,cpp}` — the built-in
  PeripheralExtension and its CLI/manifest config.
- `src/core/include/beebium/net/` — the shared `SocketPlatform.hpp` and
  `TcpClientSerialPort` (reused by the RFC 2217 extensions).

Tests: `tests/test_ip232_codec.cpp` (golden vectors),
`tests/test_ip232_serial_endpoint.cpp` (round-trip / RTS / reconnect against an
in-process loopback server), `tests/test_ip232_serial_extension.cpp` (config /
attach), and an opt-in `tests/test_ip232_tcpser_integration.cpp` that runs
against a real `tcpser` when one is available.

Authoritative protocol references: BeebEm `Src/IP232.cpp` + `Src/Serial.cpp`, and
[tcpser](https://github.com/go4retro/tcpser).
