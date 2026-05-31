# Sideways Memory Refactor

Status: landed. The work described below is complete - this document
is kept for historical context.

Summary of what landed:
- New `SlotInfo` struct + `slot_info(uint8_t slot)` accessor on every
  sideways-bearing Memory variant (Model B, Model B ROM/RAM, B+, B+ 128K)
  and on `AliasedBankedMemory` / `ConfigurableBankedMemory`.
- `SidewaysService::HasAliasedSideways`, `HasConfigurableSideways`, and
  `HasMemorySlotKind` concepts deleted. Replaced with `HasSlotInfo` and
  `HasSlotMutators`. GetSlotStatus, ConfigureSlot, ReadSlotData and the
  live header scanner now dispatch through one path.
- `devices/UserRomSocketBank.hpp` extracted: 10 ConfigurableSlot user
  sockets plus their accessors and mutators, composed by both B+ variants.
  ~150 lines of duplication eliminated.
- `Memory::supports_slot_configuration()` removed from all six classes
  that defined it. `apply_sideways_configs` in ServerMain.hpp now relies
  on the topology validation that already ran upstream.
- IC71 SocketSpec on both B+ variants now reports `supports_rom=true,
  supports_ram=false, supports_empty=false, runtime_configurable=false`
  (the soldered MOS+BASIC system ROM).
- The S13 helper (Refactor 4 in the doc below) was not extracted. After
  Refactor A the residual S13 routing per B+ class is small enough that
  a classifier would add more indirection than it saves. The polarity
  inversion between `s13_is_south_` (B+ 64K) and `s13_is_north_` (B+
  128K) remains - flag for cleanup if a third B+ variant arrives.

The rest of this document is the original plan, kept verbatim.

---

Original audience: whoever picks this up next.

## Why this document exists

The recent Model B+ / B+ 128K work (live header scanner, configurable
user sockets, S13-aware BASIC routing) landed by extending the existing
shapes. The result works but has accumulated duplication, parallel
APIs, and split sources of truth. This doc captures what's wrong, why,
and a concrete refactor that another agent can pick up cold.

The two user-facing bugs that motivated the most recent edits are
already fixed; this is purely about maintainability.

## What's gotten messy

### 1. ModelBPlusHardware and ModelBPlus128KHardware are ~80% duplicates

Both files own the same machinery, line-for-line in most places:

| Concern | ModelBPlusHardware.hpp | ModelBPlus128KHardware.hpp |
|---------|------------------------|----------------------------|
| 10 ConfigurableSlot user-socket members | 165-174 | 166-175 |
| `basic_rom_image_name_` field | 181 | 179 |
| S13 cache (different polarity!) | `s13_is_south_` 186 | `s13_is_north_` 197 |
| `user_slot_for(slot)` + const overload | 742-758 | 763-779 |
| `load_sideways_rom` | 765-789 | 788-826 |
| `load_sideways_data` | 795-826 | 828-870 |
| `configure_slot_as_ram` / `configure_slot_as_empty` | 827-840 | 875-888 |
| `supports_slot_configuration() { return true; }` | 844 | 890 |
| `slot_type(slot)` | 849-862 | 893-908 |
| `slot_image_name(slot)` | 870-881 | 913-924 |
| `apply_motherboard_links` (S13 cache write) | 1053 | 1121 |

The 128K differs only in:

- Four extra Ram<16384> banks (SRAM W/X/Y/Z) at 186-189.
- `load_sideways_rom` / `load_sideways_data` route slots 0/1/14/15 to
  SRAM Y/Z (and the BASIC pair) per S13 instead of always to
  `basic_rom`.
- `slot_type` reports the SRAM banks as Ram.

Because the polarity of the S13 cache is inverted between the two
classes (`s13_is_south_` vs `s13_is_north_`), one half of each routing
table is also inverted. Any future change has to be made twice, both
inverted, with no compile-time check that they agree.

### 2. SidewaysService has a four-way dispatch on memory shape

`SidewaysService.hpp` carries three Has*-style concepts plus a
fallback to answer one question, "what is at slot N?":

- `HasAliasedSideways<T>` (45) - Model B's AliasedBankedMemory exposes
  `socket(idx)`.
- `HasConfigurableSideways<T>` (54) - Model B + ROM/RAM board's
  ConfigurableBankedMemory exposes `slot(idx)`.
- `HasMemorySlotKind<T>` (75) - new, used for the B+ family which
  exposes `slot_type(slot)` / `slot_image_name(slot)` directly on the
  hardware class.
- Fallback: read the topology and assume Ram if and only if the socket
  is fixed-RAM.

Every reader (`GetSlotStatus`, the live scanner, `ConfigureSlot`) has
to repeat the same four-way branch. The current state is brittle
enough that adding the new B+ branch in two places was a separate bug
fix. A new machine (Master 128 is on the roadmap) will need yet
another shape unless we unify first.

Branch sites:
- GetSlotStatus: 215, 222, 229
- ConfigureSlot reject path: 354, 363, 365
- ConfigureSlot apply path: 404, 407, 424, 427
- Scanner: 637, 641, 645

### 3. S13-aware routing is repeated five times per B+ class

For each of ModelBPlusHardware and ModelBPlus128KHardware, the same
"which device backs slot 0/1/14/15 given the S13 link?" logic is
re-derived in:

1. `load_sideways_rom`
2. `load_sideways_data`
3. `slot_type`
4. `slot_image_name`
5. `apply_motherboard_links` (which also rebinds the BankedMemory)

One slip and they disagree. Five copies x two classes = ten places
to keep in sync.

### 4. Image-name tracking is ad-hoc per device

- `ConfigurableSlot` carries `image_name` natively.
- `basic_rom` (a Rom<16384>) does not, so both B+ classes keep a
  parallel `basic_rom_image_name_` string.
- SRAM W/X/Y/Z (Ram<16384>) can't carry an image name at all. After
  `*SRLOAD` into SRAM Y, the live scanner can pick up the ROM header
  but `slot_image_name` returns `""`.

### 5. supports_slot_configuration() duplicates the topology

`Memory::supports_slot_configuration()` is a single machine-wide bool.
It coexists with the per-socket `SocketSpec::supports_rom/ram/empty`
flags in SlotTopology. They can disagree (e.g. IC71 on B+ has
`supports_ram = true` in the topology but is actually a soldered ROM
that cannot become RAM). The flag also forces `apply_sideways_configs`
to fall back to a stderr warning instead of using the topology.

Call sites for the flag:
- `ServerMain.hpp:1104, 1113` - `apply_sideways_configs`
- `ModelBHardware.hpp:613`, `ModelBPlusHardware.hpp:844`,
  `ModelBPlus128KHardware.hpp:890`, `ModelBRomRamBoardHardware.hpp:463`
- `AliasedBankedMemory.hpp:245`, `ConfigurableBankedMemory.hpp:202`
- Two test cases that just assert it's true.

## Goals of the refactor

1. One source of truth per question, per machine. No more "the
   topology says X but the device says Y".
2. SidewaysService talks to memory through one uniform API. New
   machine variants do not require new concept branches.
3. Adding the Master 128 (similar to B+ but with shadow modes,
   different sideways layout) is a single new Hardware class, not a
   sweep through SidewaysService and ServerMain.
4. Move the S13-aware routing to a single helper so the BankedMemory
   binding, the read-back, and the load all share one decision.

Non-goals: changing the gRPC wire format, changing what the user sees
in the New Machine dialog or the Memory sidebar, touching the Model B
or ROM/RAM board behaviour beyond what falls out of the unification.

## Refactor A - extract a UserSocketBank component

Pull the 10 ConfigurableSlot members, `user_slot_for(slot)`, and the
identical user-socket portion of every accessor into a single class
that both B+ variants own by composition.

Sketch:

```cpp
// src/core/include/beebium/devices/UserRomSocketBank.hpp
class UserRomSocketBank {
public:
    // Slot pair layout for B+ family: IC35..IC68 each present two
    // 16K halves; sockets are indexed 0..9 -> slots 2..11.
    static constexpr std::array<uint8_t, 10> slot_to_socket = {
        /* slot 2 -> */ 0, 1,  /* slot 4 -> */ 2, 3,
        /* slot 6 -> */ 4, 5,  /* slot 8 -> */ 6, 7,
        /* slot 10 -> */ 8, 9,
    };

    ConfigurableSlot* slot_for(uint8_t slot);
    const ConfigurableSlot* slot_for(uint8_t slot) const;

    // Returns true if the slot was a user socket and was handled.
    bool load_rom(uint8_t slot, std::span<const uint8_t> data,
                  std::string_view image_name);
    bool load_data(uint8_t slot, std::span<const uint8_t> data,
                   std::string_view image_name);
    bool configure_as_ram(uint8_t slot);
    bool configure_as_empty(uint8_t slot);

    SlotType slot_type(uint8_t slot) const;
    std::string_view slot_image_name(uint8_t slot) const;
    bool owns_slot(uint8_t slot) const;

    // Expose individual ConfigurableSlot& for use in make_bank<>().
    ConfigurableSlot& at(size_t socket_idx);
private:
    std::array<ConfigurableSlot, 10> slots_{ /* all Empty */ };
};
```

Each B+ class then becomes:

```cpp
class ModelBPlusHardware {
    UserRomSocketBank user_sockets_;
    Rom<16384> basic_rom;
    std::string basic_rom_image_name_;
    // ...

    void load_sideways_rom(uint8_t slot, ...) {
        if (user_sockets_.load_rom(slot, ..., image_name)) return;
        // Only IC71 BASIC pair left:
        load_basic_pair(slot, ..., image_name);
    }
};
```

Touch points:
- `src/core/include/beebium/devices/UserRomSocketBank.hpp` (new).
- `ModelBPlusHardware.hpp`: remove the 10 ConfigurableSlot members and
  `user_slot_for`; delegate.
- `ModelBPlus128KHardware.hpp`: same delegation.
- The SidewaysType `decltype(make_bank<...>(...))` packs in both
  classes - update to take `user_sockets_.at(n)`.
- Both `apply_motherboard_links` keep their own S13 logic for now
  (refactor C tightens that further).

Tests touched: none directly. `test_grpc_sideways.cpp` and
`test_disc_integration.cpp` reach the user sockets through the public
API, not the private members.

After A: 80% duplication between the two B+ classes drops to ~20%
(just basic_rom, SRAM, and S13-driven routing).

## Refactor B - one slot_info() accessor on every Memory

Add a single method to every Memory type:

```cpp
struct SlotInfo {
    SlotType type;                 // Empty / Rom / Ram
    bool populated;                // True if Rom or Ram, false if Empty
    std::string image_name;        // Empty string if none
    bool exists;                   // True iff the slot is part of the topology
};

SlotInfo slot_info(uint8_t slot) const;
```

(`std::string` rather than `string_view` because some implementations
will assemble it from multiple sources, e.g. "SRAM Y" + filename.)

Each Memory implements `slot_info` in the way that matches its layout:

- `AliasedBankedMemory::slot_info(slot)` reads `socket(slot_to_socket(slot))`.
- `ConfigurableBankedMemory::slot_info(slot)` reads `slot(slot)`.
- `ModelBPlusHardware::slot_info(slot)` consults the UserRomSocketBank
  first, then the BASIC pair under S13.
- `ModelBPlus128KHardware::slot_info(slot)` consults UserRomSocketBank
  first, then the SRAM/BASIC pairs under S13.

`SidewaysService.hpp`:

- Delete `HasAliasedSideways`, `HasConfigurableSideways`,
  `HasMemorySlotKind`.
- Add one concept `HasSlotInfo<T>` that requires
  `t.slot_info(uint8_t)`. (Or rely on duck typing if every Memory
  defines it - probably cleaner with a concept for diagnostics.)
- `GetSlotStatus` becomes one straight loop.
- The scanner's `is_ram` test becomes
  `mem.slot_info(probe_slot).type == SlotType::Ram`.
- ConfigureSlot reads `slot_info` for the current state and writes via
  the existing `configure_slot_as_ram` / `configure_slot_as_empty` /
  `load_sideways_rom` (or unify those later).

This is the bulk of the refactor's payoff: deleting branches from
SidewaysService rather than adding more.

Touch points:
- `src/service/include/beebium/service/SidewaysService.hpp` - delete
  three concepts, simplify ~10 dispatch sites.
- `src/core/include/beebium/devices/AliasedBankedMemory.hpp` - add
  `slot_info(slot)`.
- `src/core/include/beebium/devices/ConfigurableBankedMemory.hpp` -
  add `slot_info(slot)`.
- `src/core/include/beebium/ModelBHardware.hpp` - thin pass-through.
- `src/core/include/beebium/ModelBRomRamBoardHardware.hpp` - thin
  pass-through.
- `src/core/include/beebium/ModelBPlusHardware.hpp` - new method
  combining `slot_type` + `slot_image_name`.
- `src/core/include/beebium/ModelBPlus128KHardware.hpp` - same.
- Keep `slot_type` / `slot_image_name` as deprecated thin wrappers
  during the transition, or delete them in the same commit if all
  callers have moved.

Tests: existing service tests should pass unchanged. Add a focused
unit test per Memory type that hits `slot_info` for each slot in the
topology, exercising both default state and after configure/load.

## Refactor C - retire Memory::supports_slot_configuration()

The per-socket `SocketSpec::supports_rom/ram/empty` flags in
`SlotTopology` are the right source of truth. The machine-wide flag
adds nothing.

Steps:

1. Update `ServerMain.hpp:1100-1140` (`apply_sideways_configs`) to ask
   the topology, not the flag:

   ```cpp
   auto topo = Memory::slot_topology(motherboard_links);
   const auto* spec = topo.find_socket_for_slot(slot);
   if (!spec) {
       std::cerr << "Slot " << slot << " does not exist on this machine\n";
       continue;
   }
   if (marker == RAM_SLOT_MARKER) {
       if (!spec->supports_ram) {
           std::cerr << "Slot " << slot << " (" << spec->label
                     << ") cannot be configured as RAM\n";
           continue;
       }
       machine.state().memory.configure_slot_as_ram(slot);
       continue;
   }
   // ...etc
   ```

2. Delete `supports_slot_configuration()` from every Memory and every
   Hardware class. There are six call-site definitions plus the two
   call sites in ServerMain.

3. Audit `SocketSpec::supports_*` flags for every machine - fix IC71
   on both B+ variants to `supports_ram = false, supports_empty =
   false` (the BASIC ROM is soldered).

4. Update the two tests in `test_aliased_banked_memory.cpp:547` and
   `test_configurable_banked_memory.cpp:450` - delete or convert to
   topology assertions.

5. Update SidewaysService's ConfigureSlot to read the topology for
   permission checks (it already reads it for existence; just make
   permission checks use the same source).

After C: `SocketSpec` is the only place that says what a slot can
hold. The Hardware classes only implement the mechanism.

## S13 helper (small, can land in any of A/B/C)

The five-times-per-class S13 routing reduces to one helper:

```cpp
// Returns the device that backs the given sideways slot under the
// current S13 link state. Returns nullopt for slots owned by the
// UserRomSocketBank.
struct BasicPair { Rom<16384>* rom; std::string* image_name; };
struct SramPair  { Ram<16384>* y; Ram<16384>* z; };
struct BasicHalf { /* lo/hi pair */ };

std::variant<std::monostate, BasicHalf, SramHalf>
device_for_slot(uint8_t slot) const;
```

Each of `load_sideways_rom`, `load_sideways_data`, `slot_info`, and
`apply_motherboard_links` (when it rebinds the BankedMemory) calls
this helper and dispatches on the variant. Add a test case that
asserts the four routes are consistent for both S13 positions.

## Suggested ordering

A, then C, then B. Rationale:

- A is contained, deletes the most code, and gives the next two
  refactors fewer places to edit.
- C is the smallest behaviour change but touches a lot of grep-able
  surfaces; doing it on top of A means fewer edits per file.
- B is the largest surface change. Doing it last means it absorbs the
  simplified internals from A and C, so the SidewaysService diff
  shows pure subtraction.

Each step should leave the build green and all tests passing. Commit
frequently per [[feedback_commit_ratchet]].

## Test strategy

After each refactor step:

```bash
cmake --build /Users/rjs/Code/beebium/build
cd /Users/rjs/Code/beebium/build
ctest -R "(Sideways|sideways|ModelBPlus|grpc_sideways|disc_integration|aliased_banked|configurable_banked)" --output-on-failure -j 4
```

Per [[feedback_targeted_tests]] do not run the full suite until the
end. The targeted set above is what actually exercises the touched
code.

End-of-refactor: run the full ctest suite and do a manual GUI smoke:

- Plain B+ 64K and B+ 128K boot from disc with default presets.
- New Machine -> configure slot 2 as RAM -> Save -> Load image into
  it via `*SRLOAD` -> Memory sidebar shows the ROM header within
  ~1 second.
- New Machine -> configure slot 11 (IC68 hi, defaults to DFS on B+
  64K disc preset) -> verify it cannot be configured as RAM if we
  decided IC68 is solderable (or that it can if we left it free).

## Risks

- **The Memory template is in every translation unit that includes
  ServerMain.hpp.** Compile times will go up briefly while iterating;
  rebuild only the affected `beebium-model-b*` target.
- **Refactor C changes user-visible behaviour** for the edge case
  where someone passes `--sideways 0:ram` on a B+: today they get a
  no-op + stderr warning; after C they get a clean rejection from
  apply_sideways_configs. This is the intended fix but mention it in
  the commit message.
- **The B+ user-socket polarity inversion** (`s13_is_south_` vs
  `s13_is_north_`) is easy to flip by accident during A. Pick one
  convention (recommend `s13_is_south_` since that matches the
  Service Manual's default) and update the 128K class to match.
- **Live scanner regression.** The scanner currently has four
  `is_ram` branches at SidewaysService.hpp:632-645. Refactor B
  replaces all four with one call to `slot_info`. The existing
  integration test in `test_grpc_sideways.cpp` for "B+ 128K *SRLOAD
  triggers SlotHeaderChangedEvent" must continue to pass.

## Pointers

- Current source of truth for topology: `SlotTopology.hpp:30`
  (`SocketSpec`) and per-machine `slot_topology(MotherboardLinks)`
  static methods.
- ConfigurableSlot: `devices/ConfigurableSlot.hpp:39`. Already does
  what UserRomSocketBank needs internally.
- AliasedBankedMemory: `devices/AliasedBankedMemory.hpp:44`. Use as
  the reference for how to expose a uniform slot_info().
- The live header scanner: `SidewaysService.hpp:609-680`.
- The Memory sidebar's UI assumptions (what fields it expects from
  GetSlotStatus): `clients/macos/Beebium/Memory/MemorySidebarView.swift`.

## Out of scope (separate work)

- `ConnectDialog.friendlyModelName` should source the display name
  from `SystemClient.MachineIdentity.model_name` instead of a Swift
  switch statement.
- Audit `SocketSpec::supports_*` defaults for the Model B and ROM/RAM
  board - they may need the same tightening as IC71 on the B+.
- Master 128 sideways layout - the Master has its own quirks
  (cartridge slots, MOS+OS in different banks) and should be designed
  with this refactor's `slot_info` API in mind from day one.
