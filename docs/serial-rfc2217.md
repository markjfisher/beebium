# RFC 2217 serial (`rfc2217-client-serial`, `rfc2217-server-serial`)

RFC 2217 — the IETF **Telnet Com Port Control Option** — carries a serial port
over a Telnet/TCP session and lets a client remotely configure and drive the
serial port on an "access server". Beebium implements **both roles**, as two
optional plugin extensions:

- **`rfc2217-client-serial`** — Beebium is the Telnet client to a remote access
  server (ser2net, a networked FujiNet, pySerial's `rfc2217_server.py`). It is
  the network sibling of host-serial, and the one place baud configures a *real*
  remote UART.
- **`rfc2217-server-serial`** — Beebium is the access server, exposing the
  emulated BBC serial port so any off-the-shelf RFC 2217 tool can connect and be
  the device on the far end of the BBC's cable. The standards-based,
  Beebium-unaware twin of rpc-serial.

Both ship as **dynamically-loaded plugins** (only host-serial is built in),
discovered from `<exe-dir>/extensions/`. For the shared serial architecture see
[serial-acia.md](serial-acia.md); the research notes are in
[serial-network-ip232-rfc2217.md](discussion/serial-network-ip232-rfc2217.md).

## How it works

Both roles share one codec (`beebium_ext_rfc2217_common`): a Telnet IAC parser +
the COM-PORT-OPTION subnegotiation set. Application data is escaped `IAC IAC`
(0xFF doubled, the same idea as IP232's escape but inside Telnet framing).
Commands ride in `IAC SB COM-PORT-OPTION <cmd> <value…> IAC SE`; the client uses
command codes 1–12 and the server replies with the same code + 100.

See [Supported vs deliberately not implemented](#supported-vs-deliberately-not-implemented)
below for exactly which COM-PORT commands are honoured.

### Control lines

The BBC RS423 connector has only RTS (out) and CTS (in) — no DCD/DTR. So:
- **Client:** the BBC's RTS → `SET-CONTROL` RTS to the remote; the remote's
  `NOTIFY-MODEMSTATE` CTS gates the BBC's transmit (real remote flow control).
- **Server:** the client's `SET-CONTROL` RTS drives the BBC's CTS input
  (`accepts_more` → `/CTS`); the BBC's RTS is reported back as a
  `NOTIFY-MODEMSTATE` CTS bit.

### Baud and the rate mismatch

For the **client**, `baud=` sets the *remote* UART's real rate. The mismatch
between the BBC's 6850/ULA rate and the RFC 2217 rate is absorbed by the same
bounded tx/rx queues + `/CTS` back-pressure as host-serial: the remote paces the
flow over TCP (its UART can only drain at the configured baud, so its socket
buffer fills, TCP flow-control stops our writer, the tx queue fills, `/CTS`
asserts, and the guest's transmit loop stalls losslessly). For the **server**,
client-requested baud/parity are **cosmetic** (echo-accepted) — the BBC's real
rate is whatever guest software programs into the Serial ULA.

### Never stalls the emulator

All socket I/O runs on dedicated threads; the emulation thread only touches
bounded, mutex-protected queues. An unresponsive peer stalls the *guest* (via
`/CTS`), never the emulator host.

## How to use it

### Client

| key | type | default | meaning |
|-----|------|---------|---------|
| `url` | string | — | access-server endpoint as `[scheme://]host:port`; alternative to `host`/`port` |
| `host` / `port` | string / int | — | access-server address (alternative to `url`) |
| `baud` | int | `19200` | baud to set on the remote UART (real hardware) |
| `framing` | string | `8N1` | remote UART framing `<data><parity><stop>` (data 5–8; parity N/O/E/M/S; stop 1/2) |
| `flow` | string | `none` | remote UART flow control: `none` / `rtscts` / `xonxoff` |
| `dtr` | bool | `true` | assert DTR on the remote UART (the BBC has no DTR pin; real devices need it) |
| `tx_buffer` | int | `4096` | transmit buffer bytes; `/CTS` asserts at/above it |

`baud`/`framing`/`flow`/`dtr` configure the **remote** UART (a real device);
they are independent of the BBC's own line settings, which the guest OS owns.

```
# A value with a colon must be double-quoted (the shell usually needs single
# quotes too); host=/port= cannot be combined with url=.
beebium-model-b start --rfc2217-client-serial 'url="rfc2217://ser2net.local:4001":baud=9600:framing=7E1:flow=rtscts'
beebium-model-b start --rfc2217-client-serial host=ser2net.local:port=4001
```

### Server

| key | type | default | meaning |
|-----|------|---------|---------|
| `bind` | string | `127.0.0.1` | listen address |
| `port` | int | *(required)* | TCP port to listen on |
| `tx_buffer` | int | `4096` | transmit buffer bytes |

```
beebium-model-b start --rfc2217-server-serial port=4001
```

Then connect any RFC 2217 client, e.g. with pySerial:

```
python -m serial.tools.miniterm rfc2217://127.0.0.1:4001
# or, in code:
#   s = serial.serial_for_url("rfc2217://127.0.0.1:4001", baudrate=9600)
```

## Supported vs deliberately not implemented

Beebium implements the COM-PORT commands that fit its **byte-oriented** model.
The plugin bridges two byte-streams — the BBC's serial port and the TCP peer —
that the ULA has already framed into whole bytes, decoupled by bounded queues.
Anything that is **sub-byte or out-of-band** (a line condition rather than a
data value) cannot ride that byte queue, and carrying it would need extra
side-channels into the ULA/ACIA; those are deliberately left out for now.

**Supported**

| Command | Client (we drive the remote) | Server (we are the device) |
|---|---|---|
| Option negotiation | offers `WILL COM-PORT`/`BINARY`/`SGA` | answers `DO`/`WILL` |
| Data (`IAC IAC`) | yes | yes |
| `SET-BAUDRATE` | sets the **real** remote baud | **cosmetic** — echo-accepted, never applied to the BBC |
| `SET-DATASIZE`/`PARITY`/`STOPSIZE` | sets the remote framing (`framing=`) | **cosmetic** — echo-accepted |
| `SET-CONTROL` flow / DTR | sets the remote (`flow=`/`dtr=`) | echo-accepted |
| `SET-CONTROL` RTS | the BBC's RTS → remote | client RTS → the BBC's CTS input |
| `NOTIFY-MODEMSTATE` (CTS) | consumed as remote flow control | emitted from the BBC's RTS |
| `PURGE-DATA`, the `*-MASK` requests | — | echo-accepted (so strict clients open) |

The **server never changes the emulated BBC hardware.** A remote client's
baud/parity/stop requests are acknowledged but **not applied**: the BBC's real
line settings are whatever the guest OS programs into the MC6850/Serial ULA, and
re-timing the hardware from the network would desync the OS's RAM copy of those
settings. The queues absorb any rate mismatch (see [Baud and the rate
mismatch](#baud-and-the-rate-mismatch)).

**Deliberately NOT implemented (with fall-backs)**

- **`BREAK`.** A BREAK is the line held in the space state longer than a
  character frame — a *condition*, not a byte — so it can't traverse the byte
  queue. *Fall-back:* a BBC-transmitted break is silently not propagated to the
  remote (no data flows while the guest holds break; normal data resumes when it
  clears), and a `BREAK` request from a remote/client is ignored (not injected
  into the BBC's receiver).
- **`NOTIFY-LINESTATE`** (framing / parity / overrun / break flags). Also
  sub-byte. *Fall-back:* line errors on the remote link are invisible to the BBC
  (a corrupted byte arrives as-is or is dropped by the remote); the server emits
  no line-state, so a client sees a steady "no errors" state.
- **Honouring the line/modem-state masks and flow-control *enforcement*.** The
  masks are echo-accepted but not used to filter notifications; flow control
  beyond the connection-state + RTS/CTS gating is not enforced.

These can be revisited if a concrete need appears; none of them is commonly used
by BBC serial software.

## Security

RFC 2217 is **plain Telnet — no encryption, no authentication**. Anyone who can
reach the port can drive the BBC's serial line. The server therefore **binds
`127.0.0.1` by default**; for remote access, tunnel over SSH/WireGuard rather
than binding a public address. It serves **one client at a time** (a serial port
is one cable); further connections are rejected.

## Implementation

- `src/extensions/rfc2217-common/Rfc2217Codec.{hpp,cpp}` — the shared
  Telnet/COM-PORT codec (golden-vector tested against RFC 2217 / pySerial).
- `src/extensions/rfc2217-client-serial/` — the client endpoint + extension.
- `src/extensions/rfc2217-server-serial/` — the server endpoint + extension.
- `src/core/include/beebium/net/TcpServerSerialPort.hpp` — the listen/accept-one
  transport (the server's; the client reuses `TcpClientSerialPort`);
  `EndpointUrl.hpp` parses the `url=` form.

Tests: `test_rfc2217_codec` (golden vectors), `test_rfc2217_client_endpoint` /
`test_rfc2217_server_endpoint` (in-process, the real client and server endpoints
talking over TCP), the extension config tests, and an **opt-in
`test_rfc2217_pyserial_client`** that drives the server from a real pySerial
client (skipped when pySerial/Python is absent; run via `uv run --with pyserial`).
