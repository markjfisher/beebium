# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

import sys
sys.path.insert(0, "/Users/rjs/Code/py8dis/py8dis")
from commands import *
import acorn

load(0x8000, "/Users/rjs/Code/beebium/roms/acorn-nfs_3_34.rom", "6502")

# acorn.bbc() provides: os_text_ptr ($F2), romsel_copy ($F4), osrdsc_ptr ($F6),
# all OS vectors (brkv, wrchv, ..., netv), all OS entry points (osasci, osbyte, ...),
# plus hooks for automatic OSBYTE/OSWORD annotation.
# acorn.is_sideways_rom() provides: rom_header, language_entry, service_entry,
# rom_type, copyright_offset, binary_version, title, language_handler, service_handler.
acorn.bbc()
acorn.is_sideways_rom()

# ============================================================
# Hardware registers
# ============================================================

# MC6854 ADLC registers (active at $FEA0-$FEA3 when active Econet station)
constant(0xFEA0, "adlc_cr1")   # Write: CR1 (or CR3 if AC=1). Read: SR1
constant(0xFEA1, "adlc_cr2")   # Write: CR2 (or CR4 if AC=1). Read: SR2
constant(0xFEA2, "adlc_tx")    # Write: TX FIFO (continue frame). Read: RX FIFO
constant(0xFEA3, "adlc_tx2")   # Write: TX FIFO (last byte, terminates frame). Read: RX FIFO

# Econet hardware on the 1MHz bus
constant(0xFE18, "econet_station_id")  # Read: station DIP switches AND INTOFF (disable NMIs)
constant(0xFE20, "econet_nmi_enable")  # Read: INTON (re-enable NMIs). Any read $FE20-$FE23.

# Other hardware
constant(0xFE30, "romsel")
constant(0xFEE0, "tube_control")

# Key ADLC register values (from original disassembly annotations):
#   CR1=$C1: full reset (TX_RESET|RX_RESET|AC)
#   CR1=$82: RX listen (TX_RESET|RIE)
#   CR1=$44: TX active (RX_RESET|TIE)
#   CR2=$67: clear all status (CLR_TX_ST|CLR_RX_ST|FC_TDRA|2_1_BYTE|PSE)
#   CR2=$E7: TX prepare (RTS|CLR_TX_ST|CLR_RX_ST|FC_TDRA|2_1_BYTE|PSE)
#   CR2=$3F: TX last data (CLR_RX_ST|TX_LAST_DATA|FLAG_IDLE|FC_TDRA|2_1_BYTE|PSE)
#   CR2=$A7: TX in handshake (RTS|CLR_TX_ST|FC_TDRA|2_1_BYTE|PSE)

# ============================================================
# Named constants for OSBYTE/OS values
# ============================================================
constant(20,  "osbyte_explode_chars")
constant(120, "osbyte_write_keys_pressed")
constant(143, "osbyte_issue_service_request")
constant(168, "osbyte_read_rom_ptr_table_low")

# ============================================================
# Zero page — Econet workspace ($90-$A9)
# ============================================================
# From J.G. Harston's NFSWorkSp and mdfs.net/Docs/Comp/BBC/AllMem

label(0x97, "escapable")           # b7=respond to Escape flag
label(0x98, "need_release_tube")   # b7=need to release Tube
label(0x9A, "net_tx_ptr")          # NetTx control block pointer (low)
label(0x9B, "net_tx_ptr_hi")       # NetTx control block pointer (high)
label(0x9C, "net_rx_ptr")          # NetRx control blocks pointer (low)
label(0x9D, "net_rx_ptr_hi")       # NetRx control blocks pointer (high)
label(0x9E, "nfs_workspace")       # General NFS workspace pointer (low)
label(0x9F, "nfs_workspace_hi")    # General NFS workspace pointer (high)
label(0xA0, "nmi_tx_block")        # Block to be transmitted (low)
label(0xA1, "nmi_tx_block_hi")     # Block to be transmitted (high)
label(0xA2, "port_buf_len")        # Open port buffer length (low)
label(0xA3, "port_buf_len_hi")     # Open port buffer length (high)
label(0xA4, "open_port_buf")       # Open port buffer address (low)
label(0xA5, "open_port_buf_hi")    # Open port buffer address (high)
label(0xA6, "port_ws_offset")      # Port workspace offset
label(0xA7, "rx_buf_offset")       # Receive buffer offset
label(0xA9, "rom_svc_num")         # ROM service number (7=osbyte, 8=osword)

# ============================================================
# Zero page — Filing system workspace ($B0-$CF)
# ============================================================
# CFS/RFS workspace, repurposed by NFS when Econet filing system active.
# From mdfs.net/Docs/Comp/BBC/AllMem

label(0xB0, "fs_load_addr")        # Load/start address (4 bytes)
label(0xB1, "fs_load_addr_hi")
label(0xB2, "fs_load_addr_2")
label(0xB8, "fs_error_ptr")        # Error pointer
label(0xBB, "fs_options")          # Current options
label(0xBC, "fs_block_offset")     # Offset into block
label(0xBD, "fs_last_byte_flag")   # b7=last byte from block
label(0xBE, "fs_crc_lo")           # CRC workspace (low)
label(0xBF, "fs_crc_hi")           # CRC workspace (high)
label(0xCD, "fs_temp_cd")          # Used as temporary storage by NFS
label(0xCE, "fs_temp_ce")          # Used as temporary storage by NFS

# Zero page — Additional OS locations
label(0x10, "zp_temp_10")          # Temporary storage (Y save during service calls)
label(0x11, "zp_temp_11")          # Temporary storage (X save during service calls)
label(0x16, "nmi_workspace_start") # Start of NMI workspace area ($0016-$0076)
label(0x63, "zp_63")               # Used by NFS

# ============================================================
# Page $0D — NMI handler workspace ($0D00-$0D67)
# ============================================================
# The NMI shim code occupies $0D00-$0D1F. Workspace follows at $0D20.
# From J.G. Harston's PageD (mdfs.net/Misc/Source/Acorn/NFS/PageD)

# NMI shim internal addresses
constant(0x0D0C, "nmi_jmp_lo")       # JMP target low byte (self-modifying)
constant(0x0D0D, "nmi_jmp_hi")       # JMP target high byte (self-modifying)
constant(0x0D0E, "set_nmi_vector")   # Subroutine: set NMI handler (A=low, Y=high)
constant(0x0D14, "nmi_rti")          # NMI return: restore ROM bank, PLA, BIT INTON, RTI

# Scout/acknowledge packet buffer ($0D20-$0D25)
constant(0x0D20, "tx_dst_stn")       # TX: Destination station
constant(0x0D21, "tx_dst_net")       # TX: Destination network
constant(0x0D22, "tx_src_stn")       # TX: Source station
constant(0x0D23, "tx_src_net")       # TX: Source network
constant(0x0D24, "tx_ctrl_byte")     # TX: Control byte
constant(0x0D25, "tx_port")          # TX: Port number
constant(0x0D26, "tx_data_start")    # TX: Start of data area

# TX control
constant(0x0D2A, "tx_data_len")      # Length of data in open port block
constant(0x0D3A, "tx_ctrl_status")   # TX control/status byte (shifted by ASL at $8E33)

# Received scout ($0D3D-$0D48)
constant(0x0D3D, "rx_src_stn")       # RX: Source station
constant(0x0D3E, "rx_src_net")       # RX: Source network
constant(0x0D3F, "rx_ctrl")          # RX: Control byte
constant(0x0D40, "rx_port")          # RX: Port number
constant(0x0D41, "rx_remote_addr")   # RX: Remote address (4 bytes) / broadcast data (8 bytes)

# TX state
constant(0x0D4A, "tx_flags")         # TX workspace flags (b0=four-way, b6=completion)
constant(0x0D4B, "nmi_next_lo")      # Next NMI handler address (low)
constant(0x0D4C, "nmi_next_hi")      # Next NMI handler address (high)
constant(0x0D4F, "tx_index")         # Current TX buffer index
constant(0x0D50, "tx_length")        # TX frame length

# RX/status
constant(0x0D38, "rx_status_flags")  # RX status/mode flags
constant(0x0D3B, "rx_ctrl_copy")     # Copy of received control byte
constant(0x0D52, "tx_in_progress")   # Referenced by NMI handlers
constant(0x0D5C, "scout_status")     # Scout/packet status indicator

# Econet state ($0D60-$0D67)
constant(0x0D62, "osword_busy")      # b7=Transmission in progress
constant(0x0D63, "protection_mask")  # Protection mask
constant(0x0D64, "rx_flags")         # b7=Rx at $00C0, b2=Halted
constant(0x0D65, "saved_prot_mask")  # Saved protection mask
constant(0x0D66, "econet_init_flag") # b7=Econet using NMI code ($00=no, $80=yes)
constant(0x0D67, "tube_flag")        # b7=Tube present ($00=no, $FF=yes)

# ============================================================
# Page $0E — Filing system workspace
# ============================================================
constant(0x0E00, "fs_server_stn")    # File server station number
constant(0x0E01, "fs_server_net")    # File server network number
constant(0x0E05, "fs_sequence")      # FS command sequence number
constant(0x0E09, "fs_csd_handle")    # Current selected directory handle
constant(0x0E0A, "fs_lib_handle")    # Library directory handle
constant(0x0E0B, "fs_work_0e0b")    # FS workspace
constant(0x0E0C, "fs_work_0e0c")    # FS workspace
constant(0x0E10, "fs_work_0e10")    # FS workspace
constant(0x0E11, "fs_work_0e11")    # FS workspace
constant(0x0E16, "fs_work_0e16")    # FS workspace

# Other workspace used by NFS
constant(0x0400, "nmi_handler_page4")  # NMI handler code copied from ROM
constant(0x0414, "nmi_init_entry")     # Entry point into copied NMI init code
constant(0x0500, "nmi_handler_page5")  # NMI handler code page 5
constant(0x0600, "nmi_handler_page6")  # NMI handler code page 6
constant(0x0DEB, "fs_state_deb")       # Filing system state

# ============================================================
# Named labels for key routines
# ============================================================
label(0x80AE, "return_1")
label(0x8145, "return_2")
label(0x8275, "return_3")

# ============================================================
# File header / overview comment (placed at $8000, first in code)
# ============================================================
comment(0x8000, """\
NFS ROM 3.34 disassembly (Acorn Econet filing system)

NMI handler architecture
========================
The NFS ROM uses self-modifying code to implement a state machine for
ADLC communication. An NMI workspace shim is copied to $0D00 at init.

NMI entry ($0D00):
  BIT $FE18       ; INTOFF -- immediately disable further NMIs
  PHA / TYA / PHA ; save A, Y
  LDA #$nn        ; page in NFS ROM bank (self-modified at init)
  STA $FE30
  JMP $xxxx       ; self-modifying target at $0D0C/$0D0D

set_nmi_vector ($0D0E): stores A->$0D0C (low), Y->$0D0D (high)
  Falls through to nmi_rti.

nmi_rti ($0D14):
  LDA $F4         ; restore original ROM bank
  STA $FE30
  PLA / TAY / PLA ; restore Y, A
  BIT $FE20       ; INTON -- re-enable NMIs
  RTI

NMI handler chain for transmission:
  $96F6 -> RX scout (idle listen)
  $9D4C -> TX data (2 bytes per NMI)
  $9D94 -> TX completion (switch to RX: CR1=$82)
  $9DB2 -> RX reply scout
  $9DC8 -> RX reply continuation

Key ADLC register values:
  CR1=$C1: full reset (TX_RESET|RX_RESET|AC)
  CR1=$82: RX listen (TX_RESET|RIE)
  CR1=$44: TX active (RX_RESET|TIE)
  CR2=$67: clear all status (CLR_TX_ST|CLR_RX_ST|FC_TDRA|2_1_BYTE|PSE)
  CR2=$E7: TX prepare (RTS|CLR_TX_ST|CLR_RX_ST|FC_TDRA|2_1_BYTE|PSE)
  CR2=$3F: TX last data (CLR_RX_ST|TX_LAST_DATA|FLAG_IDLE|FC_TDRA|2_1_BYTE|PSE)
  CR2=$A7: TX in handshake (RTS|CLR_TX_ST|FC_TDRA|2_1_BYTE|PSE)""")

# ============================================================
# Dispatch table at $8020 (low bytes) / $8044 (high bytes)
# ============================================================
# Used via PHA/PHA/RTS pattern at $80A4-$80AE.
# Three dispatch paths use this table:
#   - Service calls 0-12: index = svc_num + 1 (Y=0 at c809f)
#   - Language entry 0-4:  index = reason + 14 (Y=$0D at c809f)
#   - *NET1-4 commands:    index = char-'1' + 33 (Y=$20 at c809f)
#
# rts_code_ptr(lo_addr, hi_addr) decodes the address and adds entry().

# Service call handlers (indices 1-13)
for i in range(1, 14):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# Language entry handlers (indices 14-18)
for i in range(14, 19):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# Indices 19-32: unknown dispatcher, but addresses are all in ROM
for i in range(19, 33):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# *NET command handlers (indices 33-36)
# Index 36 overlaps: low byte at $8044 (= high table[0])
for i in range(33, 37):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# ============================================================
# NMI handler chain entry points
# ============================================================
# These are installed via self-modifying JMP at $0D0C/$0D0D,
# so py8dis cannot trace them automatically.

# --- ADLC init and idle listen ---
entry(0x96F0)   # ADLC init / reset entry
entry(0x96F6)   # RX scout (idle listen) - default NMI handler

# --- TX path: polling, data, completion ---
entry(0x9C48)   # INACTIVE polling loop (pre-TX)
entry(0x9D4C)   # NMI TX data handler (2 bytes per NMI)
entry(0x9D72)   # TX error path
entry(0x9D88)   # TX_LAST_DATA and frame completion
entry(0x9D94)   # TX completion: switch to RX mode

# --- RX reply handlers ---
entry(0x9DB2)   # RX reply scout handler
entry(0x9DC8)   # RX reply continuation handler
entry(0x9DE3)   # RX reply next handler

# --- Four-way handshake ---
entry(0x9EDD)   # Four-way handshake data phase
entry(0x9EE9)   # Four-way handshake RX scout
entry(0x9EFF)   # Four-way handshake RX continuation

# --- Completion / error ---
entry(0x9F39)   # TX completion handler
entry(0x9F3F)   # Error handler
entry(0x9F15)   # Reply validation handler

# --- Discovered via JMP $0D0E scan (NMI handler installations) ---
entry(0x9715)   # RX scout second byte handler
entry(0x9747)   # Full scout frame RX handler
entry(0x9839)   # Reply data RX handler
entry(0x984F)   # Reply data continuation
entry(0x9865)   # Handler installed at $9862
entry(0x989A)   # Handler installed at $9880
entry(0x98F7)   # Reply RX data handler
entry(0x9992)   # Reply completion handler

# --- NMI shim at end of ROM ---
entry(0x9FCB)   # Secondary NMI entry (BIT $FE18; PHA; TYA; PHA; STA romsel; JMP rx_scout)
entry(0x9FD9)   # set_nmi_vector + nmi_rti (in ROM, not in RAM workspace)
entry(0x9FEB)   # TX flags update + address calculation

# --- Data RX NMI handlers (four-way handshake data phase) ---
entry(0x9E2B)   # Data phase RX first byte
entry(0x9E50)   # Data phase RX continuation
entry(0x9EA4)   # Data phase RX bulk transfer

# ============================================================
# Additional known entry points
# ============================================================
entry(0x81D0)
entry(0x81E0)
entry(0x81FE)
entry(0x8200)

# ============================================================
# Code regions identified by manual inspection of equb data
# ============================================================
# These are preceded by RTS and start with valid opcodes, but
# are not reachable via JSR/JMP from already-traced code (their
# callers are themselves in equb regions -- cascading resolution).

entry(0x84A4)   # INY*5; RTS (pointer arithmetic helper)
entry(0x84AA)   # DEY*4; RTS (pointer arithmetic helper)
entry(0x87C8)   # PHA; JSR ... (called from $8789 and $8A6B)
entry(0x8844)   # STA abs; CMP#; ... (called from $8743)
entry(0x8933)   # TAY; BNE; ... (preceded by RTS, standalone entry)
entry(0x89EA)   # JSR $8508; ... (preceded by RTS, standalone entry)
entry(0x8E33)   # ASL $0D3A; ... (preceded by RTS, NMI workspace handler)
entry(0x9063)   # LDY zp; CMP#; ... (preceded by RTS, standalone entry)
entry(0x99BB)   # LDA $0D40; BNE; ... (NMI handler continuation)

# --- Code found in third-pass remaining equb regions ---
entry(0x8741)   # BEQ +3; JMP $8844 (called from $8743 region)
entry(0x8E7B)   # CMP #6; BCS ... (after 2-byte inline data table at $8E79)
entry(0x8F57)   # LDY #$28; ... (preceded by RTS, standalone entry)

# --- Code found in fourth-pass small equb regions ---
entry(0x8D5C)   # JSR ... (preceded by RTS, standalone entry)
entry(0x982D)   # LDA #$82; STA $FEA0; installs NMI handler $9839

# ============================================================
# Inline comments for key instructions (from original annotations)
# ============================================================
# Note: acorn.bbc()'s hooks auto-annotate OSBYTE/OSWORD calls, so
# we only add comments where the auto-annotations don't reach.

# NMI handler init — ROM code copies to page $04/$05/$06
comment(0x80F9, "Copy NMI handler code from ROM to RAM pages $04-$06")
comment(0x8113, "Copy NMI workspace initialiser from ROM to $0016-$0076")

# ============================================================
# ADLC full reset ($96DC)
# ============================================================
comment(0x96DC, """\
ADLC full reset
Aborts all activity and returns to idle RX listen mode.""")

comment(0x96DC, "CR1=$C1: TX_RESET | RX_RESET | AC (both sections in reset, address control set)", inline=True)
comment(0x96E1, "CR4=$1E (via AC=1): 8-bit RX word length, abort extend enabled, NRZ encoding", inline=True)
comment(0x96E6, "CR3=$00 (via AC=1): no loop-back, no AEX, NRZ, no DTR", inline=True)

# ============================================================
# Enter RX listen mode ($96EB)
# ============================================================
comment(0x96EB, """\
Enter RX listen mode
TX held in reset, RX active with interrupts. Clears all status.""")

comment(0x96EB, "CR1=$82: TX_RESET | RIE (TX in reset, RX interrupts enabled)", inline=True)
comment(0x96F0, "CR2=$67: CLR_TX_ST | CLR_RX_ST | FC_TDRA | 2_1_BYTE | PSE", inline=True)

# ============================================================
# NMI RX scout handler ($96F6) — idle listen
# ============================================================
comment(0x96F6, """\
NMI RX scout handler (initial byte)
Default NMI handler for incoming scout frames. Checks if the frame
is addressed to us or is a broadcast. Installed as the NMI target
during idle RX listen mode.
Tests SR2 bit0 (AP = Address Present) to detect incoming data.
Reads the first byte (destination station) from the RX FIFO and
compares against our station ID. Reading $FE18 also disables NMIs
(INTOFF side effect).""")

comment(0x96F6, "A=$01: mask for SR2 bit0 (AP = Address Present)", inline=True)
comment(0x96F8, "BIT SR2: Z = A AND SR2 -- tests if AP is set", inline=True)
comment(0x96FB, "AP not set, no incoming data -- check for errors", inline=True)
comment(0x96FD, "Read first RX byte (destination station address)", inline=True)
comment(0x9700, "Compare to our station ID ($FE18 read = INTOFF, disables NMIs)", inline=True)
comment(0x9703, "Match -- accept frame", inline=True)
comment(0x9705, "Check for broadcast address ($FF)", inline=True)
comment(0x9707, "Neither our address nor broadcast -- reject frame", inline=True)
comment(0x9709, "Flag $40 = broadcast frame", inline=True)
comment(0x970E, "Install next NMI handler at $9715 (RX scout second byte)", inline=True)

# ============================================================
# INACTIVE polling loop ($9C48)
# ============================================================
comment(0x9C48, """\
INACTIVE polling loop
Polls SR2 for INACTIVE (bit2) to confirm the network line is idle before
attempting transmission. Uses a 3-byte timeout counter on the stack.
The timeout (~256^3 iterations) generates "Line Jammed" if INACTIVE
never appears.
The CTS check at $9C66-$9C6B works because CR2=$67 has RTS=0, so
cts_input_ is always true, and SR1_CTS reflects presence of clock hardware.""")

comment(0x9C4D, "Y=$E7: CR2 value for TX prep (RTS|CLR_TX_ST|CLR_RX_ST|FC_TDRA|2_1_BYTE|PSE)", inline=True)
comment(0x9C4F, "A=$04: INACTIVE mask for SR2 bit2", inline=True)
comment(0x9C53, "INTOFF -- disable NMIs", inline=True)
comment(0x9C56, "INTOFF again (belt-and-braces)", inline=True)
comment(0x9C59, "BIT SR2: Z = $04 AND SR2 -- tests INACTIVE", inline=True)
comment(0x9C5C, "INACTIVE not set -- re-enable NMIs and loop", inline=True)
comment(0x9C5E, "Read SR1 (acknowledge pending interrupt)", inline=True)
comment(0x9C61, "CR2=$67: CLR_TX_ST|CLR_RX_ST|FC_TDRA|2_1_BYTE|PSE", inline=True)
comment(0x9C66, "A=$10: CTS mask for SR1 bit4", inline=True)
comment(0x9C68, "BIT SR1: tests CTS present", inline=True)
comment(0x9C6B, "CTS set -- clock hardware detected, start TX", inline=True)
comment(0x9C6D, "INTON -- re-enable NMIs ($FE20 read)", inline=True)
comment(0x9C71, "3-byte timeout counter on stack", inline=True)

# ============================================================
# Timeout error ($9C88) and TX setup ($9C84)
# ============================================================
comment(0x9C84, "TX_ACTIVE branch (A=$44 = CR1 value for TX active)")
comment(0x9C88, """\
Timeout error: writes CR2=$07 to abort, cleans stack,
returns error $40 ("Line Jammed").""")

comment(0x9C88, "CR2=$07: FC_TDRA | 2_1_BYTE | PSE (abort TX)", inline=True)
comment(0x9C90, "Error $40 = 'Line Jammed'", inline=True)

# ============================================================
# TX preparation ($9CA2)
# ============================================================
comment(0x9CA2, """\
TX preparation
Configures ADLC for transmission: asserts RTS via CR2, enables TIE via CR1,
installs NMI TX handler at $9D4C, and re-enables NMIs.""")

comment(0x9CA2, "Write CR2 = Y ($E7: RTS|CLR_TX_ST|CLR_RX_ST|FC_TDRA|2_1_BYTE|PSE)", inline=True)
comment(0x9CA5, "CR1=$44: RX_RESET | TIE (TX active, TX interrupts enabled)", inline=True)
comment(0x9CAA, "Install NMI handler at $9D4C (TX data handler)", inline=True)
comment(0x9CB4, "INTON -- NMIs now fire for TDRA ($FE20 read)", inline=True)

# ============================================================
# NMI TX data handler ($9D4C)
# ============================================================
comment(0x9D4C, """\
NMI TX data handler
Writes 2 bytes per NMI invocation to the TX FIFO at $FEA2. Uses the
BIT instruction on SR1 to test TDRA (V flag = bit6) and IRQ (N flag = bit7).
After writing 2 bytes, checks if the frame is complete. If more data,
tests SR1 bit7 (IRQ) via BMI -- if IRQ still asserted, writes 2 more bytes
without returning from NMI (tight loop). Otherwise returns via RTI.""")

comment(0x9D4C, "Load TX buffer index", inline=True)
comment(0x9D4F, "BIT SR1: V=bit6(TDRA), N=bit7(IRQ)", inline=True)
comment(0x9D52, "TDRA not set -- TX error", inline=True)
comment(0x9D54, "Load byte from TX buffer", inline=True)
comment(0x9D57, "Write to TX_DATA (continue frame)", inline=True)
comment(0x9D62, "Write second byte to TX_DATA", inline=True)
comment(0x9D65, "Compare index to TX length", inline=True)
comment(0x9D68, "Frame complete -- go to TX_LAST_DATA", inline=True)
comment(0x9D6A, "Check if we can send another pair", inline=True)
comment(0x9D6D, "IRQ set -- send 2 more bytes (tight loop)", inline=True)
comment(0x9D6F, "RTI -- wait for next NMI", inline=True)

# TX error path ($9D72-$9D85)
comment(0x9D72, "TX error path")
comment(0x9D72, "Error $42", inline=True)
comment(0x9D76, "CR2=$67: clear status, return to listen", inline=True)
comment(0x9D7B, "Error $41 (TDRA not ready)", inline=True)
comment(0x9D7D, "INTOFF (also loads station ID)", inline=True)
comment(0x9D80, "PHA/PLA delay loop (256 iterations for NMI disable)", inline=True)
comment(0x9D85, "Jump to error handler", inline=True)

# ============================================================
# TX_LAST_DATA and frame completion ($9D88)
# ============================================================
comment(0x9D88, """\
TX_LAST_DATA and frame completion
Signals end of TX frame by writing CR2=$3F (TX_LAST_DATA). Then installs
the TX completion NMI handler at $9D94 which switches to RX mode.
CR2=$3F = 0011_1111:
  bit5: CLR_RX_ST -- clears fv_stored_ (prepares for RX of reply)
  bit4: TX_LAST_DATA -- tells ADLC this is the final data byte
  bit3: FLAG_IDLE -- send flags/idle after frame
  bit2: FC_TDRA -- force clear TDRA
  bit1: 2_1_BYTE -- two-byte transfer mode
  bit0: PSE -- prioritised status enable
Note: NO CLR_TX_ST (bit6=0), NO RTS (bit7=0 -- drops RTS after frame)""")

comment(0x9D88, "CR2=$3F: TX_LAST_DATA | CLR_RX_ST | FLAG_IDLE | FC_TDRA | 2_1_BYTE | PSE", inline=True)
comment(0x9D8D, "Install NMI handler at $9D94 (TX completion)", inline=True)

# ============================================================
# TX completion: switch to RX mode ($9D94)
# ============================================================
comment(0x9D94, """\
TX completion: switch to RX mode
Called via NMI after the frame (including CRC and closing flag) has been
fully transmitted. Switches from TX mode to RX mode by writing CR1=$82.
CR1=$82 = 1000_0010: TX_RESET | RIE (listen for reply).
Checks workspace flags to decide next action:
  - bit6 set at $0D4A -> completion at $9F39
  - bit0 set at $0D4A -> four-way handshake data phase at $9EDD
  - Otherwise -> install RX reply handler at $9DB2""")

comment(0x9D94, "CR1=$82: TX_RESET | RIE (now in RX mode)", inline=True)
comment(0x9D99, "Test workspace flags", inline=True)
comment(0x9D9C, "bit6 not set -- check bit0", inline=True)
comment(0x9D9E, "bit6 set -- TX completion", inline=True)
comment(0x9DA8, "bit0 set -- four-way handshake data phase", inline=True)
comment(0x9DAB, "Install RX reply handler at $9DB2", inline=True)

# ============================================================
# RX reply scout handler ($9DB2)
# ============================================================
comment(0x9DB2, """\
RX reply scout handler
Handles reception of the reply scout frame after transmission.
Checks SR2 bit0 (AP) for incoming data, reads the first byte
(destination station) and compares to our station ID via $FE18
(which also disables NMIs as a side effect).""")

comment(0x9DB2, "A=$01: AP mask for SR2", inline=True)
comment(0x9DB4, "BIT SR2: test AP (Address Present)", inline=True)
comment(0x9DB7, "No AP -- error", inline=True)
comment(0x9DB9, "Read RX byte (destination station)", inline=True)
comment(0x9DBC, "Compare to our station ID (INTOFF side effect)", inline=True)
comment(0x9DBF, "Not our station -- error/reject", inline=True)
comment(0x9DC1, "Install next handler at $9DC8 (reply continuation)", inline=True)

# ============================================================
# RX reply continuation handler ($9DC8)
# ============================================================
comment(0x9DC8, """\
RX reply continuation handler
Reads remaining bytes of the reply scout: source network number,
then hands off to $9DE3 for source station/network validation.""")

comment(0x9DC8, "BIT SR2: test for RDA (bit7 = data available)", inline=True)
comment(0x9DCB, "No RDA -- error/timeout", inline=True)
comment(0x9DCD, "Read next byte (source network)", inline=True)
comment(0x9DD0, "Non-zero -- mismatch (expects $00 for net=0)", inline=True)
comment(0x9DD2, "Install next handler at $9DE3", inline=True)

# ============================================================
# RX reply validation handler ($9DE3)
# ============================================================
comment(0x9DE3, """\
RX reply validation
Validates the reply scout by comparing source station and network
against the destination in the original TX buffer.""")

comment(0x9DFF, "CR2=$A7: RTS|CLR_TX_ST|FC_TDRA|2_1_BYTE|PSE (TX in handshake)", inline=True)
comment(0x9E04, "CR1=$44: RX_RESET | TIE (TX active for ack)", inline=True)

go()
