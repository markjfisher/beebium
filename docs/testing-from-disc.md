# Testing with real 6502 code: beebasm -> bootable disc -> autoboot

Some behaviour can only be trusted once *real 6502 code* exercises it on a
booted machine, through the actual MOS and hardware registers -- not the C++
device seam in isolation. This note describes a reusable pattern for that:

```
  6502 source  --beebasm-->  auto-booting DFS .ssd  --insert-->  Model B  --autoboot-->  your program runs
       |                                                                                        |
   (your test asset)                                                              writes a sentinel to zero page
                                                                                                |
                                                              C++ harness steps the CPU, peeks the sentinel, asserts
```

It is far cheaper than typing a BASIC program through the keyboard matrix (slow,
fragile) or hand-poking opcodes into RAM, and it runs a genuine BBC application:
assembled by a real assembler, loaded from a real disc image, launched by the
real DFS/MOS. The worked example is the serial BREAK test
(`tests/test_serial_break_e2e.cpp` + `tests/assets/serial/serial_break.asm`).

## When to reach for it

Use it when the thing under test is driven by guest software touching hardware
the MOS mediates: serial (ACIA/SERPROC), the Tube, Econet (ADLC), the 1MHz bus,
sound, the VIAs, sideways RAM paging, filing-system calls. If a pure C++ unit
test can already reach the seam (most cases), prefer that -- it is faster and
needs no ROMs. Escalate to this pattern when you specifically want to prove the
*guest-software-to-hardware path*, end to end.

## The four pieces

### 1. The 6502 program (beebasm source)

Write a small program that does the hardware operations and then writes a
**sentinel** byte to zero page so the C++ side knows it finished. Keep the
result observable and the control flow trivial (do the work, set the sentinel,
spin):

```asm
SENTINEL = &70            \ &70-&8F is reserved for the user; the MOS won't touch it
ORG &1900                 \ PAGE on a DFS machine -- the normal load address

.prog
    \ ... touch the hardware registers directly ...
    LDA #&FF : STA SENTINEL
.spin
    JMP spin

SAVE "PROG", prog, P%     \ load = exec = &1900
```

`beebasm`'s `IF MODE = n / ELSE / ENDIF` plus `-D MODE=<n>` lets one source
assemble into several variants (e.g. a "transmit" and a "detect" build) without
duplicating the setup.

### 2. Assemble into an auto-booting disc

```
beebasm -i serial_break.asm -D MODE=0 -do out.ssd -boot PROG -title MYTEST
```

`-do out.ssd` writes a DFS single-sided disc image; `-boot PROG` adds a `!BOOT`
file (`*BASIC` then `*RUN PROG`) and sets `*OPT 4,3`, so the disc auto-runs your
program. Generate it **at test time** and skip when `beebasm` is absent (see the
discovery helper below) -- the `.asm` is the checked-in, reviewable artefact;
the `.ssd` is a throwaway in the temp dir.

### 3. Boot a Model B from it (the harness)

```cpp
ModelB machine;
machine.memory().load_mos(mos.data(), mos.size());          // acorn-mos_1_20.rom
machine.memory().load_basic(basic.data(), basic.size());   // bbc-basic_2.rom
machine.memory().load_sideways_rom(14, dfs.data(), dfs.size());  // acorn-dfs_2_26.rom
machine.memory().install_acorn_1770();                     // add the 1770 FDC to a Model B

auto disc = load_disc_from_url_or_filepath(ssd.string());  // <beebium/disc/DiscLoader.hpp>
machine.memory().disc_drive_0.insert(std::move(disc.disc));

machine.memory().set_device_or_whatever(...);              // wire in the subsystem under test
machine.memory().enable_video_output();
machine.memory().set_auto_boot(true);                      // the SHIFT-BREAK equivalent
machine.reset();
```

`set_auto_boot(true)` is the key call: it makes DFS run `!BOOT` on reset, exactly
as holding SHIFT during BREAK would, so no keystrokes are injected at all. (This
is the same boot path the CE2023 game-load test uses,
`tests/test_tube_ce2023_trace.cpp`.) A Model B+ has the 1770 built in, so it
needs neither `install_acorn_1770()` nor a separate DFS slot -- `ModelBPlus`
with ADFS/DFS in the DFS slot is the `tests/test_scsi_adfs_boot.cpp` template.

### 4. Run to the sentinel and assert

```cpp
bool ran = false;
for (std::uint64_t i = 0; i < 30'000'000; ++i) {           // generous budget; boot is ~4-5M
    machine.step();
    if (machine.memory().video_output.has_value())
        renderer.process(machine.memory().video_output.value());
    if ((i & 0xFFFF) == 0 && machine.peek(0x0070) == 0xFF) { ran = true; break; }
}
REQUIRE(ran);
// ... assert whatever the subsystem under test recorded ...
```

`peek()` is the side-effect-free read; poll the sentinel every so often and
early-exit. Budget for boot plus the work; on a fast host the whole thing is
well under a second.

## Determinism tricks (learned from the serial test)

- **Own the hardware directly when you need precise control.** The serial
  program writes the ACIA/SERPROC registers itself rather than going through the
  MOS RS423 driver, so interrupt-driven MOS buffering can't race the test. Note
  the MOS-level equivalent in a comment (e.g. `OSBYTE &9C`) for the reader.
- **Disable the MOS interrupt that would consume your event.** To *detect* an
  inbound condition by polling, turn off the relevant interrupt (the serial test
  clears the ACIA receive-interrupt bit) so the MOS ISR doesn't read and clear
  the hardware before your foreground loop sees it.
- **Use a readiness handshake for inbound timing.** When an *external* peer must
  act only after the guest has set things up (e.g. selected the port / raised
  carrier), have the program write a second sentinel ("ready") first; the harness
  waits for it, *then* triggers the peer. This avoids losing an event the guest
  isn't yet listening for.
- **Sentinels live in &70-&8F.** That block is reserved for the user, so the MOS
  leaves it alone across the run.

## Skipping cleanly when tools are absent

beebasm (and any external peer such as pySerial) won't exist on every CI
runner. Discover them and `SKIP` -- the same idiom the pySerial serial tests use:

```cpp
std::string find_beebasm() {
    for (auto& c : {std::string(std::getenv("BEEBIUM_BEEBASM") ?: ""), std::string("beebasm")})
        if (!c.empty() && std::system((c + " --help >/dev/null 2>&1").c_str()) == 0) return c;
    return "";
}
// ... if (beebasm.empty()) SKIP("beebasm not found");
```

Pair it with `catch_discover_tests(<target> PROPERTIES SKIP_RETURN_CODE 4)` in
`tests/CMakeLists.txt` so a skipped run reports as *skipped*, not failed. The
ROMs come from `BEEBIUM_ROM_DIR`; the `.asm` lives under `BEEBIUM_TEST_ASSETS_DIR`
(`tests/assets/`), both passed in as compile definitions.

## Reusing the pattern

The pieces generalise to any guest-driven subsystem -- swap the register pokes
and the wired-in device:

| Subsystem | Program touches | Harness wires in |
|---|---|---|
| Serial (the example) | ACIA `&FE08/9`, SERPROC `&FE10` | `serial_socket.set_device(...)` |
| Tube | Tube ULA `&FEE0-&FEE7` | a Tube backend / parasite |
| Econet | ADLC `&FEA0-&FEA3` | an Econet transport |
| 1MHz bus / SCSI | `&FCxx/&FDxx` (JIM/FRED) | the 1MHz-bus extension |
| Sound | the System VIA / SN76489 path | sound capture |

The constants (`set_auto_boot`, `install_acorn_1770`, `load_sideways_rom`,
`load_disc_from_url_or_filepath`, `disc_drive_0.insert`, `peek`) are the same in
every case. Start from `tests/test_serial_break_e2e.cpp` and
`tests/assets/serial/serial_break.asm` and change the middle.
