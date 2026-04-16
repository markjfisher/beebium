# Piconet Upstream Issues

Defects and improvement candidates noticed in [Piconet](https://github.com/jprayner/piconet)
while implementing Beebium's `PiconetBackend`. Each entry is sized to be reviewable
and, if appropriate, submittable as an upstream issue or PR. Entries cite the
firmware source at /Users/rjs/Code/piconet (a local clone of the upstream repo);
line numbers are accurate as of the version we worked against.

This file is **not** a tracking issue inside Beebium -- the work continues
regardless of what upstream does. It is a discovery log we can act on later.

---

## 1. `_decode_base64` lacks output buffer bounds checking (real overflow risk)

**File:** `board/src/piconet.c` lines 409-419
**Severity:** Likely exploitable buffer overflow; firmware author flagged with TODO.

```c
size_t _decode_base64(const char* input, uint8_t* output_buffer) {
    if (input == NULL) {
        return 0;
    }

    // TODO: check for buffer overflow
    uint8_t* c = output_buffer;
    base64_decodestate s;
    base64_init_decodestate(&s);
    return base64_decode_block(input, strlen(input), c, &s);
}
```

The function takes only an output pointer, no length. Callsite:

```c
cmd.tx.data_len = _decode_base64(strtok(NULL, delim), cmd.tx.data);
```

`cmd.tx.data` is a fixed `uint8_t[TX_DATA_BUFFER_SZ]` (3500 bytes). The
incoming command line is buffered in `CMD_BUFFER_SZ = TX_DATA_BUFFER_SZ * 2 =
7000 bytes`, which can carry ~5250 bytes of decoded payload -- exceeding the
TX data buffer. A malicious or buggy host can trigger a stack/struct-relative
buffer overflow.

**Suggested fix:** add a `size_t output_buffer_sz` parameter, validate that
`(strlen(input) / 4) * 3` (an upper bound on decoded length) does not exceed
the buffer, and return an error code (or 0 with errno-style state) on
violation.

---

## 2. `_encode_base64` lacks output buffer bounds checking (TODO-flagged)

**File:** `board/src/piconet.c` lines 393-407
**Severity:** Lower than (1) -- buffers are sized 2x source -- but the same
shape of API defect.

```c
char* _encode_base64(char* output_buffer, const uint8_t* input, size_t len) {
    // TODO: check for buffer overflow
    char* c = output_buffer;
    ...
```

Currently safe in practice because `B64_DATA_BUFFER_SZ = RX_DATA_BUFFER_SZ * 2`
which is more than the worst-case base64 expansion (1.34x + null + padding).
Worth adding a length parameter for symmetry with the decode fix and to
prevent regressions if buffer sizes ever drift.

---

## 3. `cmd_tx_t::scout_extra_data` is sized `TX_DATA_BUFFER_SZ` (~3500 bytes too large)

**File:** `board/src/piconet.c` lines 104-113
**Severity:** Memory waste on a constrained device; firmware author flagged
with TODO.

```c
typedef struct {
    uint8_t                 dest_station;
    uint8_t                 dest_network;
    uint8_t                 control_byte;
    uint8_t                 port;
    uint8_t                 data[TX_DATA_BUFFER_SZ];
    size_t                  data_len;
    uint8_t                 scout_extra_data[TX_DATA_BUFFER_SZ]; // TODO: really this long?
    size_t                  scout_extra_data_len;
} cmd_tx_t;
```

`TX_SCOUT_BUFFER_SZ = 32`, and the scout header is 6 bytes (4 address +
ctrl + port), so `scout_extra_data` can be at most 26 bytes. The 3500-byte
allocation is ~140x larger than needed, wasting ~3.4 KB of SRAM per command
struct on the Pi Pico (264 KB total).

**Suggested fix:** change to `uint8_t scout_extra_data[TX_SCOUT_BUFFER_SZ - 6]`
(or define a `MAX_SCOUT_EXTRA = 26` constant for clarity), and have the
`_decode_base64` rework above use the correct bound.

---

## 4. MachinePeek canned response is hardcoded firmware-side (cannot be overridden)

**File:** `board/src/econet.c` lines 605-616
**Severity:** User-visible misidentification; design choice that may be
intentional but worth a configurability lever.

```c
uint8_t machine_type = 0x55;    // U = undefined
uint8_t manufacturer_id = 0x4a; // J = JPR
uint8_t version_minor = 0x00;   // first release
uint8_t version_major = 0x05;   // normal 32-bit client
```

Any peer probing the Piconet's MachinePeek (control byte 0x88) sees this
canned identity, regardless of what machine is connected via the host
driver. For a host that wants to advertise a specific machine class (BBC
Model B, BBC Master, an emulator presenting itself as either), there's no
way to override.

**Suggested fix:** add a `SET_IDENTITY <machine_type> <manufacturer_id>
<ver_minor> <ver_major>` command. Default to the existing canned response;
let drivers customise. Beebium would set the identity to match the
emulated machine variant (Model B / B+ / Master).

This is the most user-visible defect from a Beebium perspective -- it
means a real Acorn fileserver scanning the Econet sees "JPR" as the
manufacturer of every emulated BBC.

---

## 5. Documentation gap: TX command's ctrl byte requires the wire high bit

**File:** `README.md`, `board/src/piconet.c` (TX command parser, line 477)
**Severity:** Documentation; trips up new implementers.

The TX command parser stores the ctrl byte literally:

```c
cmd.tx.control_byte = strtol(strtok(NULL, delim), NULL, 10);
```

And the firmware writes it directly into the scout buffer:

```c
_tx_scout_buffer[4] = control;
```

The Econet wire convention is that scout frames have the high bit (`0x80`)
set on the ctrl byte to distinguish scout from data. The firmware does
NOT OR this bit on; the host is responsible. The README, the protocol
documentation, and the parser code do not call this out -- a careful
reading of the TypeScript driver examples (e.g. `login-server` uses
`controlByte = 0x80`) is the only signal.

**Suggested fix:** README addition explaining the convention. Optionally,
make the firmware OR `0x80` itself for safety (would need a compatibility
note for existing drivers that already set the bit -- ORing twice is a no-op,
so this is backwards-compatible).

This bit Beebium during Phase 4: our PiconetBackend first sent ctrl bytes
without the high bit (because FourWayHandshake stores them post-mask) and
the resulting scouts would have been malformed on a real wire. Easy to
miss; explicit documentation would prevent recurrence.

---

## 6. REPLY command parser remains live with no working code path

**File:** `board/src/piconet.c` lines 484-487; `board/src/econet.c` lines
206-235, 635-645 (commented out), 774-782.
**Severity:** Confusing failure mode; documented in our feasibility paper
but not in upstream README.

The REPLY command path was implemented and then deliberately disabled in
commit `168d466`. The host-side parser still accepts `REPLY <id> <data>`,
queues it for processing, and runs through `reply()`, which always fails
with `INVALID_RECEIVE_ID` because `_pending_reply.valid` is never set to
true. The firmware author left this in place for potential future
re-enabling.

A naive driver that issues REPLY (perhaps in response to an `RX_TRANSMIT`
event with an unset `needs_reply` flag) will see only the cryptic
`REPLY_RESULT INVALID_RECEIVE_ID` with no upstream documentation that REPLY
is non-functional.

**Suggested fix:** either
(a) reject REPLY at parse time with `ERROR REPLY currently unsupported`, or
(b) document the limitation in README so drivers don't issue it, or
(c) re-enable the path with a clear design for handling host-driven flag-fill
    safely (this was the path the firmware author chose to abandon -- it has
    real architectural difficulty per the commit history).

Recommendation: (a) plus (b). The dead code is a footgun for new driver
authors.

---

## 7. Event queue depth (`QUEUE_SZ_EVENT = 6`) may drop frames under sustained load

**File:** `board/src/piconet.c` line 33
**Severity:** Performance / reliability under bursty inbound traffic;
needs investigation, not yet confirmed as a real issue in practice.

```c
#define QUEUE_SZ_EVENT          6
```

The event queue carries inbound RX events from Core 1 (wire) to Core 0
(USB serial output). If Core 0 falls behind -- e.g. a slow USB host or
a momentarily congested CDC pipe -- Core 1's `queue_add_blocking()` calls
back-pressure, which means the ADLC's RX buffer can overflow before the
event makes it across cores.

For a busy fileserver pulling many small packets per second, 6 events
of headroom may be marginal. Worth measuring under load.

**Suggested investigation:** instrument the firmware to count
`queue_full` events and report them via STATUS. If non-zero under
realistic load, raise the queue depth (cheap on RAM) or add a
`MONITOR_OVERRUN` style event to make drops observable to the host.

---

## 8. Asymmetric line terminators (commands `\r`, events `\n`) are undocumented

**File:** `board/src/piconet.c` lines 191/200/etc. (events use `\n`),
line 430 (commands parsed on `\r`)
**Severity:** Documentation; surprises new implementers.

Commands sent to the firmware are terminated by carriage return (`\r`).
Events emitted by the firmware are terminated by line feed (`\n`).
This asymmetry is correct (it lets a typical line-oriented serial tool
treat the device as readable text in either direction) but the README
does not mention it; we discovered it by reading both the command parser
and the event printf calls.

**Suggested fix:** README addition under a "Protocol grammar" section.

---

## How to use this document

When reviewing for upstream submission:

1. Group items by intent: code defects (1, 2, 3), feature gaps (4, 6, 7),
   documentation (5, 8).
2. File code defects as separate issues; reference the line-number citations.
3. For (4), draft a `SET_IDENTITY` command proposal first -- it's the most
   user-impactful and warrants discussion before code.
4. Documentation items can be a single PR against `README.md`.
5. Item (7) needs measurement before submission. Beebium can contribute
   the instrumentation if/when we have a self-hosted CI runner and a way
   to drive sustained inbound traffic.

This document grows as we discover more during Phases 5-8 of the
PiconetBackend implementation.
