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

# ============================================================
# Relocated code blocks
# ============================================================
# The NFS ROM copies code from ROM into RAM at initialisation ($80C8-$8119).
# These blocks execute at different addresses than their storage locations.
# move(dest, src, length) tells py8dis the runtime address for each block.
#
# The page copy loop ($80F9) starts with Y=0, DEY/BNE wraps through
# $FF..$01 — all 256 bytes of each page are copied.
#
# The workspace init ($8113) copies X=$60 downto 0 (BPL) = 97 bytes.
#
# Vectors set up during init:
#   BRKV  = $0016 (in workspace block — BRK/error handler)
#   RDCHV = $04E7 (in page 4 — RDCH handler)
#   WRCHV = $051C (in page 5 — WRCH handler)
#   EVNTV = $06E8 (in page 6 — event handler)

# BRK handler + NMI workspace init code ($9307 → $0016-$0076)
move(0x0016, 0x9307, 0x61)

# NMI handler / CLI command code ($934C/$944C/$954C → pages $04/$05/$06)
move(0x0400, 0x934C, 0x100)
move(0x0500, 0x944C, 0x100)
move(0x0600, 0x954C, 0x100)

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
# Inline string subroutine hook
# ============================================================
# sub_c853b prints an inline string following the JSR, terminated by a
# byte with bit 7 set. The high-bit byte is the opcode of the next
# instruction — the routine jumps there via JMP (fs_load_addr).
hook_subroutine(0x853b, "print_inline", stringhi_hook)

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

# NMI shim internal addresses (label not constant — these are memory refs)
label(0x0D0C, "nmi_jmp_lo")         # JMP target low byte (self-modifying)
label(0x0D0D, "nmi_jmp_hi")         # JMP target high byte (self-modifying)
label(0x0D0E, "set_nmi_vector")     # Subroutine: set NMI handler (A=low, Y=high)
label(0x0D14, "nmi_rti")            # NMI return: restore ROM bank, PLA, BIT INTON, RTI
label(0x0D1A, "nmi_shim_1a")        # Referenced by NMI workspace init

# Scout/acknowledge packet buffer ($0D20-$0D25)
label(0x0D20, "tx_dst_stn")         # TX: Destination station
label(0x0D21, "tx_dst_net")         # TX: Destination network
label(0x0D22, "tx_src_stn")         # TX: Source station
label(0x0D23, "tx_src_net")         # TX: Source network
label(0x0D24, "tx_ctrl_byte")       # TX: Control byte
label(0x0D25, "tx_port")            # TX: Port number
label(0x0D26, "tx_data_start")      # TX: Start of data area

# TX control
label(0x0D2A, "tx_data_len")        # Length of data in open port block
label(0x0D3A, "tx_ctrl_status")     # TX control/status byte (shifted by ASL at $8E33)

# Received scout ($0D3D-$0D48)
# $0D3D is also the base of the scout buffer for indexed access (STA $0D3D,Y)
label(0x0D3D, "rx_src_stn")         # RX: Source station (scout buffer base)
label(0x0D3E, "rx_src_net")         # RX: Source network
label(0x0D3F, "rx_ctrl")            # RX: Control byte
label(0x0D40, "rx_port")            # RX: Port number
label(0x0D41, "rx_remote_addr")     # RX: Remote address (4 bytes) / broadcast data (8 bytes)

# TX state
label(0x0D4A, "tx_flags")           # TX workspace flags (b0=four-way, b6=completion)
label(0x0D4B, "nmi_next_lo")        # Next NMI handler address (low)
label(0x0D4C, "nmi_next_hi")        # Next NMI handler address (high)
label(0x0D4F, "tx_index")           # Current TX buffer index
label(0x0D50, "tx_length")          # TX frame length
label(0x0D51, "tx_work_51")         # TX workspace
label(0x0D57, "tx_work_57")         # TX workspace

# RX/status
label(0x0D07, "nmi_shim_07")        # NMI shim workspace
label(0x0D38, "rx_status_flags")    # RX status/mode flags
label(0x0D3B, "rx_ctrl_copy")       # Copy of received control byte
label(0x0D52, "tx_in_progress")     # Referenced by NMI handlers
label(0x0D5C, "scout_status")       # Scout/packet status indicator
label(0x0D5D, "rx_extra_byte")      # Extra byte read after data frame completion

# Econet state ($0D60-$0D67)
label(0x0D62, "osword_busy")        # b7=Transmission in progress
label(0x0D63, "protection_mask")    # Protection mask
label(0x0D64, "rx_flags")           # b7=Rx at $00C0, b2=Halted
label(0x0D65, "saved_prot_mask")    # Saved protection mask
label(0x0D66, "econet_init_flag")   # b7=Econet using NMI code ($00=no, $80=yes)
label(0x0D67, "tube_flag")          # b7=Tube present ($00=no, $FF=yes)

# ============================================================
# Page $0E — Filing system workspace
# ============================================================
label(0x0E00, "fs_server_stn")      # File server station number
label(0x0E01, "fs_server_net")      # File server network number
label(0x0E05, "fs_sequence")        # FS command sequence number
label(0x0E08, "fs_work_0e08")      # FS workspace
label(0x0E09, "fs_csd_handle")     # Current selected directory handle
label(0x0E0A, "fs_lib_handle")     # Library directory handle
label(0x0E0B, "fs_work_0e0b")     # FS workspace
label(0x0E0C, "fs_work_0e0c")     # FS workspace
label(0x0E10, "fs_work_0e10")     # FS workspace
label(0x0E11, "fs_work_0e11")     # FS workspace
label(0x0E16, "fs_work_0e16")     # FS workspace

# Other workspace used by NFS
# Relocated code — BRK handler (BRKV = $0016)
label(0x0016, "brk_handler")
entry(0x0016)

# Relocated code — page 4 (NMI handlers, RDCH)
label(0x0400, "nmi_handler_page4")   # NMI handler code copied from ROM
label(0x0403, "nmi_tube_entry_2")    # JMP $06E2 (escape check for Tube)
label(0x0406, "nmi_tube_helper")     # Tube data transfer helper (in copied code)
label(0x0414, "nmi_init_entry")      # Entry point into copied NMI init code
label(0x04E7, "rdch_handler")        # RDCHV target
label(0x04EF, "tube_restore_xy")     # Restore X,Y from $10/$11, JSR c04F7
entry(0x0400)
entry(0x0403)
entry(0x0406)
entry(0x0414)
entry(0x04E7)
entry(0x04EF)

# Relocated code — page 5 (WRCH, *NET command dispatch)
# $0500-$051B: 14-entry dispatch table of word addresses used by
# JMP ($0500) at $0054 and indexed command dispatch.
label(0x0500, "nmi_handler_page5")   # NMI handler code page 5
label(0x051C, "wrch_handler")        # WRCHV target
entry(0x051C)
# Dispatch table targets (all *NET command handlers)
for addr in [0x055B, 0x05C5, 0x0626, 0x063B, 0x065D, 0x06A3,
             0x04EF, 0x053D, 0x058C, 0x0550, 0x0543, 0x0569,
             0x05D8, 0x0602]:
    entry(addr)

# Relocated code — page 6 (event handler, subroutines)
label(0x0600, "nmi_handler_page6")   # NMI handler code page 6
label(0x06E2, "tube_escape_check")   # LDA $FF; SEC; ROR; BMI to Tube write
label(0x06E8, "event_handler")       # EVNTV target
entry(0x0600)
entry(0x06E2)
entry(0x06E8)
label(0x0DEB, "fs_state_deb")        # Filing system state

# ============================================================
# Named labels for dispatch table and key routines
# ============================================================

# Dispatch tables: split low/high byte address tables
label(0x8020, "dispatch_lo")            # Low bytes of (handler_addr - 1)
label(0x8044, "dispatch_hi")            # High bytes of (handler_addr - 1)

# Dispatcher and dispatch callers
label(0x809F, "dispatch")               # PHA/PHA/RTS dispatcher entry
label(0x8069, "dispatch_net_cmd")       # *NET command dispatch (Y=$20)
# Note: $8099 is already labelled "language_handler" by acorn.is_sideways_rom()
label(0x8127, "dispatch_service")       # Service call dispatch (Y=$00)

# Filing system OSWORD dispatch ($8DF7-$8E01)
label(0x8E01, "fs_osword_dispatch")    # PHA/PHA/RTS dispatch for FS OSWORDs
label(0x8E18, "fs_osword_tbl_lo")      # Low bytes of FS OSWORD handler table
label(0x8E1D, "fs_osword_tbl_hi")      # High bytes of FS OSWORD handler table

# FS OSWORD handler routines
label(0x8E22, "copy_param_block")     # Bidirectional copy: C=1 param→ws, C=0 ws→param
label(0x8E33, "osword_0f_handler")    # OSWORD $0F: return TX result
label(0x8E53, "osword_11_handler")    # OSWORD $11: read FS reply data
label(0x8E7B, "osword_12_handler")    # OSWORD $12: read/write Econet state
label(0x8EF0, "osword_10_handler")    # OSWORD $10: allocate RX slot, copy FS command

# Econet TX/RX handler and OSWORD dispatch
label(0x8F72, "econet_tx_rx")          # Main TX/RX handler (A=0: send, A>=1: result)
label(0x9007, "osword_dispatch")       # OSWORD-style function dispatch (codes 0-8)
label(0x9020, "osword_trampoline")     # PHA/PHA/RTS trampoline
label(0x902B, "osword_tbl_lo")         # Dispatch table low bytes
label(0x9034, "osword_tbl_hi")         # Dispatch table high bytes
label(0x903D, "osword_fn4")            # Function 4: propagate carry
label(0x904B, "setup_tx_and_send")    # Set up TX ctrl block at ws+$0C and transmit

# Remote operation handlers
label(0x90FC, "remote_boot_handler")  # Remote boot: set up, download, execute at $0100
label(0x912A, "execute_at_0100")      # Zero $0100-$0102, JMP $0100
label(0x913A, "remote_validated")     # Remote op with source address validation
label(0x914A, "insert_remote_key")    # Insert char from RX block into keyboard buffer

# Control block setup
label(0x9159, "ctrl_block_setup_alt")  # Alternate entry into control block setup
label(0x9162, "ctrl_block_setup")     # Main entry: X=$1A, Y=$17, V=0 (nfs_workspace)
label(0x918E, "ctrl_block_template")  # Template table for control block initialisation

# Network transmit
label(0x9248, "econet_tx_retry")      # Transmit with retry until accepted or timeout

# Palette/VDU state save
label(0x9291, "save_palette_vdu")     # Save all 16 palette entries + VDU state

# ADLC initialisation and state management
label(0x966F, "adlc_init")           # Full init: reset ADLC, read station ID, install NMI shim
label(0x9681, "adlc_init_workspace") # Init workspace: copy NMI shim, set station ID
label(0x969D, "save_econet_state")   # Save status/protection/tube to RX block offsets 8-10
label(0x96B4, "restore_econet_state")# Restore status/protection/tube from RX block
label(0x96CD, "install_nmi_shim")    # Copy 32-byte NMI shim from ROM to $0D00

# Tube co-processor I/O subroutines (in relocated page 6)
label(0x06D0, "tube_write_data2")    # Poll Tube status 2, write A to data register 2
label(0x06D9, "tube_write_data4")    # Poll Tube status 4, write A to data register 4
label(0x06F7, "tube_write_ctrl1")    # Poll Tube control, write A to data register 1

label(0x80AE, "return_1")
label(0x8145, "return_2")
label(0x8275, "return_3")

# ============================================================
# Named labels for ADLC NMI handler routines
# ============================================================
# These replace auto-generated c/sub_ prefixed labels with
# descriptive names based on analysis of the NFS ROM's ADLC
# interaction and four-way handshake state machine.

# --- ADLC control ---
label(0x96DC, "adlc_full_reset")       # Full ADLC reset (CR1=$C1, CR4=$1E, CR3=$00)
label(0x96EB, "adlc_rx_listen")        # Enter RX listen mode (CR1=$82, CR2=$67)

# --- RX scout reception (inbound) ---
label(0x96F6, "nmi_rx_scout")          # Default NMI handler: check AP, read dest_stn
label(0x9715, "nmi_rx_scout_net")      # Read dest_net, validate network
label(0x9723, "scout_reject")          # Reject: wrong network (RX_DISCONTINUE)
label(0x9737, "scout_error")           # Error/discard: unexpected SR2 state (5 refs)
label(0x9744, "scout_discard")         # Clean discard via $9A40
label(0x974C, "scout_loop_rda")        # Scout data loop: check RDA
label(0x975C, "scout_loop_second")     # Scout data loop: read second byte of pair
label(0x9771, "scout_complete")        # Scout completion: disable PSE, check FV+RDA
label(0x9797, "scout_no_match")        # Scout port/station mismatch (3 refs)
label(0x979A, "scout_match_port")      # Port non-zero: look for matching RX block

# --- Data frame RX (inbound four-way handshake) ---
label(0x982D, "data_rx_setup")         # Switch to RX mode, install data RX handler
label(0x9839, "nmi_data_rx")           # Data frame: check AP, read dest_stn
label(0x984F, "nmi_data_rx_net")       # Data frame: validate dest_net = 0
label(0x9865, "nmi_data_rx_skip")      # Data frame: skip ctrl/port (already from scout)
label(0x988A, "rx_error")              # RX error dispatcher (13 refs -- most referenced!)
label(0x9894, "rx_error_reset")        # Full reset and discard
label(0x989A, "nmi_data_rx_bulk")      # Data frame: bulk read into port buffer
label(0x98CE, "data_rx_complete")      # Data frame completion: disable PSE, check FV
label(0x98F7, "nmi_data_rx_tube")      # Data frame: Tube co-processor variant

# --- Data frame completion and FV validation ---
label(0x9933, "data_rx_tube_complete") # Tube data frame completion
label(0x9930, "data_rx_tube_error")    # Tube data frame error (3 refs)

# --- ACK transmission ---
label(0x995E, "ack_tx")                # Send scout ACK or final ACK frame
label(0x9966, "ack_tx_configure")      # Configure CR1/CR2 for TX
label(0x9974, "ack_tx_write_dest")     # Write dest addr to TX FIFO
label(0x9992, "nmi_ack_tx_src")        # NMI: write src addr, send TX_LAST_DATA
label(0x99BB, "post_ack_scout")        # Post-ACK: process received scout data

# --- Discard and idle ---
label(0x9A34, "discard_reset_listen")  # Reset ADLC and return to idle (5 refs)
label(0x9A40, "discard_listen")        # Discard frame, return to idle listen
label(0x9A43, "discard_after_reset")   # Return to idle after reset
label(0x9A59, "immediate_op")          # Port=0 immediate operation handler

# --- TX path ---
label(0x9C48, "inactive_poll")         # Poll SR2 for INACTIVE before TX
label(0x9C84, "tx_active_start")       # Begin TX (CR1=$44)
label(0x9C88, "tx_line_jammed")        # Timeout error: "Line Jammed"
label(0x9CA2, "tx_prepare")            # Configure ADLC for TX, install NMI handler
label(0x9D4C, "nmi_tx_data")           # NMI TX: write 2 bytes per invocation
label(0x9D72, "tx_error")              # TX error path
label(0x9D88, "tx_last_data")          # Write TX_LAST_DATA, close frame
label(0x9D94, "nmi_tx_complete")       # TX done: switch to RX mode

# --- RX reply scout (outbound handshake) ---
label(0x9DB2, "nmi_reply_scout")       # Check AP, read dest_stn
label(0x9DC8, "nmi_reply_cont")        # Read dest_net, validate
label(0x9DDE, "reply_error")           # Reply error: store $41 (8 refs)
label(0x9DE3, "nmi_reply_validate")    # Read src_stn/net, check FV (Path 2)

# --- TX scout ACK + data phase ---
label(0x9E2B, "nmi_scout_ack_src")     # Write our station/net to TX FIFO
label(0x9E3B, "data_tx_begin")         # Begin data frame TX
label(0x9E50, "nmi_data_tx")           # NMI: send data payload from port buffer
label(0x9E7D, "data_tx_last")          # TX_LAST_DATA for data frame (5 refs)
label(0x9E8E, "data_tx_error")         # Data TX error (5 refs)
label(0x9E9B, "install_saved_handler") # Install handler from $0D4B/$0D4C
label(0x9EA4, "nmi_data_tx_tube")      # NMI: send data from Tube

# --- Four-way handshake: RX final ACK ---
label(0x9EDD, "handshake_await_ack")   # Switch to RX, await final ACK
label(0x9EE9, "nmi_final_ack")         # Check AP, read dest_stn
label(0x9EFF, "nmi_final_ack_net")     # Read dest_net, validate
label(0x9F15, "nmi_final_ack_validate")# Read src_stn/net, check FV

# --- Completion and error ---
label(0x9F39, "tx_result_ok")          # Store result=0 (success)
label(0x9F3D, "tx_result_fail")        # Store result=$41 (not listening) (9 refs)
label(0x9F3F, "tx_store_result")       # Store result code, signal completion (6 refs)
label(0x9F5B, "tx_calc_transfer")      # Calculate transfer size for RX block

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

NMI handler chain for outbound transmission (four-way handshake):
  $96F6: RX scout (idle listen, default handler)
  $9C48: INACTIVE polling (pre-TX, waits for idle line)
  $9D4C: TX data (2 bytes per NMI, tight loop if IRQ persists)
  $9D88: TX_LAST_DATA (close frame)
  $9D94: TX completion (switch to RX: CR1=$82)
  $9DB2: RX reply scout (check AP, read dest_stn)
  $9DC8: RX reply continuation (read dest_net, validate)
  $9DE3: RX reply validation (read src_stn/net, check FV)
  $9E24: TX scout ACK (write dest/src addr, TX_LAST_DATA)
  $9EDD: Four-way handshake data phase

NMI handler chain for inbound reception (scout -> data):
  $96F6: RX scout (idle listen)
  $9715: RX scout second byte (dest_net, install $9747)
  $9747: Scout data loop (read body in pairs, detect FV)
  $9771: Scout completion (disable PSE, read last byte)
  $995E: TX scout ACK
  $9839: RX data frame (AP check, validate dest_stn/net)
  $984F: RX data frame (validate src_net=0)
  $9865: RX data frame (skip ctrl/port bytes)
  $989A: RX data bulk read (read payload into buffer)
  $98CE: RX data completion (disable PSE, check FV, read last)
  $995E: TX final ACK

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
# Used via the PHA/PHA/RTS dispatch trick at $809F-$80AE.
#
# This is a standard 6502 computed-jump technique. The table stores
# handler addresses minus 1, split into separate low-byte and high-byte
# arrays. The dispatcher at $809F pushes the high byte then the low
# byte onto the stack. When RTS executes, it pops the address and adds
# 1, jumping to the handler.
#
# Multiple callers share this single table using different base offsets
# in Y. The dispatcher loop at $809F adds Y to X (the command index),
# so each caller maps its index into a different region of the table:
#
#   Caller              Y (base)  X (index)         Table indices
#   ─────────────────   ────────  ────────────────  ─────────────
#   Service calls        $00      svc_num            1-13
#   Language entry       $0D      reason             14-18
#   *NET1-4 commands     $20      char-'1'           33-36
#
# Index 0 and unused indices point to an RTS (null handler), so
# unrecognised service calls or out-of-range values fall through
# harmlessly.
#
# rts_code_ptr(lo_addr, hi_addr) decodes the address and adds entry().

# Service call handlers (indices 1-13)
for i in range(1, 14):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# Language entry handlers (indices 14-18)
for i in range(14, 19):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# Indices 19-32: secondary dispatch (see callers at $8079, $808C)
for i in range(19, 33):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# *NET command handlers (indices 33-36)
# Index 36 overlaps: low byte at $8044 (= high table[0])
for i in range(33, 37):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# ============================================================
# Filing system OSWORD dispatch table at $8E18/$8E1D
# ============================================================
# Used by the PHA/PHA/RTS dispatch at $8E01 (entered from sub_c8df7).
# sub_c8df7 subtracts $0F from the command code in $EF, giving a
# 0-4 index for OSWORD calls $0F-$13 (15-19).
#
# Index  OSWORD  Target   Purpose
# ─────  ──────  ───────  ────────────────────────────
#   0      $0F   $8E33    Protection/status control
#   1      $10   $8EF0    RX block read/setup
#   2      $11   $8E53    Data block copy
#   3      $12   $8E7B    FS server station lookup
#   4      $13   $8F72    Econet TX/RX handler
for i in range(5):
    rts_code_ptr(0x8E18 + i, 0x8E1D + i)

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

# --- Four-way handshake (outbound data phase) ---
entry(0x9EDD)   # Four-way handshake: switch to RX for final ACK
entry(0x9EE9)   # Four-way handshake: RX final ACK (check AP, dest_stn)
entry(0x9EFF)   # Four-way handshake: RX final ACK continuation (dest_net=0)

# --- Completion / error ---
entry(0x9F39)   # Completion handler (store result=0 to tx_block)
entry(0x9F3F)   # Error handler (store error code to tx_block)
entry(0x9F15)   # Four-way handshake: validate final ACK src addr

# --- Discovered via JMP $0D0E scan (NMI handler installations) ---
entry(0x9715)   # RX scout second byte handler (dest network)
entry(0x9747)   # Scout data reading loop (reads body in pairs)
entry(0x9839)   # Data frame RX handler (four-way handshake)
entry(0x984F)   # Data frame: validate source network = 0
entry(0x9865)   # Data frame: skip ctrl/port bytes
entry(0x989A)   # Data frame: bulk data read loop
entry(0x98F7)   # Data frame: Tube co-processor RX handler
entry(0x9992)   # ACK TX continuation (write src addr, TX_LAST_DATA)

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
entry(0x99BB)   # Post-ACK: process received scout (check port, match RX block)

# --- Econet TX/RX handler and OSWORD dispatch ($8F72-$904A) ---
# $8F72: Main transmit/receive handler entry (A=0: set up and send, A>=1: handle result)
# $9007: OSWORD-style dispatch handler (function codes 0-8, PHA/PHA/RTS)
entry(0x8F72)   # Econet TX/RX handler (CMP #1; BCS)
entry(0x9007)   # Econet function dispatch handler (PHP; PHA; save regs)
entry(0x9020)   # Dispatch trampoline (PHA/PHA/RTS into table at $902B/$9034)
entry(0x903D)   # Function 4 handler: propagate carry into stacked P

# Dispatch table at $902B (low bytes) / $9034 (high bytes)
# 9 entries for function codes 0-8, used via PHA/PHA/RTS at $9020.
# Targets: 0=$8145(RTS), 1-3=$91C7, 4=$903D, 5=$91B5, 6=$8145(RTS), 7=$9063, 8=$90CD
for i in range(9):
    rts_code_ptr(0x902B + i, 0x9034 + i)

# Alternate entry into sub_c9162 (ADLC register setup)
entry(0x9159)   # ADLC setup: LDX #$0D; LDY #$7C; BIT $833A; BVS c9167

# Dispatch targets found in equb data regions
entry(0x91B5)   # Function 5 handler
entry(0x91C7)   # Function 1/2/3 handler (shared)
entry(0x90CD)   # Function 8 handler

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

# ============================================================
# print_inline subroutine ($853B)
# ============================================================
comment(0x853B, """\
Print inline string (high-bit terminated)
Pops the return address from the stack, prints each byte via OSASCI
until a byte with bit 7 set is found, then jumps to that address.
The high-bit byte serves as both the string terminator and the opcode
of the first instruction after the string.""")

comment(0x853B, "Pop return address (low) — points to last byte of JSR", inline=True)
comment(0x853E, "Pop return address (high)", inline=True)
comment(0x8543, "Advance pointer past return address / to next char", inline=True)
comment(0x8549, "Load next byte from inline string", inline=True)
comment(0x854B, "Bit 7 set? Done — this byte is the next opcode", inline=True)
comment(0x8552, "Jump to address of high-bit byte (resumes code after string)", inline=True)

# ============================================================
# Dispatch table comments ($8020-$8068)
# ============================================================
comment(0x8020, """\
Dispatch table: low bytes of (handler_address - 1)
Each entry stores the low byte of a handler address minus 1,
for use with the PHA/PHA/RTS dispatch trick at $809F.
See dispatch_hi ($8044) for the corresponding high bytes.
Indexed by service number (1-13), language reason (14-18),
or *NET command (33-36), with a base offset added by the caller.""")

comment(0x8044, """\
Dispatch table: high bytes of (handler_address - 1)
Paired with dispatch_lo ($8020). Together they form a table of
37 handler addresses, used via the PHA/PHA/RTS trick at $809F.""")

# ============================================================
# *NET command dispatch ($8069)
# ============================================================
comment(0x8069, """\
*NET command dispatcher
Parses the character after *NET as '1'-'4', maps to table
indices 33-36 via base offset Y=$20, and dispatches via $809F.
Characters outside '1'-'4' fall through to return_1 (RTS).""")

comment(0x8069, "Read command character following *NET", inline=True)
comment(0x806B, "Subtract ASCII '1' to get 0-based command index", inline=True)
comment(0x8075, "Y=$20: base offset for *NET commands (index 33+)", inline=True)

# ============================================================
# PHA/PHA/RTS dispatcher ($809F)
# ============================================================
comment(0x809F, """\
PHA/PHA/RTS computed dispatch
X = command index within caller's group (e.g. service number)
Y = base offset into dispatch table (0, $0D, $20, etc.)
The loop adds Y+1 to X, so final X = command index + base + 1.
Then high and low bytes of (handler-1) are pushed onto the stack,
and RTS pops them and jumps to handler_address.

This is a standard 6502 trick: RTS increments the popped address
by 1 before jumping, so the table stores (address - 1) to
compensate. Multiple callers share one table via different Y
base offsets.""")

comment(0x809F, "Add base offset Y to index X (loop: X += Y+1)", inline=True)
comment(0x80A4, "Load high byte of (handler - 1) from table", inline=True)
comment(0x80A7, "Push high byte onto stack", inline=True)
comment(0x80A8, "Load low byte of (handler - 1) from table", inline=True)
comment(0x80AB, "Push low byte onto stack", inline=True)
comment(0x80AC, "Restore X (fileserver options) for use by handler", inline=True)
comment(0x80AE, "RTS pops address, adds 1, jumps to handler", inline=True)

# ============================================================
# Language entry dispatch ($8099)
# ============================================================
comment(0x8099, """\
Language entry dispatcher
Called when the NFS ROM is entered as a language. X = reason code
(0-4). Dispatches via table indices 14-18 (base offset Y=$0D).""")

comment(0x809D, "Y=$0D: base offset for language handlers (index 14+)", inline=True)

# ============================================================
# Service call dispatch ($8127)
# ============================================================
comment(0x8127, """\
Service call dispatcher
Dispatches MOS service calls 0-12 via the shared dispatch table.
Uses base offset Y=0, so table index = service number + 1.
Service numbers >= 13 are ignored (branch to return_2).
Called via JSR $809F rather than fall-through, so it returns
to $813C to restore saved registers.""")

comment(0x8137, "Y=0: base offset for service calls (index 1+)", inline=True)
comment(0x8139, "JSR to dispatcher (returns here after handler completes)", inline=True)

# NMI handler init — ROM code copies to page $04/$05/$06
# ============================================================
# Filing system OSWORD dispatch ($8DF7 / $8E01)
# ============================================================
comment(0x8DF7, """\
Filing system OSWORD entry
Subtracts $0F from the command code in $EF, giving a 0-4 index
for OSWORD calls $0F-$13 (15-19). Falls through to the
PHA/PHA/RTS dispatch at $8E01.""")

comment(0x8DF7, "Command code from $EF", inline=True)
comment(0x8DF9, "Subtract $0F: OSWORD $0F-$13 become indices 0-4", inline=True)

comment(0x8E01, """\
PHA/PHA/RTS dispatch for filing system OSWORDs
X = OSWORD number - $0F (0-4). Dispatches via the 5-entry table
at $8E18 (low) / $8E1D (high).""")

comment(0x8E18, "Dispatch table: low bytes for OSWORD $0F-$13 handlers", inline=True)
comment(0x8E1D, "Dispatch table: high bytes for OSWORD $0F-$13 handlers", inline=True)

comment(0x80F9, "Copy NMI handler code from ROM to RAM pages $04-$06")
comment(0x8113, "Copy NMI workspace initialiser from ROM to $0016-$0076")

# ============================================================
# Econet TX/RX handler ($8F72)
# ============================================================
comment(0x8F72, """\
Econet transmit/receive handler
A=0: Initialise TX control block from ROM template at $8310
     (zero entries substituted from NMI workspace $0DDA), transmit
     it, set up RX control block, and receive reply.
A>=1: Handle transmit result (branch to cleanup at $8F48).""")

comment(0x8F72, "A=0: set up and transmit; A>=1: handle result", inline=True)
comment(0x8F78, "Load from ROM template (zero = use NMI workspace value)", inline=True)
comment(0x8FA3, "Enable interrupts before transmit", inline=True)
comment(0x8FA9, "Dest station = $FFFF (accept reply from any station)", inline=True)
comment(0x8FBB, "Initiate receive with timeout", inline=True)
comment(0x8FBE, "Non-zero = error/timeout: jump to cleanup", inline=True)

# Data transfer loop ($8FD7-$8FF3)
comment(0x8FD7, "Receive data blocks until command byte = $00 or $0D", inline=True)
comment(0x8FEF, "Test for end-of-data marker ($0D)", inline=True)

# ============================================================
# OSWORD-style function dispatch ($9007)
# ============================================================
comment(0x9007, """\
Econet function dispatch handler
Saves all registers, retrieves the function code from the stacked A,
and dispatches to one of 9 handlers (codes 0-8) via the PHA/PHA/RTS
trampoline at $9020. Function codes >= 9 are ignored.

Dispatch targets:
  0,6: $8145 (return_2, no-op RTS)
  1-3: $91C7 (shared handler)
  4:   $903D (propagate carry into stacked P)
  5:   $91B5
  7:   $9063
  8:   $90CD""")

comment(0x900E, "Retrieve original A (function code) from stack", inline=True)
comment(0x9020, "PHA/PHA/RTS trampoline: push handler addr-1, RTS jumps to it", inline=True)

# ============================================================
# Function 4 handler ($903D)
# ============================================================
comment(0x903D, """\
Function 4: propagate carry into stacked processor status
The ROR/ASL pair on the stacked P register replaces the saved
carry flag with the current carry, so the caller gets the
updated carry after PLP restores the flags.""")

comment(0x903E, "ROR then ASL the stacked P: replaces saved carry with current carry", inline=True)

# ============================================================
# Control block setup routine ($9159 / $9162)
# ============================================================
comment(0x9159, """\
Alternate entry into control block setup
Sets X=$0D, Y=$7C. Tests bit 6 of $833A to choose target:
  V=0 (bit 6 clear): stores to (nfs_workspace)
  V=1 (bit 6 set):   stores to (net_rx_ptr)""")

comment(0x9162, """\
Control block setup — main entry
Sets X=$1A, Y=$17, clears V (stores to nfs_workspace).
Reads the template table at $918E indexed by X, storing each
value into the target workspace at offset Y. Both X and Y
are decremented on each iteration.

Template sentinel values:
  $FE = stop (end of template for this entry path)
  $FD = skip (leave existing value unchanged)
  $FC = use page high byte of target pointer""")

comment(0x9167, "Load template byte from ctrl_block_template[X]", inline=True)

comment(0x918E, """\
Control block initialisation template
Read by the loop at $9167, indexed by X from a starting value
down to 0. Values are stored into either (nfs_workspace) or
(net_rx_ptr) at offset Y, depending on the V flag.

Two entry paths read different slices of this table:
  sub_c9162:          X=$1A (26) down, Y=$17 (23) down, V=0
  ctrl_block_setup_alt: X=$0D (13) down, Y=$7C (124) down, V from BIT $833A

Sentinel values:
  $FE = stop processing
  $FD = skip this offset (decrement Y but don't store)
  $FC = substitute the page byte (net_rx_ptr_hi or nfs_workspace_hi)""")

# ============================================================
# Bidirectional block copy ($8E22)
# ============================================================
comment(0x8E22, """\
Bidirectional block copy between OSWORD param block and workspace.
C=1: copy X+1 bytes from ($F0),Y to (fs_crc_lo),Y (param to workspace)
C=0: copy X+1 bytes from (fs_crc_lo),Y to ($F0),Y (workspace to param)""")

# ============================================================
# Remote operation handlers ($90FC / $912A / $913A / $914A)
# ============================================================
comment(0x90FC, """\
Remote boot/execute handler
Validates byte 4 of the RX control block (must be zero), copies the
2-byte execution address from RX offsets $80/$81 into NFS workspace,
sets up a control block, disables keyboard (OSBYTE $C9), then falls
through to execute_at_0100.""")

comment(0x912A, """\
Execute downloaded code at $0100
Zeroes $0100-$0102 (safe BRK default), restores the protection mask,
and JMP $0100 to execute code received over the network.""")

comment(0x914A, """\
Insert remote keypress
Reads a character from RX block offset $82 and inserts it into
keyboard input buffer 0 via OSBYTE $99.""")

# ============================================================
# ADLC initialisation ($966F)
# ============================================================
comment(0x966F, """\
ADLC initialisation
Reads station ID (INTOFF side effect), performs full ADLC reset,
checks for Tube presence (OSBYTE $EA), then falls through to
adlc_init_workspace.""")

comment(0x9681, """\
Initialise NMI workspace
Copies NMI shim from ROM to $0D00, stores current ROM bank number
into shim self-modifying code, sets TX status to $80 (idle/complete),
saves station ID from $FE18 into TX scout buffer, re-enables NMIs
by reading $FE20.""")

comment(0x969D, """\
Save Econet state to RX control block
Stores rx_status_flags, protection_mask, and tx_in_progress
to (net_rx_ptr) at offsets 8-10. INTOFF side effect on entry.""")

comment(0x96B4, """\
Restore Econet state from RX control block
Loads rx_status_flags, protection_mask, and tx_in_progress
from (net_rx_ptr) at offsets 8-10, then reinitialises via
adlc_init_workspace.""")

comment(0x96CD, """\
Copy NMI shim from ROM ($9FCA) to RAM ($0D00)
Copies 32 bytes. Interrupts are enabled during the copy.""")

# ============================================================
# Relocated code block comments
# ============================================================
comment(0x0016, """\
BRK handler (BRKV target)
Handles errors when a Tube co-processor is active. Reads the error
message from zero page via ($FD), sends each byte to the Tube,
resets the stack, then dispatches through the page 5 vector table
via JMP ($0500). Also polls Tube registers for incoming data.""")

comment(0x0400, """\
NMI handler page 4 — Tube protocol and CLI dispatch
Copied from ROM at $934C during init. Contains:
  $0400: entry (JMP to CLI parser at $0473)
  $0403: escape check for Tube (JMP $06E2)
  $0406: nmi_tube_helper (Tube data transfer protocol)
  $0414: nmi_init_entry (called after copy, sets $14=$80)
  $0473: CLI command parser (ROM title, break type check)
  $04E7: RDCHV handler (read character for Tube)
  $04EF: restore X/Y, poll Tube status
  $04F7: poll Tube status register 2, read data""")

comment(0x0500, """\
NMI handler page 5 — WRCHV and file I/O commands
Copied from ROM at $944C during init. Contains:
  $0500-$051B: 14-entry dispatch table (word addresses)
  $051C: WRCHV handler (write character via Tube)
  $0543: OSBPUT (write byte to file via Tube)
  $0550: OSBGET (read byte from file via Tube)
  $055B: OSRDCH (read character via Tube)
  $0569: OSFIND open (open file via Tube)
  $058C: OSARGS (file attributes via Tube)
  $05B1: read filename/command from Tube into $0700
  $05C5: OSCLI (execute * command via Tube)
  $05D8: OSFILE (whole file operation via Tube)""")

comment(0x0600, """\
NMI handler page 6 — OSGBPB, OSBYTE/OSWORD, event handler
Copied from ROM at $954C during init. Contains:
  $0600: entry (BEQ to JMP $003A, else fall through)
  $0602: OSGBPB (multi-byte file I/O via Tube)
  $0626: OSBYTE 2-param (X result via Tube)
  $063B: OSBYTE 3-param (X,Y results via Tube)
  $065D: OSWORD variable-length (via Tube, buffer at $0130)
  $06A3: OSWORD fixed 5-byte (via Tube)
  $06D0: tube_write_data2 — poll+write to Tube data register 2
  $06D9: tube_write_data4 — poll+write to Tube data register 4
  $06E2: tube_escape_check — check $FF, forward escape to Tube
  $06E8: EVNTV handler — forward event (A,X,Y) to Tube
  $06F7: tube_write_ctrl1 — poll+write to Tube control register 1""")

# ============================================================
# OSBYTE code table for VDU state save ($9304)
# ============================================================
label(0x9304, "osbyte_vdu_table")
comment(0x9304, """\
Table of 3 OSBYTE codes used by save_palette_vdu_state ($9291):
  $85 = read cursor position
  $C2 = read shadow RAM allocation
  $C3 = read screen start address""")

# ============================================================
# Relocated code block sources ($9307, $934C, $944C, $954C)
# ============================================================
# These labels mark the ROM storage addresses. The code is
# disassembled at its runtime addresses via move() declarations
# near the top of this file. See the preamble for addresses.

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
# RX scout second byte handler ($9715)
# ============================================================
comment(0x9715, """\
RX scout second byte handler
Reads the second byte of an incoming scout (destination network).
Checks for network match: 0 = local network (accept), $FF = broadcast
(accept and flag), anything else = reject.
Installs the scout data reading loop handler at $9747.""")

comment(0x9715, "BIT SR2: test for RDA (bit7 = data available)", inline=True)
comment(0x9718, "No RDA -- check errors", inline=True)
comment(0x971A, "Read destination network byte", inline=True)
comment(0x971D, "Network = 0 -- local network, accept", inline=True)
comment(0x971F, "EOR $FF: test if network = $FF (broadcast)", inline=True)
comment(0x9721, "Broadcast network -- accept", inline=True)
comment(0x9723, "Reject: wrong network. CR1=$A2: RIE|RX_DISCONTINUE", inline=True)

comment(0x972B, "Network = $FF broadcast: clear $0D4A", inline=True)
comment(0x972E, "Store Y offset for scout data buffer", inline=True)
comment(0x9730, "Install scout data reading loop at $9747", inline=True)

# ============================================================
# Error/discard path ($9737)
# ============================================================
comment(0x9737, """\
Scout error/discard handler
Reached when the scout data loop sees no RDA (BPL at $974C) or
when scout completion finds unexpected SR2 state.
If SR2 & $81 is non-zero (AP or RDA still active), performs full
ADLC reset and discards. If zero (clean end), discards via $9A40.
This path is a common landing for any unexpected ADLC state during
scout reception.""")

comment(0x9737, "Read SR2", inline=True)
comment(0x973A, "Test AP (b0) | RDA (b7)", inline=True)
comment(0x973C, "Neither set -- clean end, discard via $9A40", inline=True)
comment(0x973E, "Unexpected data/status: full ADLC reset", inline=True)
comment(0x9741, "Discard and return to idle", inline=True)

# ============================================================
# Scout data reading loop ($9747-$976E)
# ============================================================
# This is the critical Path 1 code for ADLC FV/PSE interaction.
# The loop reads scout body bytes two at a time, storing them at
# $0D3D+Y. Between each pair, it checks SR2 to detect frame
# completion (FV set) or errors.
#
# ADLC timing requirement: after the CPU reads the penultimate byte,
# FV must be visible in SR2 before the next SR2 check. Beebium's
# inline refill + byte timer reset ensures this: reading the
# penultimate byte triggers inline refill of the last byte, which
# sets FV immediately (push-time FV). The byte timer reset prevents
# the timer from firing mid-loop.
comment(0x9747, """\
Scout data reading loop
Reads the body of a scout frame, two bytes per iteration. Stores
bytes at $0D3D+Y (scout buffer: src_stn, src_net, ctrl, port, ...).
Between each pair it checks SR2:
  - SR2 & $81 tested at entry ($974A): AP|RDA bits
    - Neither set (BEQ) -> discard ($9744 -> $9A40)
    - AP without RDA (BPL) -> error ($9737)
    - RDA set (BMI) -> read byte
  - After first byte ($9755): full SR2 tested
    - SR2 non-zero (BNE) -> scout completion ($9771)
      This is the FV detection point: when FV is set (by inline refill
      of the last byte during the preceding RX FIFO read), SR2 is
      non-zero and the branch is taken.
    - SR2 = 0 -> read second byte and loop
  - After second byte ($9769): re-test SR2 & $81 for next pair
    - RDA set (BMI) -> loop back to $974E
    - Neither set -> RTI, wait for next NMI
The loop ends at Y=$0C (12 bytes max in scout buffer).""")

comment(0x9747, "Y = buffer offset", inline=True)
comment(0x9749, "Read SR2", inline=True)
comment(0x974C, "No RDA -- error handler $9737", inline=True)
comment(0x974E, "Read data byte from RX FIFO", inline=True)
comment(0x9751, "Store at $0D3D+Y (scout buffer)", inline=True)
comment(0x9754, "Advance buffer index", inline=True)
comment(0x9755, "Read SR2 again (FV detection point)", inline=True)
comment(0x9758, "RDA set -- more data, read second byte", inline=True)
comment(0x975A, "SR2 non-zero (FV or other) -- scout completion", inline=True)
comment(0x975C, "Read second byte of pair", inline=True)
comment(0x975F, "Store at $0D3D+Y", inline=True)
comment(0x9762, "Advance and check buffer limit", inline=True)
comment(0x9765, "Buffer full (Y=12) -- force completion", inline=True)
comment(0x9767, "Save Y for next iteration", inline=True)
comment(0x9769, "Read SR2 for next pair", inline=True)
comment(0x976C, "SR2 non-zero -- loop back for more bytes", inline=True)
comment(0x976E, "SR2 = 0 -- RTI, wait for next NMI", inline=True)

# ============================================================
# Scout completion ($9771-$978F)
# ============================================================
comment(0x9771, """\
Scout completion handler
Reached from the scout data loop when SR2 is non-zero (FV detected).
Disables PSE to allow individual SR2 bit testing:
  CR1=$00 (clear all enables)
  CR2=$84 (RDA_SUPPRESS_FV | FC_TDRA) -- no PSE, no CLR bits
Then checks FV (bit1) and RDA (bit7):
  - No FV (BEQ) -> error $9737 (not a valid frame end)
  - FV set, no RDA (BPL) -> error $9737 (missing last byte)
  - FV set, RDA set -> read last byte, process scout
After reading the last byte, the complete scout buffer ($0D3D-$0D48)
contains: src_stn, src_net, ctrl, port [, extra_data...].
The port byte at $0D40 determines further processing:
  - Port = 0 -> immediate operation ($9A59)
  - Port non-zero -> check if it matches an open receive block""")

comment(0x9771, "CR1=$00: disable all interrupts", inline=True)
comment(0x9776, "CR2=$84: disable PSE, enable RDA_SUPPRESS_FV", inline=True)
comment(0x977B, "A=$02: FV mask for SR2 bit1", inline=True)
comment(0x977D, "BIT SR2: test FV (Z) and RDA (N)", inline=True)
comment(0x9780, "No FV -- not a valid frame end, error", inline=True)
comment(0x9782, "FV set but no RDA -- missing last byte, error", inline=True)
comment(0x9784, "Read last byte from RX FIFO", inline=True)
comment(0x9787, "Store last byte at $0D3D+Y", inline=True)
comment(0x978A, "CR1=$44: RX_RESET | TIE (switch to TX for ACK)", inline=True)
comment(0x978F, "Check port byte: 0 = immediate op, non-zero = data transfer", inline=True)
comment(0x9792, "Port non-zero -- look for matching receive block", inline=True)
comment(0x9794, "Port = 0 -- immediate operation handler", inline=True)

# ============================================================
# Data RX handler ($9839-$98CE)
# ============================================================
# This handler chain receives the data frame in a four-way handshake.
# After sending the scout ACK, the ROM installs $9839 to receive
# the incoming data frame.
comment(0x9839, """\
Data frame RX handler (four-way handshake)
Receives the data frame after the scout ACK has been sent.
First checks AP (Address Present) for the start of the data frame.
Reads and validates the first two address bytes (dest_stn, dest_net)
against our station address, then installs continuation handlers
to read the remaining data payload into the open port buffer.

Handler chain: $9839 (AP+addr check) -> $984F (net=0 check) ->
$9865 (skip ctrl+port) -> $989A (bulk data read) -> $98CE (completion)""")

comment(0x982D, "CR1=$82: TX_RESET | RIE (switch to RX for data frame)", inline=True)
comment(0x9839, "Read SR2 for AP check", inline=True)
comment(0x984F, "Validate source network = 0", inline=True)
comment(0x9865, "Skip control and port bytes (already known from scout)", inline=True)
comment(0x986A, "Discard control byte", inline=True)
comment(0x986D, "Discard port byte", inline=True)

# ============================================================
# Data frame bulk read ($989A-$98CE)
# ============================================================
comment(0x989A, """\
Data frame bulk read loop
Reads data payload bytes from the RX FIFO and stores them into
the open port buffer at (open_port_buf),Y. Reads bytes in pairs
(like the scout data loop), checking SR2 between each pair.
SR2 non-zero (FV or other) -> frame completion at $98CE.
SR2 = 0 -> RTI, wait for next NMI to continue.""")

comment(0x989A, "Y = buffer offset, resume from last position", inline=True)
comment(0x989C, "Read SR2 for next pair", inline=True)

# ============================================================
# Data frame completion ($98CE-$98F4)
# ============================================================
comment(0x98CE, """\
Data frame completion
Reached when SR2 non-zero during data RX (FV detected).
Same pattern as scout completion ($9771): disables PSE (CR1=$00,
CR2=$84), then tests FV and RDA. If FV+RDA, reads the last byte.
If extra data available and buffer space remains, stores it.
Proceeds to send the final ACK via $995E.""")

comment(0x98CE, "CR1=$00: disable all interrupts", inline=True)
comment(0x98D3, "CR2=$84: disable PSE for individual bit testing", inline=True)
comment(0x98DA, "A=$02: FV mask", inline=True)
comment(0x98DC, "BIT SR2: test FV (Z) and RDA (N)", inline=True)
comment(0x98DF, "No FV -- error", inline=True)
comment(0x98E1, "FV set, no RDA -- proceed to ACK", inline=True)
comment(0x98E7, "FV+RDA: read and store last data byte", inline=True)

# ============================================================
# Scout ACK / Final ACK TX ($995E-$99B5)
# ============================================================
comment(0x995E, """\
ACK transmission
Sends a scout ACK or final ACK frame as part of the four-way handshake.
If bit7 of $0D4A is set, this is a final ACK -> completion ($9F39).
Otherwise, configures for TX (CR1=$44, CR2=$A7) and sends the ACK
frame (dst_stn, dst_net from $0D3D, src_stn from $FE18, src_net=0).
The ACK frame has no data payload -- just address bytes.

After writing the address bytes to the TX FIFO, installs the next
NMI handler from $0D4B/$0D4C (saved by the scout/data RX handler)
and sends TX_LAST_DATA (CR2=$3F) to close the frame.""")

comment(0x9966, "CR1=$44: RX_RESET | TIE (switch to TX mode)", inline=True)
comment(0x996B, "CR2=$A7: RTS|CLR_TX_ST|FC_TDRA|2_1_BYTE|PSE", inline=True)
comment(0x9970, "Install saved next handler ($99BB for scout ACK)", inline=True)
comment(0x997A, "Load dest station from RX scout buffer", inline=True)
comment(0x997D, "BIT SR1: test TDRA (V=bit6)", inline=True)
comment(0x9980, "TDRA not ready -- error", inline=True)
comment(0x9982, "Write dest station to TX FIFO", inline=True)
comment(0x9985, "Write dest network to TX FIFO", inline=True)
comment(0x998B, "Install handler at $9992 (write src addr)", inline=True)

comment(0x9992, """\
ACK TX continuation
Writes source station and network to TX FIFO, completing the 4-byte
ACK frame. Then sends TX_LAST_DATA (CR2=$3F) to close the frame.""")
comment(0x9992, "Load our station ID (also INTOFF)", inline=True)
comment(0x9995, "BIT SR1: test TDRA", inline=True)
comment(0x9998, "TDRA not ready -- error", inline=True)
comment(0x999A, "Write our station to TX FIFO", inline=True)
comment(0x999D, "Write network=0 to TX FIFO", inline=True)
comment(0x99A7, "CR2=$3F: TX_LAST_DATA | CLR_RX_ST | FLAG_IDLE | FC_TDRA | 2_1_BYTE | PSE", inline=True)
comment(0x99AC, "Install saved handler from $0D4B/$0D4C", inline=True)

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
Reads the second byte of the reply scout (destination network) and
validates it is zero (local network). Installs $9DE3 for the
remaining two bytes (source station and network).
Optimisation: checks SR1 bit7 (IRQ still asserted) via BMI at $9DD9.
If IRQ is still set, falls through directly to $9DE3 without an RTI,
avoiding NMI re-entry overhead for short frames where all bytes arrive
in quick succession.""")

comment(0x9DC8, "BIT SR2: test for RDA (bit7 = data available)", inline=True)
comment(0x9DCB, "No RDA -- error", inline=True)
comment(0x9DCD, "Read destination network byte", inline=True)
comment(0x9DD0, "Non-zero -- network mismatch, error", inline=True)
comment(0x9DD2, "Install next handler at $9DE3 (reply validation)", inline=True)
comment(0x9DD6, "BIT SR1: test IRQ (N=bit7) -- more data ready?", inline=True)
comment(0x9DD9, "IRQ set -- fall through to $9DE3 without RTI", inline=True)
comment(0x9DDB, "IRQ not set -- install handler and RTI", inline=True)

# ============================================================
# RX reply validation handler ($9DE3)
# ============================================================
# This is the critical Path 2 code for ADLC FV/PSE interaction.
# The handler reads two bytes (source station and network) and
# then checks for FV. The key requirement is that RDA must be
# visible at $9DE3 even if FV has been latched.
#
# With Beebium's inline refill model, this works because the
# inline refill chain feeds bytes in rapid succession: each FIFO
# read refills the next byte. For a 4-byte reply scout:
#   Read byte 0 at $9DB9 -> refills byte 1 (RDA visible at $9DC8)
#   Read byte 1 at $9DCD -> refills byte 2 (RDA visible at $9DE3)
#   Read byte 2 at $9DE8 -> refills byte 3/LAST (FV set)
#   Read byte 3 at $9DF1 -> FIFO empty
#   Check FV at $9DFA -> FV is set
comment(0x9DE3, """\
RX reply validation (Path 2 for FV/PSE interaction)
Reads the source station and source network from the reply scout and
validates them against the original TX destination ($0D20/$0D21).
Sequence:
  1. Check SR2 bit7 (RDA) at $9DE3 -- must see data available
  2. Read source station at $9DE8, compare to $0D20 (tx_dst_stn)
  3. Read source network at $9DF0, compare to $0D21 (tx_dst_net)
  4. Check SR2 bit1 (FV) at $9DFA -- must see frame complete
If all checks pass, the reply scout is valid and the ROM proceeds
to send the scout ACK (CR2=$A7 for RTS, CR1=$44 for TX mode).""")

comment(0x9DE3, "BIT SR2: test RDA (bit7). Must be set for valid reply.", inline=True)
comment(0x9DE6, "No RDA -- error (FV masking RDA via PSE would cause this)", inline=True)
comment(0x9DE8, "Read source station", inline=True)
comment(0x9DEB, "Compare to original TX destination station ($0D20)", inline=True)
comment(0x9DEE, "Mismatch -- not the expected reply, error", inline=True)
comment(0x9DF0, "Read source network", inline=True)
comment(0x9DF3, "Compare to original TX destination network ($0D21)", inline=True)
comment(0x9DF6, "Mismatch -- error", inline=True)
comment(0x9DF8, "A=$02: FV mask for SR2 bit1", inline=True)
comment(0x9DFA, "BIT SR2: test FV -- frame must be complete", inline=True)
comment(0x9DFD, "No FV -- incomplete frame, error", inline=True)
comment(0x9DFF, "CR2=$A7: RTS|CLR_TX_ST|FC_TDRA|2_1_BYTE|PSE (TX in handshake)", inline=True)
comment(0x9E04, "CR1=$44: RX_RESET | TIE (TX active for scout ACK)", inline=True)
comment(0x9E09, "Install next handler at $9EDD (four-way data phase) into $0D4B/$0D4C", inline=True)
comment(0x9E13, "Load dest station for scout ACK TX", inline=True)
comment(0x9E16, "BIT SR1: test TDRA (V=bit6)", inline=True)
comment(0x9E19, "TDRA not ready -- error", inline=True)
comment(0x9E1B, "Write dest station to TX FIFO", inline=True)
comment(0x9E1E, "Write dest network to TX FIFO", inline=True)
comment(0x9E24, "Install handler at $9E2B (write src addr for scout ACK)", inline=True)

# ============================================================
# TX data phase: write src address ($9E2B)
# ============================================================
comment(0x9E2B, """\
TX scout ACK: write source address
Writes our station ID and network=0 to TX FIFO, completing the
4-byte scout ACK frame. Then proceeds to send the data frame.""")
comment(0x9E2B, "Load our station ID (also INTOFF)", inline=True)
comment(0x9E2E, "BIT SR1: test TDRA", inline=True)
comment(0x9E31, "TDRA not ready -- error", inline=True)
comment(0x9E33, "Write our station to TX FIFO", inline=True)
comment(0x9E36, "Write network=0 to TX FIFO", inline=True)

# ============================================================
# TX data phase: send data payload ($9E50)
# ============================================================
comment(0x9E50, """\
TX data phase: send payload
Sends the data frame payload from (open_port_buf),Y in pairs per NMI.
Same pattern as the NMI TX handler at $9D4C but reads from the port
buffer instead of the TX workspace. Writes two bytes per iteration,
checking SR1 IRQ between pairs for tight looping.""")
comment(0x9E50, "Y = buffer offset, resume from last position", inline=True)
comment(0x9E52, "BIT SR1: test TDRA (V=bit6)", inline=True)
comment(0x9E55, "TDRA not ready -- error", inline=True)
comment(0x9E57, "Write data byte to TX FIFO", inline=True)
comment(0x9E7D, "CR2=$3F: TX_LAST_DATA (close data frame)", inline=True)

# ============================================================
# Four-way handshake: switch to RX for final ACK ($9EDD)
# ============================================================
comment(0x9EDD, """\
Four-way handshake: switch to RX for final ACK
After the data frame TX completes, switches to RX mode (CR1=$82)
and installs $9EE9 to receive the final ACK from the remote station.""")
comment(0x9EDD, "CR1=$82: TX_RESET | RIE (switch to RX for final ACK)", inline=True)
comment(0x9EE2, "Install handler at $9EE9 (RX final ACK)", inline=True)

# ============================================================
# Four-way handshake: RX final ACK ($9EE9-$9F3D)
# ============================================================
# Same pattern as $9DB2/$9DC8/$9DE3 but for the final ACK.
# Validates that the final ACK is from the expected station.
comment(0x9EE9, """\
RX final ACK handler
Receives the final ACK in a four-way handshake. Same validation
pattern as the reply scout handler ($9DB2-$9DE3):
  $9EE9: Check AP, read dest_stn, compare to our station
  $9EFF: Check RDA, read dest_net, validate = 0
  $9F15: Check RDA, read src_stn/net, compare to TX dest
  $9F32: Check FV for frame completion
On success, stores result=0 at $9F39. On any failure, error $41.""")

comment(0x9EE9, "A=$01: AP mask", inline=True)
comment(0x9EEB, "BIT SR2: test AP", inline=True)
comment(0x9EEE, "No AP -- error", inline=True)
comment(0x9EF0, "Read dest station", inline=True)
comment(0x9EF3, "Compare to our station (INTOFF side effect)", inline=True)
comment(0x9EF6, "Not our station -- error", inline=True)
comment(0x9EF8, "Install handler at $9EFF (final ACK continuation)", inline=True)

comment(0x9EFF, "BIT SR2: test RDA", inline=True)
comment(0x9F02, "No RDA -- error", inline=True)
comment(0x9F04, "Read dest network", inline=True)
comment(0x9F07, "Non-zero -- network mismatch, error", inline=True)
comment(0x9F09, "Install handler at $9F15 (final ACK validation)", inline=True)
comment(0x9F0D, "BIT SR1: test IRQ -- more data ready?", inline=True)
comment(0x9F10, "IRQ set -- fall through to $9F15 without RTI", inline=True)

comment(0x9F15, """\
Final ACK validation
Reads and validates src_stn and src_net against original TX dest.
Then checks FV for frame completion.""")
comment(0x9F15, "BIT SR2: test RDA", inline=True)
comment(0x9F18, "No RDA -- error", inline=True)
comment(0x9F1A, "Read source station", inline=True)
comment(0x9F1D, "Compare to TX dest station ($0D20)", inline=True)
comment(0x9F20, "Mismatch -- error", inline=True)
comment(0x9F22, "Read source network", inline=True)
comment(0x9F25, "Compare to TX dest network ($0D21)", inline=True)
comment(0x9F28, "Mismatch -- error", inline=True)
comment(0x9F32, "A=$02: FV mask for SR2 bit1", inline=True)
comment(0x9F34, "BIT SR2: test FV -- frame must be complete", inline=True)
comment(0x9F37, "No FV -- error", inline=True)

# ============================================================
# Completion and error handlers ($9F39-$9F48)
# ============================================================
comment(0x9F39, """\
TX completion handler
Stores result code 0 (success) into the first byte of the TX control
block (nmi_tx_block),Y=0. Then sets $0D3A bit7 to signal completion
and calls full ADLC reset + idle listen via $9A34.""")
comment(0x9F39, "A=0: success result code", inline=True)
comment(0x9F3B, "BEQ: always taken (A=0)", inline=True)

comment(0x9F3F, """\
TX error handler
Stores error code (A) into the TX control block, sets $0D3A bit7
for completion, and returns to idle via $9A34.
Error codes: $00=success, $40=line jammed, $41=not listening,
$42=net error.""")
comment(0x9F3F, "Y=0: index into TX control block", inline=True)
comment(0x9F41, "Store result/error code at (nmi_tx_block),0", inline=True)
comment(0x9F43, "$80: completion flag for $0D3A", inline=True)
comment(0x9F45, "Signal TX complete", inline=True)
comment(0x9F48, "Full ADLC reset and return to idle listen", inline=True)

# ============================================================
# Generate disassembly and split into section files
# ============================================================

output = go()

import re, os

script_dirpath = os.path.dirname(os.path.abspath(__file__))
base = "nfs_334_v2"

# Section boundaries: (start_addr, label)
# Each section covers start_addr up to (but not including) the next section's start_addr.
section_breaks = [
    (0x8000, "8000_84ff_header_dispatch_init"),
    (0x8500, "8500_8dff_filing_system"),
    (0x8E00, "8e00_96db_cli_and_commands"),
    (0x96DC, "96dc_9fff_adlc_nmi_handlers"),
]

addr_re = re.compile(r';\s*([0-9a-f]{4}):', re.IGNORECASE)

lines = output.rstrip('\n').split('\n')

# Phase 1: split into preamble, code, appendix
preamble = []
code = []
appendix = []
phase = 'preamble'

for line in lines:
    if phase == 'preamble':
        preamble.append(line)
        if line.strip().startswith('org '):
            preamble.append('')  # blank line after org
            phase = 'code'
    elif phase == 'code':
        if line.startswith('.pydis_end') or line.startswith('save ') or \
           line.startswith('; Label references'):
            appendix.append(line)
            phase = 'appendix'
        else:
            code.append(line)
    else:
        appendix.append(line)

# Phase 2: split code lines by address into sections
section_contents = {label: [] for _, label in section_breaks}
current_label = section_breaks[0][1]
pending = []  # lines without addresses, buffered until next addressed line

def section_for_addr(addr):
    """Return the section label for a given address."""
    result = section_breaks[0][1]
    for break_addr, label in section_breaks:
        if addr >= break_addr:
            result = label
    return result

for line in code:
    m = addr_re.search(line)
    if m:
        addr = int(m.group(1), 16)
        new_label = section_for_addr(addr)
        if new_label != current_label:
            # Flush pending to OLD section if they're trailing blanks/comments,
            # or to NEW section if they look like a block comment for the new code.
            # Heuristic: if pending ends with blank lines only, keep with old section.
            # If pending contains non-blank comment lines, move to new section.
            has_content = any(p.strip() for p in pending)
            if has_content:
                section_contents[new_label].extend(pending)
            else:
                section_contents[current_label].extend(pending)
            current_label = new_label
            pending = []
        else:
            section_contents[current_label].extend(pending)
            pending = []
        section_contents[current_label].append(line)
    else:
        pending.append(line)

# Flush remaining pending
if pending:
    section_contents[current_label].extend(pending)

# Phase 3: write files
written_filenames = []

# Preamble
fn = f"{base}_preamble.asm"
with open(os.path.join(script_dirpath, fn), 'w') as f:
    f.write('\n'.join(preamble) + '\n')
written_filenames.append(fn)

# Code sections
for _, label in section_breaks:
    if section_contents[label]:
        fn = f"{base}_{label}.asm"
        with open(os.path.join(script_dirpath, fn), 'w') as f:
            f.write('\n'.join(section_contents[label]) + '\n')
        written_filenames.append(fn)

# Appendix
fn = f"{base}_appendix.asm"
with open(os.path.join(script_dirpath, fn), 'w') as f:
    f.write('\n'.join(appendix) + '\n')
written_filenames.append(fn)

# Master file with INCLUDEs
master_filepath = os.path.join(script_dirpath, f"{base}.asm")
with open(master_filepath, 'w') as f:
    for fn in written_filenames:
        f.write(f'INCLUDE "{fn}"\n')

# Report
print(f"Split into {len(written_filenames)} files:", file=__import__('sys').stderr)
for fn in written_filenames:
    filepath = os.path.join(script_dirpath, fn)
    n = sum(1 for _ in open(filepath))
    print(f"  {fn}: {n} lines", file=__import__('sys').stderr)
