# PiEconetBridge: kernel module silently drops Piconet scouts

Investigation log for an unresolved interaction between the Piconet
USB-CDC device and PiEconetBridge (https://github.com/cr12925/PiEconetBridge),
discovered while building Beebium's Piconet hardware contract test
suite. This is **not a Piconet defect** -- the Piconet side has been
verified to do its job correctly. The drop happens after the kernel
module receives our frames but before they reach the bridge's
user-space process.

We may revisit this with PiEconetBridge source-level help in the
future. Until then, Beebium's `[needs-station-registered]` test tag
documents that the wire-roundtrip test depends on a peer environment
we can't yet make reliable.

## Setup

- Pi 3 running PiEconetBridge v2.1 (commit 57243a1) on
  `/dev/econet-gpio` (Pi Econet HAT v2 r2a or later).
- Bridge config:
    ```
    WIRE NET 1 ON DEVICE /dev/econet-gpio
    FILESERVER ON 1.254 PATH /home/rjs/econetfs
    EXPOSE HOST 1.254 ON PORT *:32768
    ```
- The same setup is known to work correctly with a real BBC Microcomputer
  on the wire as a client of the fileserver at 1.254.
- Piconet at `/dev/tty.usbmodem101` on the developer's macOS host,
  attached to the same physical Econet wire as the Pi via the standard
  Econet socket on the Piconet board.

## What works

1. **Piconet's CLK LED follows the wire clock**: lights when the
   cable is plugged in, goes out when unplugged. So the Piconet
   correctly synchronises to the wire's clock generator.
2. **Piconet's TX/RX activity LEDs flicker** during Beebium tests --
   physical signaling is happening in both directions.
3. **`econet-monitor` (PiEconetBridge utility) running on the Pi
   sees our wire frames**. With the bridge stopped and
   `econet-monitor` reading `/dev/econet-gpio` directly, sending five
   broadcasts and one scout from the Piconet produced exactly six
   captured packets:

       --- PACKET ---  DST 0xff/0xff  SRC 0x00/0x20   ff ff 20 00
       (six broadcasts)
       --- PACKET ---  DST 0x00/0xfe  SRC 0x00/0x20   fe 00 20 00 80 99
       (one scout to the fileserver)

   The scout has the right addressing (dest 0.254, src 0.32), the
   right ctrl byte (0x80 with the wire's scout high bit set), and the
   right port (0x99). Piconet's wire transmission is correct.

   (Side note: the captured broadcasts show only the 4-byte
   address header, with no ctrl/port/payload bytes. We sent
   broadcasts whose payload was 4-5 bytes. Either `econet-monitor`
   defaults to brief output, or Piconet's BCAST is genuinely
   truncating to 4 bytes on the wire. Worth following up but
   orthogonal to the main issue here.)

## What does not work

When the bridge runs (any flag combination tried, including
`-p iIoO -z -z -z -z -z -e -d /tmp/bridge.log`), Beebium's
`TX to fileserver` test reliably returns `TX_RESULT NO_SCOUT_ACK`.
The bridge's debug log shows only its startup messages and a
periodic pool-collector tick every 60 seconds:

    [+   0.431s] BRIDGE  : Internal  Bridge reset from internal
    [+   0.431s] BRIDGE  : Internal  Networks list reset
    [+   0.431s] BRIDGE  : Internal  Station set reset on wire network 1
    [+   0.431s] BRIDGE  : Internal  Sender net is 0 for Wire device net 1
    [+   0.431s] BRIDGE  : Wire   1  Unable to find sender net. Not sending bridge reset.
    [+   0.431s] DESPATCH: Wire   1  Despatcher thread infinite condwait
    [+  60.029s] POOL    : Pool garbage collector running

Crucially, the kernel module's `irq_read` path -- which would log
`"econet-gpio: econet_irq_read(): AUN: Scout received from %d.%d
with port %02x, ctrl %02x. Acknowledging."` (kernel module
econet-gpio-module.c line 1616, an unconditional `printk`) -- never
fires for our scouts. With kernel printk verbosity maxed out
(`sysctl -w kernel.printk="8 8 8 8"`) and the kernel module's
`extralogs` flag enabled (`-e` passed to the bridge), `dmesg` shows
zero entries during a test run.

## The interpretation

- The wire-level signaling is good (econet-monitor sees frames; LEDs flicker).
- The kernel module receives the frames in raw mode (econet-monitor delivers them).
- The kernel module does NOT deliver them via its AUN-mode receive
  path (irq_read line 1616 doesn't log; bridge sees nothing).
- So: when the bridge puts the kernel module into AUN mode, frames
  with `dest=0.254` and `src=0.32` are being silently dropped before
  they reach the user-space bridge.

The bridge's startup message `Sender net is 0 for Wire device net 1.
Unable to find sender net.` is suspicious -- it's the only place the
bridge mentions net 0 handling. Possibly related: when the bridge
configures the kernel module's station set, frames with `srcnet=0`
may be filtered out as "no recognised sender net" before
classification reaches the scout-detection path.

## Why a real BBC works in the same setup

The user reports that a real BBC Microcomputer at station 221, on
the same wire and bridge config, talks to the fileserver fine.
Differences between the BBC and the Piconet that may matter:

1. The BBC's NFS ROM may issue a Bridge Query at boot and learn the
   wire's actual net number (1), then send subsequent traffic with
   `srcnet=1`. The Piconet firmware always stamps `srcnet=0`
   (econet.c lines 164, 174 -- documented as upstream issue #4 in
   piconet-upstream-issues.md). PiEconetBridge's docs note that BBC
   Bs do NOT do a Bridge Query on reset, but RISC OS or other clients
   may.

2. The BBC may emit additional handshake traffic the bridge looks for
   before adding the station to its set. The Piconet sends only what
   the host asks it to.

3. There may be a kernel-module station-set learning step that's
   triggered by traffic patterns the BBC produces and the Piconet
   doesn't.

## Where to dig next time

When PiEconetBridge investigation resumes:

1. **Run with the BBC at station 221 and the Piconet on the wire
   simultaneously** and compare bridge logs frame-by-frame. The
   BBC is a known-working peer in this exact setup. Differences
   in how the bridge logs/processes BBC traffic vs Piconet traffic
   should isolate the behaviour quickly. Test variants:
   - BBC quiet, Piconet sends TX to fileserver (1.254): same as
     today's failing case, but with the BBC providing baseline.
   - BBC loads a file from fileserver, Piconet quiet: confirm the
     bridge still works for known clients.
   - BBC loads a file, Piconet sends TX in the middle: see if the
     bridge's having-acked-the-BBC state changes how it handles
     Piconet frames.
   - Piconet sends TX to the BBC at 221 (instead of the fileserver
     at 254): the BBC's NFS ROM should scout-ack any TX addressed
     to it. This bypasses the bridge entirely on the response path.
2. Read `econet-gpio-module.c` around lines 1450-1620 to map the
   AUN-mode receive classification: where exactly an inbound frame
   with `srcnet=0, dstnet=0, dst=254` gets dropped vs delivered.
3. Read `econet-hpbridge.c` for "Sender net" handling to understand
   what the startup log message implies about per-frame source-net
   resolution.
4. Trace what `Station set updated` actually configures in the
   kernel module -- the message fires twice at startup (suggests
   two separate ioctl calls). Find the corresponding kernel
   handler.
5. Try forcing srcnet=1 on the wire by patching the Piconet firmware
   temporarily (one-line change to econet.c lines 164, 174) and see
   if it changes the bridge's behaviour.

## Related upstream-Piconet observation

The captured-broadcast truncation noted above (ff ff 20 00 with no
ctrl/port/payload) is independent of this kernel-module issue and
worth investigating separately. If real, it would mean Piconet's
BCAST drops user-supplied payload on the wire -- a clear firmware
bug. If it's just `econet-monitor` truncating its display by default,
the issue is presentational. To resolve: send a single distinctive
broadcast and trace via the kernel module's RX path (line 1788 area)
to see the on-wire byte count.
