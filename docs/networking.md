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
- Bit 0: Address Present (AP) - frame boundary indicator; an address octet is available in the Rx FIFO. In Extended Addressing Mode, AP continues to indicate addresses until the address field is complete. Cleared by reading data or by Rx Reset.
- Bit 1: Frame Valid (FV) - frame complete with no error. Set when the last data byte of a frame is transferred into the last FIFO location. Once FV is set, the ADLC stops further data transfer into the last FIFO location (preventing mixing of two frames). Cleared by CLR Rx Status or Rx Reset.
- Bit 2: Inactive Idle Received (Rx Idle) - 15+ consecutive 1s received. Stored in status register and causes interrupt. The status bit is the logical OR of the receiver idling detector (which continues to reflect idling until a "0" is received) and the stored inactive idle condition. Cleared by CLR Rx Status.
- Bit 3: Abort Received (RxABT) - 7+ consecutive 1s received. Has no meaning under out-of-frame conditions; no interrupt or storing occurs unless a flag has been detected prior to the abort. An in-frame abort is stored and causes IRQ. The status bit is the logical OR of the stored condition and the Rx abort detect logic. Cleared by CLR Rx Status. The stored abort condition is also cleared by Rx Reset.
- Bit 4: FCS/Invalid Frame Error (ERR) - CRC error or short frame (frame does not have complete Address and Control fields). When ERR is set instead of FV, other functions (frame boundary indication, control function) are exactly the same as for FV. Cleared by CLR Rx Status.
- Bit 5: Data Carrier Detect (DCD) - a positive transition on the DCD input is stored in the status register and causes IRQ. Cleared by CLR Rx Status or Rx Reset. The status bit is the logical OR of the stored condition and the present DCD input state. Note: high DCD resets the receiver section.
- Bit 6: Receiver Overrun (OVRN) - receiver data transferred into Rx FIFO when it is full, resulting in data loss. Cleared by CLR Rx Status or Rx Reset. Continued overrunning only destroys data in the first FIFO register.
- Bit 7: Receiver Data Available (RDA) - receiver data can be read from the Rx FIFO. In prioritised status mode (PSE=1), indicates non-address and non-last data available. In 1-Byte Transfer Mode, RDA high means the last register of the FIFO causes RDA to be high. In 2-Byte Transfer Mode, RDA high indicates the last two registers are full. RDA is reset automatically when data is not available.

**Status Priority (Prioritized Status Mode):**
When PSE=1 in CR2, status bits are prioritized - higher priority bits suppress lower ones.
Priority order (highest to lowest): IRQ > TDRA/FC > TxU > CTS > FD > S2RQ > RDA.
Programming tip: Test lowest priority (most frequent) conditions first.

**Stored vs Present Status:**
Certain SR2 status bits represent the logical OR of a *stored* (latched) condition and the *present* (live) pin/input state:

| Bit | Stored condition | Present condition |
|-----|-----------------|-------------------|
| DCD (b5) | Positive edge latched | Current DCD input level |
| RxABT (b3) | In-frame abort latched | Rx abort detect logic output |
| Rx Idle (b2) | Inactive idle latched | Receiver idling detector output |

Similarly in SR1: CTS (b4) latches a positive edge on CTS, while the present CTS input can still assert. Clearing the stored condition (via Clear Rx Status or Clear Tx Status) reveals the present condition — if the input is still asserted, the status bit remains set. This means software must handle the case where clearing a status bit doesn't actually clear it because the underlying condition persists.

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

### Programming Considerations (from Motorola Datasheet)

The MC68B54 datasheet includes several programming notes that are important for correct emulation:

1. **Status priority testing**: When prioritised status mode (PSE) is used, test the lowest priority conditions first, as these are the most frequently occurring and most likely to exist when the processor is interrupted.

2. **Stored vs Present status clearing**: In prioritised mode, a status condition must be read before it can be cleared. Clearing a higher-priority condition might result in a new IRQ from a lower-priority condition whose status was previously suppressed. This guarantees that status conditions are never inadvertently cleared without software having seen them.

3. **Rx FIFO clearing**: An Rx Reset clears all three Rx FIFO bytes. However, the FIFO may contain data from two different frames when an abort or DCD failure occurs mid-frame. Data from a previously closed frame (one whose closing flag has been received) will not be destroyed.

4. **Servicing Rx FIFO in 2-Byte Mode**: The procedure for reading the last bytes of data is the same regardless of whether the frame contains an even or odd number of bytes. Continue to read 2 bytes until an end-of-frame status (FV or ERR) occurs. When this occurs, indicating the last byte has been read or is ready to be read, switch temporarily to 1-byte mode with non-prioritised status (control register 2) to check whether a 1-byte read is indicated.

5. **Frame Complete Status and RTS Release**: In many cases (particularly with modems), a delay is required for releasing RTS after frame completion. An 8-bit or 16-bit delay can be added to the ADLC RTS output at the end of a transmission. After frame complete status goes high, write "1" into the Abt control bit (and Abt Extend bit if a 16-bit delay is required). After the Abt control bit is set, write "0" into the RTS control bit. The transmitter will transmit eight or sixteen "1"s and the RTS output will then go high (inactive).

6. **E clock frequency constraints**: (a) When performing a write followed by a read on successive E pulses at a high frequency, time must be allowed for status changes to occur. If E is a static part (no clock), successive write/read E pulses should be at least 500ns apart. (b) The E frequency should be high enough to move data through the FIFOs to service the peripheral requirements. The period between successive E pulses should be **less than** the period of RxC or TxC in order to maintain synchronisation between the data bus and the peripherals. This confirms the E clock must be faster than the serial clock.

7. **Clear-to-Send (CTS) real-time inhibit**: When CTS input is high, it provides a real-time inhibit to the TDRA status bit and its associated interrupt. All other status bits remain operational. Since CTS inhibits TDRA, CTS also inhibits the TDSR DMA request. The CTS input being high does not affect any other part of the transmitter — information in the Tx FIFO and Tx Shift Register will continue to be transmitted as long as the Tx CLK is running.

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

BeebEm's Econet implementation (~2720 lines in `Econet.cpp`) provides the most comprehensive reference. The analysis below is based on the BeebEm Windows codebase (the most recently maintained version). The Mac port is structurally identical but has `#ifdef __APPLE__` guards for platform differences (socket types, memory alignment).

### Data Structures

**ADLC State:**
```cpp
struct MC6854 {
    unsigned char Control1, Control2, Control3, Control4;
    unsigned char TxFifo[3], RxFifo[3];
    unsigned char TxFifoPtr;       // Next free byte in TX FIFO (0-3)
    unsigned char RxFifoPtr;       // Next free byte in RX FIFO (0-3)
    unsigned char TxFifoTxLast;    // Bitmask: which FIFO slots are "last byte of frame"
    unsigned char RxFifoFCFlags;   // Bitmask: which RX slots are "frame complete"
    unsigned char RxFifoAPFlags;   // Bitmask: which RX slots are "address present"
    unsigned char Status1, Status2;
    int PriorityStatus;            // PSE level (0=inactive, 1-4=priority tiers)
    bool CTS, Idle;
};
```

**Four-Way Handshake State Machine (10 states):**
```cpp
enum class FourWayStage {
    Idle,                // No transaction
    ScoutSent,           // TX: scout queued, waiting for timeout to fake ack
    ScoutAckReceived,    // TX: fake ack given to Beeb, awaiting data response
    DataSent,            // TX: data sent to network, waiting for remote ACK
    WaitForIdle,         // Transaction complete, buffers draining
    ScoutReceived,       // RX: scout from network, awaiting Beeb's ack
    ScoutAckSent,        // RX: ack sent, waiting for timeout to deliver data
    DataReceived,        // RX: data delivered to Beeb, awaiting Beeb's final ack
    ImmediateSent,       // Immediate op sent, waiting for reply
    ImmediateReceived    // Immediate reply received, awaiting Beeb's response
};
```

**AUN Header (8 bytes on wire):**
```cpp
struct AUNHeaderType {
    AUNType Type;           // 1=Broadcast, 2=Unicast, 3=Ack, 4=NAck, 5=Immediate, 6=ImmReply
    unsigned char Port;     // Econet port number
    unsigned char CtrlByte; // Control byte (bit 7 cleared for AUN)
    unsigned char Pad;      // Retransmission count (usually 0)
    uint32_t Handle;        // 4-byte sequence number (little-endian)
};
```

**Packet Buffers:**
```
Non-AUN: Beeb -> ADLC.TxFifo -> BeebTx -> sendto()
         recvfrom() -> BeebRx -> ADLC.RxFifo -> Beeb

AUN:     Beeb -> ADLC.TxFifo -> BeebTx -> EconetTx -> sendto()
         recvfrom() -> EconetRx -> BeebRx -> ADLC.RxFifo -> Beeb
```

### ADLC Register Access

**CPU Write (`EconetWrite`):**
- Register 0: Always CR1
- Register 1 with CR1b0=0: CR2
- Register 1 with CR1b0=1: CR3
- Register 3 with CR1b0=1: CR4
- Register 2 or (Register 3 with CR1b0=0): TX data
  - Data shifts through FIFO: `TxFifo[2]=TxFifo[1]; TxFifo[1]=TxFifo[0]; TxFifo[0]=Value`
  - `TxFifoPtr++; TxFifoTxLast<<=1`
  - Register 3 automatically sets CR2b4 (TX_LAST_DATA)
  - All blocked if TX reset (CR1b7) is set

**CPU Read (`EconetRead`):**
- Register 0: SR1 (no side effects)
- Register 1: SR2 (no side effects)
- Register 2 or 3: RX FIFO pop: `Value = RxFifo[--RxFifoPtr]`
  - Sets `EconetStateChanged` to trigger immediate poll
  - Returns 0 if RX reset or FIFO empty

### Polling Architecture

`EconetPoll()` is called from the main CPU loop after every instruction. It's a gate:

```cpp
bool EconetPoll() {
    if (EconetStateChanged || EconetTrigger <= TotalCycles) {
        EconetStateChanged = false;
        if (Socket != INVALID_SOCKET) return EconetPollReal();
    }
    return false;
}
```

`EconetPollReal()` (~680 lines) does three things on each call:
1. **Process control register actions** (auto-clearing bits, resets)
2. **Trickle data** between FIFOs and packet buffers (only when timer fires)
3. **Recompute all status bits** and determine if interrupt needed

Returns `true` to request NMI.

### Control Register Processing (in `EconetPollReal`)

Certain control bits are "write-once, auto-clear" and are processed each poll:

| Bit | Auto-clear action |
|-----|-------------------|
| CR1b5 (RX Frame Discontinue) | Clears RX buffers, resets to Idle, then clears itself |
| CR2b4 (TX Last Data) | Sets `TxFifoTxLast |= 1`, then clears itself |
| CR2b5 (Clear RX Status) | Clears SR1b1,b3 and SR2b1,b2,b3,b4,b5,b6; advances PSE; then clears itself |
| CR2b6 (Clear TX Status) | Clears SR1b4,b5,b6; re-sets CTS if line still high; then clears itself |
| CR4b5 (TX Abort) | Clears TX FIFO and buffers, resets to Idle, then clears itself |

On CR1b6 (RX Reset): additionally clears all RX buffers and FIFO.
On CR1b7 (TX Reset): additionally clears all TX buffers and FIFO.

### TX Data Flow

Every `TimeBetweenBytes` cycles (default 128, ~64us), one byte transfers from FIFO to `BeebTx`:

```
1. Pull byte: BeebTx.Buffer[Pointer] = TxFifo[--TxFifoPtr]
2. If TxFifoTxLast bit set for that slot: this is last byte of frame
3. On last byte: call EconetSendPacket() to transmit entire assembled frame
```

### RX Data Flow

Every `TimeBetweenBytes` cycles, one byte transfers from `BeebRx` into FIFO:

```
1. Shift FIFO: RxFifo[2]=RxFifo[1]; RxFifo[1]=RxFifo[0]; RxFifo[0]=BeebRx.Buffer[Pointer]
2. RxFifoPtr++; shift FC and AP flags left
3. First byte (Pointer==0): set AP flag
4. Last byte (Pointer >= BytesInBuffer): set FC flag, reset buffer
```

When FIFO is empty AND no Frame Valid flag pending, `EconetReceivePacket()` attempts non-blocking UDP read via `select()` with zero timeout.

### Status Register Derivation

Status bits are fully recomputed every poll cycle:

**SR1:**
| Bit | Derivation |
|-----|------------|
| b0 RDA | `RxFifoPtr > 0` (1-byte mode) or `> 1` (2-byte mode); mirrored to SR2b7 |
| b1 S2RQ | Set when new bits appear in SR2 (excluding RDA); cleared when cause bits clear |
| b2 Loop | Always 0 (unsupported) |
| b3 FD | Follows `FlagFillActive` |
| b4 CTS | Set when `!Socket || !(CR2b7 RTS)`; latched until CPU clears |
| b5 TxU | Set on FIFO overflow (TxFifoPtr > 4) |
| b6 TDRA/FC | TDRA mode: space in FIFO AND CTS low AND DCD low. FC mode: TxFifoPtr == 0 |
| b7 IRQ | Set when interrupt causes detected; cleared when all causes resolved |

**SR2:**
| Bit | Derivation |
|-----|------------|
| b0 AP | `RxFifoAPFlags` bit set for current top-of-FIFO slot |
| b1 FV | `RxFifoFCFlags` bit set; only cleared by Clear RX Status or RX Reset |
| b2 Idle | `Idle && !FlagFillActive` |
| b3 RxAbort | Not used (always 0 - no abort simulation) |
| b4 FCS Error | Not used (always 0 - UDP has own checksums) |
| b5 DCD | `Socket == INVALID_SOCKET` (no clock = no socket) |
| b6 Overrun | Set on FIFO overflow (RxFifoPtr > 4) |
| b7 RDA | Copy of SR1b0 |

### Prioritised Status Enable (PSE)

When CR2b0 is set, SR2 RX bits are filtered to show only the highest-priority condition:

```
Priority 1 (highest): FV, Abort, FCS Error, DCD, Overrun → suppresses AP, Idle, RDA
Priority 2: Idle → suppresses AP, RDA
Priority 3: AP → suppresses RDA
Priority 4 (lowest): RDA → suppresses FV
```

Each Clear RX Status advances to the next priority level. The BBC typically doesn't use PSE (the NFS ROM handles all bits directly).

### Interrupt Generation

Edge-triggered: interrupts fire on 0→1 transitions of status bits:

```
1. Save previous SR1, SR2
2. Recompute all status bits
3. For SR2: new_bits = (SR2 ^ PrevSR2) & SR2 & ~RDA
   - If RIE enabled and new_bits: set S2RQ, accumulate cause
4. For SR1: new_bits = (SR1 ^ PrevSR1) & SR1 & ~IRQ
   - Mask by RIE (for RDA, S2RQ, FD) and TIE (for CTS, TxU, TDRA)
   - If new_bits: set IRQ, return true (trigger NMI)
5. For cleared bits: remove from cause; if all causes gone, clear IRQ
```

### NMI Enable/Disable (INTON/INTOFF)

The ADLC NMI is gated by a flip-flop (IC97) controlled by address bus decoding:

- **INTOFF** (NMI disabled): Reading &FE18-&FE1F (station ID register)
  - Same read also returns the station number
  - On Model B: `(Address & ~3) == 0xFE18`
  - On Master: `(Address & ~3) == 0xFE38`
- **INTON** (NMI enabled): Reading &FE20-&FE27 (Video ULA range)
  - On Model B: `(Address & ~3) == 0xFE20`
  - On Master: `(Address & ~3) == 0xFE3C`

**Critical detail:** When INTON fires and there's a pending interrupt (IRQ flag already set in SR1), NMI is asserted immediately. This allows software to read the station number with interrupts disabled, configure the ADLC, then enable NMI when ready.

### AUN Four-Way Handshake Simulation

AUN uses a two-way handshake (data + ack) but the Beeb's NFS ROM expects a four-way (scout + scout-ack + data + final-ack). BeebEm bridges this gap by *faking* the scout phase locally:

**Sending a unicast:**
```
1. Beeb writes scout → EconetSendPacket() with state=Idle
   - Does NOT send anything on network
   - State → ScoutSent, arms timeout (5000 cycles / ~2.5ms)
   - Saves scout header in EconetTx for later

2. Timeout fires → EconetReceivePacket() fakes scout ack
   - Generates 4-byte ack in BeebRx (src = original destination)
   - State → ScoutAckReceived

3. Beeb writes data frame → EconetSendPacket() with state=ScoutAckReceived
   - NOW sends AUN Unicast packet via UDP (header + payload)
   - State → DataSent

4. Remote sends AUN Ack → EconetReceivePacket()
   - Generates 4-byte final ack in BeebRx
   - State → WaitForIdle → Idle (when buffers drain)
```

**Receiving a unicast:**
```
1. AUN Unicast packet arrives → EconetReceivePacket() with state=Idle
   - Constructs scout (header only, or header + 4/8 bytes) in BeebRx
   - State → ScoutReceived
   - Stores full AUN packet in EconetRx for later

2. Beeb sends scout ack → EconetSendPacket() with state=ScoutReceived
   - Does NOT send anything on network
   - State → ScoutAckSent, arms timeout

3. Timeout fires → EconetReceivePacket() delivers cached data
   - Copies remaining payload from EconetRx into BeebRx
   - State → DataReceived

4. Beeb sends final ack → EconetSendPacket() with state=DataReceived
   - Sends AUN Ack packet to remote
   - State → WaitForIdle → Idle
```

**Broadcasts:** Single packet, no handshake. State: Idle → WaitForIdle → Idle.

**Immediate ops:** Single exchange (command + reply). State: Idle → ImmediateSent → WaitForIdle → Idle.

### Scout Payload Sizes

The scout/data split depends on the control byte (with bit 7 masked):

| Control Byte | Scout carries | Data carries |
|-------------|---------------|--------------|
| 0x02 (0x82 & 0x7f) | 8 bytes | remaining from offset 8 |
| 0x03-0x05 (0x83-0x85 & 0x7f) | 4 bytes | remaining from offset 4 |
| Other | 0 bytes (header only) | all payload |

### Pseudo Flag Fill

Flag fill prevents collisions on real Econet. BeebEm approximates it:

- **Set** when: we send a packet (peer assumed busy), or see traffic for another station
- **Cleared** when: we receive a packet addressed to us, or timeout expires, or handshake completes
- **Timeout**: 500,000 cycles (~250ms), configurable via `FLAGFILLTIMEOUT`
- **Effect**: SR1b3 (Flag Detected) follows `FlagFillActive`

### Idle Detection

`ADLC.Idle = true` when all of:
- RX not reset
- FIFO empty (`RxFifoPtr == 0`)
- No Frame Valid flag set
- No incoming data waiting (`BeebRx.BytesInBuffer == 0`)

### Timeouts

| Timer | Default | Purpose |
|-------|---------|---------|
| TimeBetweenBytes | 128 cycles (~64us) | Byte trickle rate between FIFO and buffers |
| EconetScoutAckTimeout | 5,000 cycles (~2.5ms) | Delay before faking scout ack |
| FourWayStageTimeout | 500,000 cycles (~250ms) | Watchdog: force-reset hung transactions |
| EconetFlagFillTimeout | 500,000 cycles (~250ms) | Assume peer finished processing |

### Timing Rationale

Econet clock: up to 250kHz. At 250kHz, one byte (8 bits + bit-stuffing overhead) takes ~40us, or ~80 CPU cycles at 2MHz. BeebEm uses 128 cycles (~64us) as a compromise - slightly slower than hardware but gives software time to keep up. The comment notes that 64 cycles was "a bit fast for netmon prog to keep up".

### Operating Modes

1. **AUNMODE 0**: Raw Econet frames over UDP. No four-way handshake simulation. Direct packet passthrough.
2. **AUNMODE 1**: AUN protocol. Four-way handshake faked locally. AUN header added/stripped.

### Simplifications vs Real Hardware

| Aspect | Real ADLC | BeebEm |
|--------|-----------|--------|
| Collision detection | CTS reflects bus contention | CTS = !(Socket && RTS) |
| CRC/FCS | 16-bit CRC-CCITT generated and checked | Not simulated (UDP checksums suffice) |
| Flag bytes (0x7E) | Transmitted on wire between frames | Not simulated (UDP packet boundaries) |
| Bit stuffing | Zero-insertion after 5 consecutive 1s | Not simulated |
| Clock detection | DCD reflects physical clock lock | DCD = !(Socket open) |
| Abort on wire | 7+ consecutive 1s | TX Abort clears FIFO but doesn't signal remote |
| Frame sequence numbers | HDLC sequence numbering | Single 32-bit handle incremented by 4 |
| Multi-station addressing | Multiple address modes | Fixed network.station |
| RTS/DTR signals | Drive physical pins | Logical only (affect CTS calculation) |
| Flag fill | Hardware monitors bus | Pseudo algorithm with timeouts |

### Configuration

**Econet.cfg format:**
```
# Comments start with #
AUNMODE 1
LEARN 0
AUNSTRICT 0
MASSAGENETS 0
FLAGFILLTIMEOUT 500000
SCOUTACKTIMEOUT 5000
TIMEBETWEENBYTES 128
FOURWAYTIMEOUT 500000

# Station definitions: network station ip port
0 254 127.0.0.1 32768
0 1 127.0.0.1 32769
```

**AUNMap file** (optional, only used when AUNMODE=1):
```
AddMap 192.168.0.0 128    # Subnet → Econet network number mapping
```

MASSAGENETS translates between Econet network numbers (0-127) and AUN network numbers (128-255) by toggling bit 7.

## Cycle-Accurate ADLC Design

Beebium takes a more principled approach than BeebEm's poll-based model, emulating the MC68B54 with cycle-accurate timing on the E-clock domain while abstracting the serial bit-level domain.

### Two Clock Domains

The real ADLC has two independent clock domains:

1. **E-clock domain** (system bus): Register access, FIFO advancement, status register derivation, interrupt generation. This is what the CPU interacts with.

2. **TxC/RxC domain** (serial bit clock): Bit-level framing — zero insertion/deletion, flag detection, CRC generation/checking. The Econet clock box provides ~200 kHz for ~200 kbit/s.

Since Beebium uses AUN-over-UDP rather than a real Econet wire, only the E-clock domain needs cycle-accurate emulation. The serial domain is abstracted to byte/frame-level timed events. The CPU sees perfectly accurate register behaviour, but bit-level operations (zero insertion, CRC, flag bytes) that serve no purpose over UDP are not modelled.

### ADLC E Clock: Confirmed 2MHz

The MC68B54 ADLC on the BBC Micro has its E pin connected to the **2MHz system clock**, not 1MHzE. Evidence:

1. **Bus stretching mask**: $FEA0-$BEBF is marked "fast" (no stretching) in `BusStretching.hpp`, confirmed by jsbeeb and beebjit
2. **BeebEm**: No `SyncIO()` applied to Econet register accesses (unlike VIAs, CRTC, ACIA which all apply 1MHz synchronisation)
3. **BeebEm comment** (`Econet.cpp:171`): "max 250Khz network clock. **2MHz system clock**. one click every 8 cycles."
4. **MC68B54 datasheet**: The "B" variant is rated for 2.0 MHz maximum E frequency (500ns minimum cycle time)
5. **MC68B54 datasheet**: "E should be a free-running clock such as the MC6800 MPU system clock"

The ADLC is analogous to the Tube ULA: fast, no stretching, 2MHz. This is not a 1MHz bus device despite its physical proximity to the 1MHz bus area on the PCB.

### FIFO Timing Model

On real hardware, data moves through the 3-byte FIFOs on **both phases** of the E clock (rising and falling). Beebium models this:

- **tick_rising()**: Advance TX FIFO towards the serial output; advance RX FIFO from serial input towards CPU. Update FIFO pointers and frame boundary markers.
- **tick_falling()**: Complete the transfer. Update status bits based on new FIFO state.

The simulated "serial clock" determines when new bytes enter the RX FIFO or leave the TX FIFO. Rather than modelling individual bits at 200 kHz, the network backend delivers/consumes whole bytes at a configurable rate (default: one byte every 128 E-clock cycles, ~64 us, matching BeebEm's `TimeBetweenBytes`).

Frame boundary tracking uses the datasheet's pointer model:
- **TX**: Writing to "Frame Continue" ($FEA2) sets the frame boundary pointer. Writing to "Frame Terminate" ($FEA3) resets it. When a negative transition is detected at the third FIFO location (pointer transitions from set to clear), the transmitter appends FCS and closing flag.
- **RX**: Address Present (AP) flag marks the first byte of a frame. Frame Valid (FV) is set when the last byte reaches the third FIFO location (positive transition of frame boundary pointer at FIFO position 3). Once FV is set, further data transfer to the last location is blocked until status is cleared.

### Status Register Model

Status bits are updated synchronously on E-clock edges, not lazily at poll time:

- **Present conditions** (continuously derived from current state): RDA, TDRA, Loop, Idle
- **Stored conditions** (latched on transitions, cleared by CPU): FD, CTS, TxU, AP, FV, ERR, OVRN
- **Dual-nature conditions** (logical OR of stored latch + present input): DCD, RxABT, Rx Idle
  - DCD: stored positive-edge latch OR current DCD input level
  - RxABT: stored in-frame abort latch OR Rx abort detect logic
  - Rx Idle: stored inactive idle latch OR receiver idling detector
  - Clearing the stored latch reveals the present condition — if the input is still asserted, the status bit remains set

On each E-clock falling edge:
1. Derive present conditions from FIFO state and input pins
2. Detect transitions for stored conditions (0→1 edges); latch new stored conditions
3. Combine stored + present for dual-nature bits
4. Apply PSE priority filtering if enabled
5. Compute SR1/SR2 from combined state
6. Derive IRQ output: `IRQ = (RIE && rx_cause) || (TIE && tx_cause)`

**Clearing status in prioritised mode**: A status condition must be read before it can be cleared. Clearing a higher-priority condition may unmask a lower-priority one, resulting in a new IRQ. This prevents inadvertent loss of status conditions.

The IRQ output asserts on the **same E-cycle** that sets the triggering status bit — matching the datasheet's specification that interrupt timing is synchronous to E.

### NMI Integration

The ADLC's IRQ output (active low) connects to the BBC Micro's NMI line, gated by the INTON/INTOFF hardware:

**Beebium integration:**
- `Mc6854` satisfies the `NmiSource` concept: `bool nmi_pending() const` returns the logical AND of the ADLC's IRQ output and the NMI-enable flip-flop state
- New mask constant: `kEconetNmiDeviceMask = 0x02` (bit 1, alongside disc controller's bit 0)
- Hardware's `poll_nmi()` aggregates both disc and ADLC NMI sources
- The ADLC's NMI is **not** subject to the 1MHz sampling restriction applied to the disc controller (since the ADLC runs at 2MHz and its IRQ output updates at 2MHz, it can be polled every cycle)

**NMI gating** is external to the ADLC — it's address-decoded hardware on the BBC board:
- INTOFF: reading $FE18 (station ID register) — clears the NMI-enable flip-flop
- INTON: accessing $FE20 range (Video ULA) — sets the NMI-enable flip-flop
- When INTON fires with a pending ADLC IRQ, NMI asserts immediately

### Machine::step() Integration

The `Mc6854` integrates into `Machine::step()` as a 2MHz peripheral:

```
ClockSubscriber: clock_rate = Rate_2MHz, clock_edges = Both
NmiSource:       nmi_pending() -> bool

step() {
    // ... existing CPU tick, video tick, VIA tick ...

    // Tick ADLC (if Econet hardware fitted)
    if (econet_enabled) {
        if (is_rising) adlc.tick_rising();
        else adlc.tick_falling();
    }

    // ... existing IRQ poll ...

    // NMI poll: aggregate disc + ADLC
    // ADLC NMI can be polled every 2MHz cycle (not restricted to 1MHz like disc)
    uint8_t nmi_mask = 0;
    if ((cycle_count & 1) == 0) {
        nmi_mask |= memory.poll_disc_nmi();   // Disc at 1MHz
    }
    if (econet_enabled) {
        nmi_mask |= adlc.nmi_pending() ? kEconetNmiDeviceMask : 0;
    }
    M6502_SetDeviceNMI(&cpu, kDiscNmiDeviceMask | kEconetNmiDeviceMask, nmi_mask);
}
```

The ADLC address region ($FEA0-$FEA3) is mapped via `Mirror<0x03>` in the Hardware's memory map, with the NMI gating addresses ($FE18, $FE20) handled by the existing SHEILA routing logic.

### Class Structure

```
Mc6854 (ClockSubscriber, NmiSource)
├── Registers
│   ├── cr1_, cr2_, cr3_, cr4_      // Control register latches
│   ├── sr1_, sr2_                   // Derived status (updated on E edges)
│   └── nmi_enable_                  // INTON/INTOFF flip-flop state
├── TX Path
│   ├── tx_fifo_[3]                  // 3-byte transmit FIFO
│   ├── tx_fifo_ptr_                 // Write pointer (0-3)
│   ├── tx_last_flags_               // Bitmask: which slots are "last byte"
│   └── tx_frame_buffer_             // Assembled frame awaiting network send
├── RX Path
│   ├── rx_fifo_[3]                  // 3-byte receive FIFO
│   ├── rx_fifo_ptr_                 // Read pointer (0-3)
│   ├── rx_ap_flags_                 // Bitmask: which slots have Address Present
│   ├── rx_fc_flags_                 // Bitmask: which slots have Frame Complete
│   └── rx_frame_buffer_             // Received frame being trickled into FIFO
├── Timing
│   ├── byte_timer_                  // Countdown for next byte transfer
│   └── byte_period_                 // Configurable (default 128 E-cycles)
├── Protocol State
│   ├── handshake_stage_             // 10-state FSM for AUN 4-way simulation
│   ├── flag_fill_active_            // Pseudo flag fill state
│   └── idle_                        // Line idle detection
├── Interface
│   ├── read(offset) -> uint8_t      // CPU register read
│   ├── write(offset, value)         // CPU register write
│   ├── tick_rising()                // E-clock rising edge
│   ├── tick_falling()               // E-clock falling edge
│   ├── nmi_pending() -> bool        // NmiSource concept
│   ├── set_nmi_enable(bool)         // INTON/INTOFF control
│   └── station_id() -> uint8_t      // Station number for $FE18 reads
└── Network Backend (injected)
    ├── send(frame) -> bool          // Send AUN packet via UDP
    ├── receive() -> optional<frame> // Non-blocking receive
    └── is_connected() -> bool       // Socket/clock status for DCD
```

The network backend is injected, keeping the ADLC as pure hardware emulation. Test doubles can simulate specific Econet scenarios without any UDP.

### Comparison with BeebEm

| Aspect | BeebEm | Beebium |
|--------|--------|---------|
| Tick granularity | Once per CPU instruction | Every 2MHz half-cycle (rising + falling) |
| FIFO model | Shift register with cycle-counted trickle | 3-byte FIFO with position pointers and frame boundary markers, advanced on E phases |
| Status derivation | Recomputed at poll time | Updated synchronously on E-clock falling edges |
| NMI timing | Edge detection at poll time (may be late by one instruction) | Asserts on same E-cycle as triggering status bit |
| E-clock phases | Not modelled | Both phases advance FIFO state |
| PSE (priority filtering) | Partial (4-tier) | Full per datasheet |
| Frame boundaries | Implicit in packet state machine | Explicit pointer transitions through FIFO positions |
| Serial bit-level | Not modelled | Not modelled (abstracted — same as BeebEm, but for different reasons) |
| Testability | Integrated with BeebEm global state | Isolated class with injected network backend |

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

### Phase 1: Cycle-Accurate ADLC Hardware Emulation

1. **Create `Mc6854` class** as a `ClockSubscriber` and `NmiSource`
   - `clock_rate = Rate_2MHz`, `clock_edges = Both` — ticked every 2MHz half-cycle
   - Four control registers (CR1-CR4) with correct addressing:
     - Register 0 write: always CR1
     - Register 1 write: CR2 (when CR1b0=0) or CR3 (when CR1b0=1)
     - Register 3 write with CR1b0=1: CR4
     - Register 2/3 write with CR1b0=0: TX data (register 3 auto-sets TX_LAST)
   - Status registers (SR1, SR2) updated synchronously on E-clock falling edges
   - 3-byte transmit and receive FIFOs advanced on both E phases (rising + falling)
   - FIFO frame boundary pointers: tx_last_flags_, rx_ap_flags_, rx_fc_flags_ (bitmasks)
   - PSE (Prioritised Status Enable) with full 4-tier priority filtering per datasheet
   - Auto-clearing control bits: CR1b5, CR2b4, CR2b5, CR2b6, CR4b5
   - Edge-triggered interrupt: IRQ asserts on same E-cycle as triggering status bit
   - Injected network backend (for testability — test doubles replace UDP)

2. **NMI gating (INTON/INTOFF)** — external to ADLC, in memory map routing
   - INTOFF: any access to &FE18-&FE1F (station ID range); also returns station number
   - INTON: any read from &FE20-&FE27 (Video ULA range)
   - On Master: INTOFF at &FE38-&FE3B, INTON at &FE3C-&FE3F
   - Flip-flop state stored in `Mc6854::nmi_enable_`, toggled via `set_nmi_enable(bool)`
   - When INTON fires with pending IRQ: assert NMI immediately

3. **Conditional hardware presence** (like disc controllers)
   - `--station N` enables Econet hardware AND sets station number
   - Without `--station`, no ADLC mapped — machine has no Econet fitted
   - DNFS ROM auto-detects: reads &FE18, if no Econet hardware, no NFS initialisation

4. **Memory mapping** (when enabled)
   - Map ADLC to &FEA0-&FEA3 via `Mirror<0x03>`
   - Map station ID register to &FE18 (returns configured station number)
   - No bus stretching required ($FEA0-$BEBF is "fast" at 2MHz)
   - NMI gating via INTON/INTOFF address decoding in SHEILA routing

5. **CTS/DCD logic**
   - DCD (SR2b5): low when network backend is connected (clock present), high otherwise
   - CTS: `!(backend.is_connected() && CR2b7_RTS)` — same logic as BeebEm
   - CTS positive-edge stored; cleared by Clear TX Status

6. **Integration with Machine::step()**
   - Tick ADLC on every 2MHz cycle (rising + falling edges) — no 1MHz restriction
   - `kEconetNmiDeviceMask = 0x02` alongside existing `kDiscNmiDeviceMask = 0x01`
   - Aggregate NMI: disc at 1MHz + ADLC at 2MHz (different polling rates)
   - Byte trickle rate: one TX/RX byte every 128 E-cycles (configurable)

### Phase 2: Econet Protocol Layer

1. **Frame assembly/disassembly**
   - TX: accumulate bytes from FIFO into frame buffer; send on TxLast
   - RX: trickle bytes from frame buffer into FIFO; set AP on first byte, FC on last
   - No CRC needed (UDP provides checksums; real Econet CRC not simulated by BeebEm)

2. **Four-way handshake state machine** (10 states)
   - Fakes scout/ack phases locally (AUN only does data+ack)
   - Scout timeout: 5,000 cycles (~2.5ms) before generating fake ack
   - Watchdog timeout: 500,000 cycles (~250ms) force-resets hung transactions
   - Scout payload sizes depend on control byte (0x82→8 bytes, 0x83-0x85→4 bytes, other→0)
   - Immediate operations: single exchange without scout phase
   - Broadcasts: fire-and-forget, no handshake

3. **Pseudo flag fill**
   - Set on send and on seeing traffic for other stations
   - Cleared on receiving packet for us, on timeout, or on handshake completion
   - Drives SR1b3 (Flag Detected)
   - Timeout: 500,000 cycles (~250ms)

4. **Idle detection**
   - Idle = RX not reset AND FIFO empty AND no FV AND no pending data
   - Drives SR2b2 (Inactive Idle) when `Idle && !FlagFillActive`

### Phase 3: AUN Network Layer

1. **UDP transport**
   - Socket management on port 32768 (configurable)
   - Non-blocking receive via `select()` with zero timeout
   - AUN packet encoding/decoding (8-byte header + payload)
   - Transaction handle: 32-bit sequence number incremented by 4
   - Broadcast via `SO_BROADCAST` socket option

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

1. **Timing accuracy**: ~~How cycle-accurate does the ADLC emulation need to be?~~ **Answered:** Beebium takes a cycle-accurate approach on the E-clock domain (2MHz, both phases), departing from BeebEm's poll-based model. The ADLC is ticked every 2MHz half-cycle with status bits updated synchronously on E-clock edges and NMI asserting on the same cycle as the triggering condition. The serial bit-level domain (TxC/RxC) is abstracted to byte-level timed events since Beebium uses AUN-over-UDP, not a real Econet wire. Byte trickle rate remains configurable (default 128 E-cycles per byte, ~64 us). See "Cycle-Accurate ADLC Design" section for full details.

2. **Clock detection**: ~~How should we handle the "no clock" error condition?~~ **Answered:** BeebEm equates "socket open" with "clock present". DCD (SR2b5) is set when socket is invalid, cleared when socket is open. CTS depends on both DCD and RTS (CR2b7). For Beebium: when `--station` is used and the network backend is connected, report clock present (DCD low). No connection = no clock = DCD high = NFS reports "No clock". The ADLC E clock is definitively 2MHz (see "Cycle-Accurate ADLC Design" section for evidence).

3. **Multi-network support**: Should we support multiple Econet network numbers for complex bridged setups? (Initial implementation: single network 0, expand later if needed. BeebEm supports MASSAGENETS for bit-7 translation between Econet 0-127 and AUN 128-255 ranges.)

4. **ROM licensing**: What is the legal status of distributing NFS/ANFS ROMs?

5. **Broadcast announcement format**: What packet format for local discovery? Could reuse AUN broadcast type, or define a simple Beebium-specific announcement.

6. **Self-send prevention**: BeebEm notes that real Econet can't send to itself (a station can't be both transmitting and receiving on the shared bus). `*STATIONS` poll sends a packet to itself which causes confusion in AUN mode. Should we explicitly drop packets addressed to our own station?

7. **NACK handling**: BeebEm has TODOs noting that NACKs are received but not properly handled - they're treated as ACKs. Should Beebium implement proper NACK/retry logic, or follow BeebEm's pragmatic approach?

8. **Immediate operation data sizes**: Control byte 0x82 expects 8-byte scout payload, 0x83-0x85 expect 4-byte. These magic numbers are baked into BeebEm with comments like "We're assuming things here." Need to verify these against the NFS ROM source or Econet documentation.

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

- **MC68B54 ADLC Datasheet** (Motorola) - `docs/datasheets/MOTOROLA MICROPROCESSORS DATA MANUAL 6854 section.pdf`
  - Complete register specifications, timing diagrams, programming considerations
  - Key insight: Status priority - test lowest priority conditions first (most frequent)
  - Stored vs Present status: DCD, Rx Abort, Rx Idle are OR of stored + present conditions
  - FIFO: 3-byte TX and RX FIFOs with pointer-based frame boundary tracking
  - E clock constraint: period between successive E pulses must be less than period of RxC/TxC
  - Key diagrams: Block diagram (Fig 1), Bus timing (Fig 6), TX/RX state diagrams (Fig 8a/8b), Status priority tree (Fig 10)
  - Bus timing table: MC68B54 cycle time min 0.5 us (= 2MHz max), E Low min 210ns, E High min 220ns

- **AUN Manager's Guide** (Acorn, 1992) - `docs/manuals_text/AUN_Managers_Guide/full_text.md`
  - AUN uses UDP + proprietary two-way handshake (not Econet's four-way)
  - IP address format: `1.network.net.station` (Class A, netmask &FFFF0000)
  - Uses RIP for routing, RevARP for client address discovery
  - Default isolated network address: `1.0.128.station`

- **DNFS Manual** (Acorn, 1984) - `docs/manuals_text/DNFS_Manual/full_text.md`
  - DNFS ROM contains both DFS 1.20 and NFS 3.60
  - Keyboard switch 1 selects DFS vs NFS at boot
  - Auto-detects Econet/DFS hardware presence

- **BBC Micro Advanced User Guide, Chapter 25** - `docs/manuals_text/Advanced_User_Guide/25_25._Floppy_Disc_and_Econet.md`
  - ADLC register addresses (&FEA0-&FEA3), station ID register (&FE18)
  - "The 68B54 ADLC is the central component in the Econet Interface circuit"
  - NMI auto-enabled by reading station ID register

- **BBC Micro Advanced User Guide, Chapter 28** - `docs/manuals_text/Advanced_User_Guide/28_28._The_One_Megahertz_bus.md`
  - 1MHz bus timing, bus stretching mechanism, double-accessing problem
  - NNMI pin (pin 6) connected directly to 6502 NMI input, pulled up to +5V with 3K3 resistor
  - NMIs triggered on negative-going edges

- **BBC Master New Advanced User Guide, Chapter 11** - `docs/manuals_text/BBC_Master-New-Advanced-User-Guide/11_11._Hardware.md`
  - Master-specific INTON/INTOFF addresses (&FE38/&FE3C vs Model B's &FE18/&FE20)

- Econet Advanced User Guide (Acorn, 1988)

### Code References

- BeebEm Windows (primary reference): `/Users/rjs/Code/beebem-windows/Src/Econet.cpp` + `Econet.h`
  - Most recently maintained version (~2720 lines)
  - Full AUN support (added 2009), 10-state handshake FSM
  - ADLC integration via `BeebMem.cpp` (INTON/INTOFF, register read/write)
  - NMI handling in `6502core.cpp` (EconetPoll called after each instruction)
- BeebEm macOS: `/Users/rjs/Code/beebem-mac/Src/Econet.cpp`
  - Structurally identical to Windows; `#ifdef __APPLE__` for platform differences
- Pi Econet Bridge: https://github.com/cr12925/PiEconetBridge

### Beebium Integration Points

- `src/core/include/beebium/BusStretching.hpp` — Econet at index 5 marked "fast" (no stretching), confirms 2MHz
- `src/core/include/beebium/Machine.hpp` — `Machine::step()` tick ordering, NMI polling, `kDiscNmiDeviceMask`
- `src/core/include/beebium/NmiAggregator.hpp` — `NmiSource` concept, `NmiBinding`, multi-source NMI aggregation
- `src/core/include/beebium/ClockConcepts.hpp` — `ClockSubscriber`, `StaticRateSubscriber` concepts
- `src/core/include/beebium/ClockBinding.hpp` — `should_tick()` based on static/dynamic clock rate
- `src/core/include/beebium/Via6522.hpp` — Existing 2MHz peripheral model (both edges, both phases)

### Online Resources

- J.G. Harston's Econet pages: http://mdfs.net/Docs/Comp/Acorn/Econet/
- BeebWiki Econet documentation
- StarDot forums (retro computing community)
