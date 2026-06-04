# Network serial: IP232 and RFC 2217

Research notes exploring two ways to carry the BBC's serial port (RS423) over a
TCP/IP network, and how each might become a `SerialPortDevice` PeripheralExtension
in Beebium (the Phase 8 "validate the seam" work in
`serial-refactoring-plan.md`). This is exploratory -- no code yet.

Primary sources:
- BeebEm's implementation: `/Users/rjs/Code/beebem-windows/Src/IP232.cpp` +
  `Serial.cpp` (the authoritative IP232 definition -- there is no formal spec).
- BeebEm help: <https://acorn.huininga.nl/pub/software/BeebEm/BeebEm-4.14.68000-20160619/Help/serial.html>
- tcpser (the usual IP232 server): <https://github.com/go4retro/tcpser>
- RFC 2217 "Telnet Com Port Control Option": <https://datatracker.ietf.org/doc/html/rfc2217>
- A Rust RFC 2217 *server* crate: <https://github.com/esp-rs/rfc2217-rs>

---

## TL;DR / recommendation

| Candidate | Build? | Why |
| --- | --- | --- |
| **IP232 client** | **Yes (primary)** | The retro headline: dial BBSes / talk to a virtual modem (tcpser), exact BeebEm parity. Tiny protocol, cheap to implement. |
| **RFC 2217 client** | **Yes (valuable)** | Lets the BBC drive a *real remote* serial device over the network (e.g. a ser2net box, or a networked FujiNet). The network analogue of host-serial. |
| **RFC 2217 server** | **Consider** | The *standards-based* way to let any off-the-shelf serial tool (pyserial, socat, tio, minicom via `rfc2217://`) reach the *emulated* BBC port over the network -- the universal-client twin of rpc-serial. Caveats: unauthenticated plain Telnet, point-to-point, cosmetic baud. |
| **IP232 server** | **No** | No real use case -- IP232 exists for "emulator dials out to tcpser", not the reverse. |

In Beebium terms: **both are just more `SerialPortDevice` extensions** (siblings of
host-serial / rpc-serial / loopback), each wrapping a TCP socket + a small protocol
codec instead of a tty. They reuse the host-serial machinery (bounded queues, an
async reader/writer thread, `/CTS` back-pressure, the no-stall invariant). Start
with the two *client* roles; defer the servers.

---

## IP232

### What it is

IP232 is an ad-hoc, minimal protocol (no formal spec; defined by tcpser and the
BeebEm source) for carrying an emulated serial port over a TCP stream. Its reason
for existing is retro telecomms: connect the BBC's RS423 to a **virtual Hayes
modem** (tcpser), which in turn bridges to telnet BBSes and other TCP services.

### Roles (who connects to whom)

**The emulator is the TCP client.** BeebEm `connect()`s out to an IP232 *server*
at `IP232Address:IP232Port` (default **25232**); tcpser is that server, listening
on 25232 and bridging to its (real or virtual) modem side. So:

```
  BBC serial  <->  Beebium IP232 extension (TCP client)  <--TCP-->  tcpser (server, :25232)  <->  telnet BBS / TCP
```

(`IP232.cpp:115-156` -- `socket()` + `connect()` to the configured address/port.)

### Wire protocol

There are two modes (`IP232.cpp:22-32`, `73`):

1. **Raw mode** (`IP232Raw = true`): a pure byte pipe. No escaping. The TCP
   connection is opened/closed as RTS rises/falls; CTS reflects connection status.
   Good for a plain socket peer that doesn't understand the escape convention.

2. **IP232 mode** (`IP232Raw = false`, the tcpser default): a *persistent*
   connection with minimal in-band signalling using `0xFF` (255) as an escape/flag
   byte:
   - **Outbound** (Beeb -> server): a data byte of `0xFF` is doubled -> `0xFF 0xFF`
     (`Serial.cpp:280-281`). An RTS change is sent as `0xFF <0|1>` when handshake
     is enabled (`IP232SetRTS`, `IP232.cpp:288-297`).
   - **Inbound** (server -> Beeb): `0xFF 0x01` = DTR-high, `0xFF 0x00` = DTR-low,
     `0xFF 0xFF` = a literal `0xFF` data byte (`IP232.cpp:433-470`). DTR from the
     "modem" is presented to the Beeb as DCD/CTS.

That is the *entire* control channel: carrier/handshake (DTR/DCD/RTS/CTS) via the
`0xFF` escape, plus raw data. **There is no baud / parity / framing negotiation** --
those stay local to the emulated ACIA + Serial ULA. IP232 only tunnels bytes and a
couple of modem lines.

### Notes for a Beebium implementation

- Trivially small: a TCP client socket + the `0xFF` escape state machine + the
  raw/non-raw toggle. Map the escape-conveyed DTR to the BBC's DCD, and the BBC's
  RTS to the outbound `0xFF <0|1>`.
- CLI shape would mirror host-serial, e.g.
  `--ip232-serial host=localhost:port=25232:mode=ip232|raw`.
- BeebEm runs the socket on a background read thread with ring buffers -- exactly
  the shape of our `HostSerialEndpoint` (reader/writer threads + bounded queues),
  so the host-side primitives port over directly.

---

## RFC 2217

### What it is

RFC 2217 is an IETF standard: the **Telnet Com Port Control Option**. It extends a
Telnet session so a client can *remotely configure and drive a physical serial
port* on an "access server": set baud rate, data size, parity, and stop bits;
assert/clear DTR, RTS and BREAK; choose flow control; and receive unsolicited
**modem-state** and **line-state** notifications (DCD, RI, DSR, CTS, errors).

### Roles

- **Access server (the TCP server)** owns the *physical* serial port, accepts
  Telnet connections, bridges bytes, applies the client's config commands, and
  emits state notifications. (e.g. `ser2net` on Linux; the `esp-rs/rfc2217-rs`
  crate is exactly this -- it exposes a local `/dev/ttyUSBx` as an RFC 2217 server.)
- **Client** initiates the Telnet session, negotiates the option, sends config
  commands, and consumes notifications. (e.g. pySerial's `rfc2217://host:port`.)

So the device-with-the-real-port is the server; the application is the client.

### Wire protocol

Layered on Telnet. The option is negotiated with `IAC WILL/DO COM-PORT-OPTION`,
then commands ride in subnegotiations: `IAC SB COM-PORT-OPTION <cmd> <value...>
IAC SE`. Data bytes equal to `IAC` (0xFF) are escaped as `IAC IAC` -- structurally
the same idea as IP232's `0xFF` doubling, but inside the Telnet framing. Commands
are acknowledged with the *actual* value applied (which may differ from the
request), and the server pushes NOTIFY-MODEMSTATE / NOTIFY-LINESTATE with
bit-mapped status the client can filter.

### Notes for a Beebium implementation

- Richer than IP232: a Telnet IAC parser + the COM-PORT subnegotiation set +
  modem/line-state mapping. Still a bounded socket codec on top of the same
  endpoint machinery.
- **As a client**, Beebium would point at a remote access server
  (`rfc2217://host:port`) and treat it like a remote host-serial device -- it can
  even set the *remote* port's real baud, which IP232 cannot.
- **As a server**, Beebium would expose the *emulated* BBC port over TCP, so a
  remote RFC 2217 client could connect across the network and *be* the device on
  the far end of the BBC's serial cable -- read what the BBC transmits, send bytes
  it receives, exchange the control lines. This is the **standards-based twin of
  rpc-serial**: rpc-serial already lets a remote client be the serial device, but
  only via Beebium's own gRPC API (the client must be Beebium-aware); an RFC 2217
  server speaks a *universal* protocol, so off-the-shelf tools connect with zero
  knowledge of Beebium -- e.g. `python -m serial.tools.miniterm rfc2217://host:port`,
  or `socat`/`tio` bridging `rfc2217://` to a local pty that *any* serial program
  (minicom, kermit, even another emulator) then talks to. That universal-client
  reach is the real appeal, and the reason this is rated "consider" rather than
  "defer". Caveats:
  - **Security.** RFC 2217 is plain Telnet -- **no encryption, no authentication**.
    Anyone who can reach the port can drive the BBC's serial line. Default the
    bind address to loopback; expect users to tunnel over SSH/WireGuard for remote
    access; be explicit that the raw service is unauthenticated.
  - **Cosmetic baud.** Client-requested baud/parity are advisory against an
    emulated UART -- the BBC's actual line rate is whatever guest software programs
    into the Serial ULA (the host-baud-vs-BBC-baud distinction). The server honours
    control lines + tunnels bytes; param-negotiation is echo-accepted.
  - **Point-to-point.** A serial port is one cable: one active client at a time;
    accept one connection and reject/queue the rest.

---

## IP232 vs RFC 2217

| | IP232 | RFC 2217 |
| --- | --- | --- |
| Specification | ad-hoc (tcpser / BeebEm source) | IETF standard (Telnet option) |
| Framing | raw bytes + `0xFF` escape | Telnet `IAC SB COM-PORT-OPTION ... IAC SE`, `IAC IAC` escaping |
| Configure remote UART (baud/parity/...)? | No (local-only) | Yes |
| Control lines | DTR / DCD / RTS via `0xFF` escape (or raw connect=RTS) | DTR / RTS / BREAK + modem-state / line-state notifications |
| Flow control | minimal (RTS/CTS; raw-mode connect on RTS) | inbound/outbound: none / XON-XOFF / hardware / DCD / DSR / DTR, plus Telnet-level suspend/resume |
| Who owns the physical port | the far end is usually a *virtual* modem (tcpser) -- no real port | the access server owns a *real* UART |
| Emulator's natural role | TCP **client** to tcpser | RFC 2217 **client** to ser2net (or **server** exposing its own port) |
| Complexity | tiny | moderate (Telnet negotiation + notifications) |
| Use case | retro modem / BBS dial-out | drive a real remote serial device; standards-based remote access |

They are not the same thing, though they rhyme: both tunnel serial-over-TCP with an
escape byte and some line-state signalling. IP232 is a thin retro-comms hack; RFC
2217 is a general remote-serial standard that can actually reconfigure a remote
UART and report its line states in detail.

---

## How both fit Beebium's `SerialPortDevice` seam

Everything we built for serial makes these "just another extension":

- Each is a **PeripheralExtension** that attaches to the `SerialPort` handle and
  presents a `SerialPortDevice` to the ACIA/Serial ULA -- exactly like host-serial,
  rpc-serial, loopback. No core or `SerialService` changes; this is the payoff of
  the open seam.
- Each reuses the **host-serial endpoint machinery**: an async reader/writer thread
  over the transport, bounded queues, and `/CTS` back-pressure to the guest -- the
  [no-external-peer-stalls-the-emulator](feedback_no_external_peer_stalls_emulator)
  invariant. The "port" is a TCP socket + a protocol codec instead of a tty; the
  rest is shared. A socket-backed analogue of `HostSerialPort` would slot under the
  existing `HostSerialEndpoint` design.
- Each can carry a **typed config gRPC API** (like the new `HostSerial` service)
  for endpoint/host/port and mode, plus the ExtensionUi panel -- the multi-API
  pattern, for free.
- The **baud-ownership subtlety** matters: as a *client*, the remote end's baud is
  real (RFC 2217 can set it; IP232 cannot); as a *server*, the BBC's emulated rate
  is authoritative and a client's requested baud is cosmetic.

Likely extension names (sketch): `ip232-serial` (client), `rfc2217-serial`
(client), and -- if/when wanted -- `rfc2217-serial-server`.

---

## Configuration surface

All three are small, and each follows the existing manifest-parameter convention,
so each gets the CLI form (`--<name> key=val:key=val`), `describe-extension`, the
typed `GetConfig`/`SetConfig` API, and an ExtensionUi panel for free. `tx_buffer`
is the shared no-stall back-pressure mark, same as host-serial / rpc-serial.

### `ip232-serial` (client) -- connects out to a tcpser-style server

| key | type | default | notes |
| --- | --- | --- | --- |
| `host` | string | `localhost` | IP232 server address |
| `port` | int | `25232` | IP232 server port |
| `mode` | string | `ip232` | `ip232` (0xFF-escaped, persistent) or `raw` (pure pipe, connect/disconnect on RTS) |
| `handshake` | bool | `true` | convey RTS via the 0xFF escape (`ip232` mode only) |
| `tx_buffer` | int | `4096` | /CTS back-pressure mark |

`--ip232-serial host=bbs.example.com:port=25232`. No baud/framing -- IP232 does
not carry them; the rate is local to the emulated ACIA/ULA.

### `rfc2217-serial` (client) -- connects out to an access server

| key | type | default | notes |
| --- | --- | --- | --- |
| `host` | string | *(required)* | RFC 2217 access-server address |
| `port` | int | *(required)* | TCP port (RFC 2217 has no standard port) |
| `baud` | int | `19200` | **real** -- sets the *remote* UART's rate |
| `framing` | string | `8N1` | remote UART data/parity/stop (could expand to `data_bits`/`parity`/`stop_bits`) |
| `tx_buffer` | int | `4096` | back-pressure mark |

`--rfc2217-serial host=ser2net.local:port=4001:baud=9600`. This is the one place
baud genuinely configures hardware.

### `rfc2217-serial` server -- listens, exposes the emulated BBC port

| key | type | default | notes |
| --- | --- | --- | --- |
| `bind` | string | `127.0.0.1` | **security: loopback by default** |
| `port` | int | *(required)* | TCP listen port |
| `baud` | int | `19200` | advisory default reported to clients (**cosmetic** vs the emulated UART) |
| `framing` | string | `8N1` | advisory default |
| `tx_buffer` | int | `4096` | back-pressure mark |

`--rfc2217-serial role=server:bind=0.0.0.0:port=4001`. Essentially "a port +
default settings", plus the one thing that matters: a loopback-by-default bind.

### Patterns and one design choice

- **baud** is *real* for the RFC 2217 client, *advisory* for the server, *absent*
  for IP232 -- a clean illustration of the host-vs-BBC-baud distinction.
- **Connect-out vs listen** is the only structural difference: clients take
  `host`+`port`; the server takes `bind`+`port`.
- **One extension with `role=client|server` vs two.** RFC 2217 client and server
  share the identical Telnet/IAC + COM-PORT codec; only the socket lifecycle
  (connect vs accept) differs. Leaning to ONE `rfc2217-serial` extension with a
  `role` switch (the host-serial `mode=pty|device` pattern -- params vary by role),
  rather than separate `rfc2217-serial` / `rfc2217-serial-server` extensions.

## Recommendation and next steps

1. **`ip232-serial` (client) first.** Highest retro value, BeebEm parity, smallest
   protocol. Validates that a *network-backed* `SerialPortDevice` works end to end
   against a real third-party server (tcpser on :25232). Good first proof of the
   seam.
2. **`rfc2217-serial` (client) next.** Lets the BBC use a real remote serial device
   (ser2net, a networked FujiNet, ...) -- the network sibling of host-serial, and
   the one that can set the remote baud.
3. **RFC 2217 *server* -- consider.** The standards-based way to let *any*
   off-the-shelf serial tool reach the emulated BBC port over the network (the
   universal-client twin of rpc-serial -- no Beebium-aware client needed). More
   compelling than first rated. Gated on accepting the caveats above: ship it
   loopback-bound by default with an explicit "unauthenticated" stance, treat baud
   as advisory, and serve one client at a time. Worth doing once the two clients
   prove the seam.
4. **IP232 *server* -- skip** unless a use case turns up.

Open questions to resolve before coding:
- Confirm tcpser's exact non-raw framing against `IP232.cpp` with a live capture
  (especially the inbound DTR/`0xFF` cases and the handshake/RTS direction).
- Decide the connection lifecycle for a *client* extension (connect at init vs
  on-demand; reconnect policy; how a dropped TCP connection maps to DCD/CTS so the
  guest sees a clean "carrier lost" rather than a host stall).
- RFC 2217 scope: a minimal client (negotiate option, set baud + DTR/RTS, consume
  modem-state) is enough for the FujiNet-style case; full flow-control + line-state
  filtering can come later.
- Whether the two clients share a common "socket transport + pluggable codec"
  base, with IP232 and RFC 2217 as codecs over it.
