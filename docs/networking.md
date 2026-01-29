# Econet and AUN Networking Support

This document captures research and planning for implementing Econet and AUN (Acorn Universal Networking) support in Beebium.

## Overview

Econet is Acorn's local area networking system, introduced in 1981. It connects BBC Micros (and later Acorn machines) allowing shared access to file servers, printers, and inter-machine communication. The hardware is based on the Motorola MC68B54 Advanced Data Link Controller (ADLC).

AUN (Acorn Universal Networking) encapsulates Econet protocols over TCP/IP, originally developed for RISC OS but now used to bridge vintage Econet hardware with modern networks.

### Goals for Beebium

1. **Emulate the MC68B54 ADLC** at the hardware level
2. **Support AUN protocol** for network connectivity
3. **Enable connectivity with Pi Econet Bridge** for access to real Econet networks
4. **Support NFS/ANFS ROMs** for file server access

## Hardware Architecture

### Memory Map

| Address | Function | Access |
|---------|----------|--------|
| &FE18 | Station ID register | Read only |
| &FEA0 | Control Register 1 / Status Register 1 | Write / Read |
| &FEA1 | Control Registers 2,3 / Status Register 2 | Write / Read |
| &FEA2 | Transmit FIFO (Frame Continue) / Receive FIFO | Write / Read |
| &FEA3 | Transmit FIFO (Frame Terminate) / Receive FIFO | Write / Read |

The station ID at &FE18 is set by hardware links (S11) and returns a value 0-255. Reading this register also enables NMI from the ADLC.

### MC68B54 ADLC Registers

The ADLC has four control registers (CR1-CR4) and two status registers (SR1, SR2), plus transmit and receive FIFOs.

**Control Register 1 (CR1):** (RS1=0, RS0=0, R/W=0)
- Bit 0: Address Control (AC) - selects CR2 vs CR3/CR4 for writes to RS1=0,RS0=1
- Bit 1: Receiver Interrupt Enable (RIE) - 1=enable RX interrupts
- Bit 2: Transmitter Interrupt Enable (TIE) - 1=enable TX interrupts
- Bit 3: RDSR Mode - DMA mode for receiver, inhibits RDA-caused IRQ
- Bit 4: TDSR Mode - DMA mode for transmitter, inhibits TDRA-caused IRQ
- Bit 5: Rx Frame Discontinue - discards current frame, auto-resets when frame discarded
- Bit 6: Receiver Reset (RxRS) - 1=hold receiver in reset, must write 0 to release
- Bit 7: Transmitter Reset (TxRS) - 1=hold transmitter in reset, transmits marks (1s)

**Status Register 1 (SR1):** (RS1=0, RS0=0, R/W=1)
- Bit 0: Receiver Data Available (RDA) - mirrors SR2 bit 7 for convenience
- Bit 1: Status #2 Read Request (S2RQ) - OR of all SR2 stored conditions (except RDA)
- Bit 2: Loop Status - 1 when in "On-Loop" condition (Loop Mode only)
- Bit 3: Flag Detected (FD) - flag received (if FD Enable set), cleared by CLR Rx Status
- Bit 4: Clear To Send (CTS) - positive edge stored, cleared by CLR Tx Status
- Bit 5: Transmit Underrun (TxU) - TX ran out of data, frame aborted
- Bit 6: TDRA/Frame Complete - TX FIFO available OR frame complete (selectable)
- Bit 7: Interrupt Request (IRQ) - 1 when IRQ output is active (low)

**Status Register 2 (SR2):** (RS1=0, RS0=1, R/W=1)
- Bit 0: Address Present (AP) - address byte available in RX FIFO, cleared by reading data
- Bit 1: Frame Valid (FV) - frame complete with no error, cleared by CLR Rx Status
- Bit 2: Inactive Idle Received (Rx Idle) - 15+ consecutive 1s received
- Bit 3: Abort Received (RxABT) - 7+ consecutive 1s received in-frame
- Bit 4: FCS/Invalid Frame Error (ERR) - CRC error or short frame
- Bit 5: Data Carrier Detect (DCD) - positive edge stored, high DCD resets receiver
- Bit 6: Receiver Overrun (OVRN) - data lost due to full FIFO
- Bit 7: Receiver Data Available (RDA) - non-address, non-last data available

**Status Priority (Prioritized Status Mode):**
When PSE=1 in CR2, status bits are prioritized - higher priority bits suppress lower ones.
Priority order (highest to lowest): IRQ > TDRA/FC > TxU > CTS > FD > S2RQ > RDA.
Programming tip: Test lowest priority (most frequent) conditions first.

**Control Register 2 (CR2):** (RS1=0, RS0=1, R/W=0, AC=0)
- Bit 0: Prioritized Status Enable (PSE) - enables status bit suppression
- Bit 1: 2-Byte/1-Byte Transfer - controls when TDRA/RDA indicate availability
- Bit 2: Flag/Mark Idle Select - 1=flags, 0=mark idle during transmit idle
- Bit 3: Frame Complete/TDRA Select - selects meaning of SR1 bit 6
- Bit 4: Transmit Last Data - signals last byte, auto-clears
- Bit 5: Clear Receiver Status - clears stored RX status bits (except AP, RDA)
- Bit 6: Clear Transmitter Status - clears stored TX status bits (except TDRA)
- Bit 7: RTS Control - 1=RTS output low (active)

**Control Register 3 (CR3):** (RS1=0, RS0=1, R/W=0, AC=1)
- Bit 0: Logical Control Field Select (LCF)
- Bit 1: Extended Control Field Select (Cex)
- Bit 2: Auto Address Extend Mode (Aex)
- Bit 3: 01/11 Idle
- Bit 4: Flag Detect Status Enable (FDSE)
- Bit 5: Loop/Non-Loop Mode
- Bit 6: Go Active on Poll/Test (GAP/TST)
- Bit 7: Loop On-line Control/DTR (LOC/DTR)

**Control Register 4 (CR4):** (RS1=1, RS0=1, R/W=0, AC=1)
- Bit 0: Double Flag/Single Flag Interframe Control
- Bits 1-2: Word Length Select Transmit (5-8 bits)
- Bits 3-4: Word Length Select Receive (5-8 bits)
- Bit 5: Transmit Abort - transmits abort sequence
- Bit 6: Abort Extend - extends abort to 16 bits
- Bit 7: NRZI/NRZ Select

### FIFO Operation

The ADLC has 3-byte FIFOs for both transmit and receive. Data transfers between registers on both phases of the E clock.

**Transmit FIFO:**
- Write to "Frame Continue" address (&FEA2): sets frame boundary pointer
- Write to "Frame Terminate" address (&FEA3): resets frame boundary pointer
- When negative transition detected at third FIFO location, transmitter appends FCS and closing flag

**Receive FIFO:**
- Address Present (AP) bit indicates address byte available
- Frame Valid (FV) set when last byte enters last FIFO location
- Once FV is set, further data transfer to last location is blocked until status cleared

### Interrupt Handling

The ADLC generates NMI (Non-Maskable Interrupt) on the BBC Micro. The NMI is automatically enabled when the station ID register (&FE18) is read. This allows software to control when it's ready to handle network traffic.

## Econet Protocol

### Frame Format

Econet uses HDLC-like framing:

```
+------+----------+----------+----------+----------+---------+------+------+------+
| Flag | Dest Net | Dest Stn | Src Net  | Src Stn  | Control | Port | Data | CRC  |
+------+----------+----------+----------+----------+---------+------+------+------+
| 7E   | 1 byte   | 1 byte   | 1 byte   | 1 byte   | 1 byte  | 1 byte| var  | 2 bytes |
+------+----------+----------+----------+----------+---------+------+------+------+
```

- **Flag**: 0x7E marks frame boundaries
- **Destination/Source**: Network number (0 for local) + station number
- **Control byte**: Identifies frame type and sequence
- **Port**: Destination port number (determines protocol/service)
- **CRC**: 16-bit CRC-CCITT for error detection

### Four-Way Handshake

Econet uses a four-way handshake for reliable data transfer:

```
Sender                              Receiver
  |                                    |
  |------- Scout Frame --------------->|  (destination, port, control)
  |                                    |
  |<------ Scout Acknowledge ----------|  (confirms ready to receive)
  |                                    |
  |------- Data Frame(s) ------------->|  (actual payload)
  |                                    |
  |<------ Final Acknowledge ----------|  (confirms receipt)
  |                                    |
```

**Scout Frame**: Small frame announcing intent to transmit, containing destination address, port, and control byte.

**Scout Acknowledge**: Receiver confirms it exists and is ready.

**Data Frame(s)**: The actual data payload, potentially spanning multiple frames.

**Final Acknowledge**: Receiver confirms successful receipt.

### Standard Ports

| Port | Service |
|------|---------|
| &00 | Immediate operations |
| &90 | File server command |
| &91 | File server command reply |
| &92 | File server high-priority data |
| &93 | File server high-priority data reply |
| &94 | File server data |
| &95 | File server data reply |
| &99 | File server broadcast |
| &9C | Bridge protocol |
| &9D | Resource location |
| &D0 | SJ Research MDFS |
| &D1 | SJ Research Print Server |

### Immediate Operations

Port &00 is reserved for immediate operations - low-level commands that execute directly without NFS involvement:

| Code | Operation | Description |
|------|-----------|-------------|
| &81 | PEEK | Read memory from remote station |
| &82 | POKE | Write memory on remote station |
| &83 | JSR | Execute subroutine on remote station |
| &84 | User Procedure | Call OS routine on remote |
| &85 | OS Procedure | Reserved |
| &86 | Halt | Halt remote station |
| &87 | Continue | Resume halted station |
| &88 | Machine Type | Query machine type |

Stations can set a **protection mask** to restrict which immediate operations are allowed.

## AUN Protocol

AUN (Acorn Universal Networking) encapsulates Econet over UDP/IP. It was developed by Acorn for RISC OS machines but is now widely used for bridging.

### Key Differences from Native Econet

From the AUN Manager's Guide:
> "The transport protocol is User Datagram Protocol (UDP), enhanced by a proprietary handshake mechanism designed to support the semantics of Econet SWI calls. This is not a straightforward port of the four-way handshake mechanism used by native Econet, but is rather a **two-way handshake protocol** overlaid with a timeout and retransmission mechanism better suited to the characteristics of IP traffic."

AUN uses:
- **UDP** for transport (not TCP, which is stream-oriented and unsuited to Econet semantics)
- **IP** for network layer
- **ARP/RevARP** for address resolution
- **RIP** for routing information exchange

### Transport

- **Protocol**: UDP
- **Default Port**: 32768

### Packet Format

```
+------+------+------+------+--------+----------------+
| Type | Port | CB   | Pad  | Handle | Econet Payload |
+------+------+------+------+--------+----------------+
| 1    | 1    | 1    | 1    | 4      | variable       |
+------+------+------+------+--------+----------------+
```

- **Type**: Packet type (see below)
- **Port**: Econet port number
- **CB**: Control byte
- **Pad**: Padding byte (usually 0)
- **Handle**: 32-bit transaction handle for matching replies
- **Payload**: Econet data (without addressing, flags, CRC)

### AUN Packet Types

| Type | Name | Description |
|------|------|-------------|
| 1 | AUN_TYPE_BROADCAST | Broadcast packet |
| 2 | AUN_TYPE_UNICAST | Standard data packet |
| 3 | AUN_TYPE_ACK | Acknowledgement |
| 4 | AUN_TYPE_NACK | Negative acknowledgement |
| 5 | AUN_TYPE_IMMEDIATE | Immediate operation request |
| 6 | AUN_TYPE_IMM_REPLY | Immediate operation reply |

### AUN IP Address Format

AUN uses a Class A IP address format with netmask &FFFF0000:

```
1.network.net.station
```

| Field | Bytes | Description |
|-------|-------|-------------|
| site | 1 | Always 1 (reserved) |
| network | 1 | Logical network number (internal routing) |
| net | 1 | Econet net number |
| station | 1 | Econet station number |

**Examples:**
| Econet Address | AUN IP Address |
|----------------|----------------|
| 3.2 | 1.1.3.2 |
| 129.16 | 1.3.129.16 |

**Default (isolated network):** `1.0.128.station`

### Address Mapping (BeebEm Style)

BeebEm uses a simpler configuration file (`Econet.cfg`) with explicit mappings:
```
AUNMODE 1
AUNMAP 0.254 192.168.0.100
AUNMAP 0.253 192.168.0.101
```

This differs from Acorn's official AUN IP scheme and may be more practical for emulator-to-emulator and emulator-to-bridge communication.

## MOS Interface

### OSWORD Calls

**OSWORD &10 - Transmit**

Initiates a network transmission.

Control block at (XY):
```
Offset  Size  Description
0       1     Control byte
1       1     Port number
2       2     Destination station (network.station)
4       4     Buffer address
8       4     Buffer start offset
12      4     Buffer end offset
```

Returns status in control block byte 0:
- &00: Transmitted OK
- &40: Line jammed
- &41: Net error
- &42: Not listening
- &43: No clock
- &44: Transmit not started (bad control block)

**OSWORD &11 - Receive**

Opens a receive block to accept incoming data.

Control block at (XY):
```
Offset  Size  Description
0       1     Flag byte (0 to open, &7F to poll)
1       1     Port number (&00 = any)
2       2     Station number (&0000 = any)
4       4     Buffer address
8       4     Buffer start offset
12      4     Buffer end offset
```

Flag byte returns:
- &00: Receive block open, no data yet
- &FF: Data received successfully
- Other: Error codes

**OSWORD &12 - Read Arguments**

Reads information about a completed receive.

**OSWORD &13 - Read Station Info**

Returns local station number.

**OSWORD &14 - Read FS Info / Notify**

File server information and notification operations.

### OSBYTE Calls

| Call | Function |
|------|----------|
| &32 | Poll transmit |
| &33 | Poll receive |
| &34 | Delete receive block |
| &35 | Sever remote connection |
| &C9 | Read/write Econet OS call interception flag |
| &CE | Read/write Econet read character flag |
| &CF | Read/write Econet write character flag |
| &D0 | Read/write Econet OS RDCH/WRCH flag |

## BeebEm Implementation Analysis

BeebEm's Econet implementation (~2600 lines in `Econet.cpp`) provides a comprehensive reference.

### Key Components

**ADLC State Structure:**
```cpp
struct MC6854 {
    unsigned char control1, control2, control3, control4;
    unsigned char txfifo[3], rxfifo[3];
    unsigned char txfptr, rxfptr;  // FIFO pointers
    unsigned char txftl;           // TX FIFO fill level
    unsigned char rxffc;           // RX FIFO fill count
    unsigned char rxap;            // RX address present flags
    unsigned char status1, status2;
    int sr2pse;                    // SR2 prioritised status enable
    bool cts, idle;
};
```

**Four-Way Handshake State Machine:**
```cpp
enum class FourWayStage {
    Idle,
    ScoutSent,
    ScoutAckReceived,
    DataSent,
    WaitForIdle,
    ScoutReceived,
    ScoutAckSent,
    DataReceived
};
```

**AUN Header:**
```cpp
struct AUNHeader {
    AUNType type;
    unsigned char port;
    unsigned char cb;       // Control byte
    unsigned char pad;
    uint32_t handle;        // Transaction handle
};
```

### BeebEm Operating Modes

1. **AUNMODE 0**: Native Econet mode (local machine-to-machine only)
2. **AUNMODE 1**: AUN mode (UDP-based networking)

### Key Implementation Details

- Uses UDP sockets on port 32768
- Maintains separate state for ADLC registers and four-way handshake
- Handles flag fill (continuous flag transmission) while processing
- Implements immediate operations (PEEK, POKE, JSR, etc.)
- Supports both station-based and AUN-mapped addressing

## Pi Econet Bridge

The Pi Econet Bridge is a Raspberry Pi-based gateway between real Econet networks and AUN/IP networks.

### Hardware

- Raspberry Pi with custom Econet interface board
- Directly connects to Econet cable
- Kernel module handles timing-critical Econet operations

### Network Architecture

```
BBC Micro <--Econet--> Pi Bridge <--AUN/UDP--> Beebium
                           |
                           +--AUN/UDP--> RISC OS
                           |
                           +--AUN/UDP--> Other bridges
```

### Configuration

The Pi Econet Bridge uses configuration for:
- Local station number
- Network number mappings
- IP address to station mappings
- File server locations

### Compatibility Notes

For Beebium to work with Pi Econet Bridge:
1. Must implement AUN protocol correctly
2. Must handle bridge-specific packet types
3. May need to handle network number translation
4. Should support dynamic station discovery

## NFS/ANFS ROMs

To use Econet file servers, BBC Micros need appropriate network filing system ROMs.

### ROM Versions

| ROM | Machine | Notes |
|-----|---------|-------|
| NFS 3.34 | Model B | Original network filing system |
| NFS 3.60 | Model B | Later version, in DNFS ROM |
| ANFS 4.18 | Master 128 | Advanced NFS for Master series |
| ANFS 4.25 | Master 128 | Later ANFS version |

### DNFS ROM

The DNFS (Disc and Network Filing System) ROM combines DFS 1.20 and NFS 3.60 into a single ROM. From the DNFS manual:

- **Auto-detection:** DNFS checks for Econet and/or disc hardware at startup
- **Priority:** If both are present, DFS takes priority by default
- **Selection:** Keyboard switch 1 (link S1) overrides to select NFS at boot
- **Commands:** Use `*NET` or `*DISC` to switch filing systems at runtime

**NFS 3.60 improvements over 3.34:**
- Multi-column catalogue display
- Password masking at logon
- Control characters allowed in printer protocols (for graphics dumps)
- Econet runs as IRQ task (foreground activities don't block network)
- No "privileged" station numbers - all stations protected against immediate operations

### ROM Interaction

NFS/ANFS ROMs:
- Hook into MOS vectors for filing system operations
- Use OSWORD &10-&14 for network operations
- Provide *NET, *I AM, *BYE, *SDISC commands
- Implement file server protocol over Econet

Note: NFS can coexist with DFS/ADFS - users select filing system with *DISC, *NET, etc.

## Implementation Plan for Beebium

### Phase 1: ADLC Hardware Emulation

1. **Create `Adlc68B54` class** implementing the peripheral interface
   - Four control registers with correct bit semantics
   - Two status registers with proper priority encoding
   - 3-byte transmit and receive FIFOs
   - NMI generation logic

2. **Conditional hardware presence** (like disc controllers)
   - `--station N` enables Econet hardware AND sets station number
   - Without `--station`, no ADLC mapped - machine has no Econet fitted
   - DNFS ROM auto-detects and falls back to DFS-only mode

3. **Memory mapping** (when enabled)
   - Map ADLC to &FEA0-&FEA3
   - Map station ID register to &FE18 (returns configured station number)
   - Reading &FE18 enables NMI from ADLC

4. **Integration with Machine**
   - Add to peripheral tick loop (when present)
   - Handle NMI generation

### Phase 2: Econet Protocol Layer

1. **Frame handling**
   - Build frames from ADLC FIFO writes
   - Parse incoming frames into ADLC FIFO
   - CRC generation and validation

2. **Four-way handshake state machine**
   - Track conversation state
   - Handle timeouts and retries
   - Support immediate operations

3. **Local testing**
   - Implement loopback mode
   - Test with NFS ROM

### Phase 3: AUN Network Layer

1. **UDP transport**
   - Socket management on port 32768 (configurable)
   - AUN packet encoding/decoding
   - Transaction handle tracking

2. **Local subnet discovery** (broadcast-based)
   - On startup, broadcast announcement: "station N at IP:port"
   - Listen for peer announcements, build dynamic peer table
   - Periodic re-announcements (handle stations joining/leaving)
   - No configuration needed for same-subnet peers

3. **Explicit address mapping** (for cross-subnet / bridge)
   - `--aun-map <net.stn>=<ip:port>` for explicit mappings
   - Static mappings take precedence over discovered peers

4. **Pi Econet Bridge compatibility**
   - `--aun-bridge <ip:port>` for bridge connectivity
   - Test with actual bridge hardware
   - Handle bridge-specific behaviors
   - Bridge provides access to real Econet stations

### Phase 4: Configuration and Integration

1. **Command-line options**
   - `--station <n>` - Enable Econet hardware and set station number (no flag = no Econet)
   - `--aun-map <net.stn>=<ip:port>` - Explicit station-to-IP mapping (repeatable)
   - `--aun-port <port>` - Local UDP port (default 32768)
   - `--aun-bridge <ip:port>` - Pi Econet Bridge address (optional)

2. **Frontend integration**
   - Network status display
   - Configuration UI

3. **ROM management**
   - Include or document NFS/ANFS ROM acquisition
   - ROM slot configuration for network ROMs

## Open Questions

1. **Timing accuracy**: How cycle-accurate does the ADLC emulation need to be? BeebEm appears to use a polling approach rather than strict cycle timing.

2. **Clock detection**: Econet requires a clock signal. How should we handle the "no clock" error condition when not connected to a network? (Possibly: always report clock present when `--station` is used, since AUN doesn't have a physical clock.)

3. **Multi-network support**: Should we support multiple Econet network numbers for complex bridged setups? (Initial implementation: single network 0, expand later if needed.)

4. **ROM licensing**: What is the legal status of distributing NFS/ANFS ROMs?

5. **Broadcast announcement format**: What packet format for local discovery? Could reuse AUN broadcast type, or define a simple Beebium-specific announcement.

## Design Considerations

### Station Configuration (Avoiding BeebEm's Sequential Model)

BeebEm uses a sequential consumption model where each new instance takes the next entry from `Econet.cfg`. This creates launch-order dependencies and makes it awkward to restart a specific instance.

**Adopted approach for Beebium:**

`--station N` both enables Econet hardware AND sets station number:
```
beebium --station 254    # File server, Econet enabled
beebium --station 1      # Workstation, finds server via broadcast
beebium                  # No Econet hardware fitted
```

This mirrors physical hardware - you either have the Econet interface fitted or you don't. The DNFS ROM auto-detects hardware presence and behaves accordingly.

**Local discovery via broadcast:**
Stations on the same subnet discover each other automatically - no configuration needed for the common case. Cross-subnet or bridge connectivity uses explicit `--aun-map` or `--aun-bridge`.

**Future enhancements** (not initial implementation):
- Named profiles in config file for convenience
- Per-window configuration for multi-window setups

### Usage Examples

**Simple local network (same subnet, automatic discovery):**
```bash
# Terminal 1: File server
beebium --station 254

# Terminal 2: Workstation - finds server automatically via broadcast
beebium --station 1

# On the workstation, log in to the file server:
# *I AM 254 SYST
```

**Mixed network with Pi Econet Bridge:**
```bash
# Emulator connects to bridge, can talk to real BBC Micros on Econet segment
beebium --station 1 --aun-bridge 192.168.1.100
```

**Cross-subnet with explicit mapping:**
```bash
# Workstation on different subnet, explicit server address
beebium --station 1 --aun-map 254=192.168.2.50:32768
```

**Complex network (local emulators + bridge to real hardware):**
```bash
# File server (local emulator)
beebium --station 254

# Workstation (local, auto-discovers server, also connects to bridge)
beebium --station 1 --aun-bridge 192.168.1.100

# Real BBC Micros on Econet segment are accessible via the bridge
# e.g., station 42 on real Econet can be reached through the bridge
```

**No Econet (DFS only):**
```bash
# Without --station, no Econet hardware is fitted
beebium --drive0 games.ssd
```

## References

### Documentation (OCR'd text available in docs/manuals_text/)

- **MC68B54 ADLC Datasheet** (Motorola) - `docs/manuals_text/MC68B54_Datasheet/full_text.md`
  - Complete register specifications, timing diagrams, programming considerations
  - Key insight: Status priority - test lowest priority conditions first (most frequent)
  - Stored vs Present status: DCD, CTS, Rx Abort, Rx Idle are OR of stored + present conditions
  - FIFO: 3-byte TX and RX FIFOs with pointer-based frame boundary tracking

- **AUN Manager's Guide** (Acorn, 1992) - `docs/manuals_text/AUN_Managers_Guide/full_text.md`
  - AUN uses UDP + proprietary two-way handshake (not Econet's four-way)
  - IP address format: `1.network.net.station` (Class A, netmask &FFFF0000)
  - Uses RIP for routing, RevARP for client address discovery
  - Default isolated network address: `1.0.128.station`

- **DNFS Manual** (Acorn, 1984) - `docs/manuals_text/DNFS_Manual/full_text.md`
  - DNFS ROM contains both DFS 1.20 and NFS 3.60
  - Keyboard switch 1 selects DFS vs NFS at boot
  - Auto-detects Econet/DFS hardware presence

- Econet Advanced User Guide (Acorn, 1988)
- BBC Micro Advanced User Guide, Chapter 25

### Code References

- BeebEm: `/Users/rjs/Code/beebem-mac/Src/Econet.cpp`
- Pi Econet Bridge: https://github.com/cr12925/PiEconetBridge

### Online Resources

- J.G. Harston's Econet pages: http://mdfs.net/Docs/Comp/Acorn/Econet/
- BeebWiki Econet documentation
- StarDot forums (retro computing community)
