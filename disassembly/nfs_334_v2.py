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

# Tube ULA registers ($FEE0-$FEE7) — named by acorn.bbc()
# R1 ($FEE0/$FEE1): events and escape signalling (host↔parasite)
# R2 ($FEE2/$FEE3): command bytes and general data transfer
# R4 ($FEE6/$FEE7): one-byte transfers (error codes, BRK signalling)

# Key ADLC register values (from original disassembly annotations):
#   CR1=$C1: full reset (TX_RESET|RX_RESET|AC)
#   CR1=$82: RX listen (TX_RESET|RIE)
#   CR1=$44: TX active (RX_RESET|TIE)
#   CR2=$67: clear all status (CLR_TX_ST|CLR_RX_ST|FC_TDRA|2_1_BYTE|PSE)
#   CR2=$E7: TX prepare (RTS|CLR_TX_ST|CLR_RX_ST|FC_TDRA|2_1_BYTE|PSE)
#   CR2=$3F: TX last data (CLR_RX_ST|TX_LAST_DATA|FLAG_IDLE|FC_TDRA|2_1_BYTE|PSE)
#   CR2=$A7: TX in handshake (RTS|CLR_TX_ST|FC_TDRA|2_1_BYTE|PSE)

# ============================================================
# Protocol constants from DNFS 3.60 reference (NFS00)
# ============================================================

# Econet port numbers (FS protocol version 2)
# Ports are allocated above $B0 to leave $01-$AA free for user applications.
# The low 3 bits of the TXCB flag byte encode error reasons and index into
# the error message table (ERRTAB) at $84AF.
constant(0x99, "port_command")        # PCMND: command port
constant(0x90, "port_reply")          # PREPLY: reply port
constant(0x91, "port_save_ack")       # PSAACK: save acknowledge port
constant(0x92, "port_load_data")      # PLDATA: load data port
constant(0x93, "port_remote")         # PREMOT: remote operation port
constant(0xD1, "port_printer")        # PBLOCK: printer block port

# Econet error codes
constant(0xA0, "err_line_jammed")     # LINJAM
constant(0xA1, "err_net_error")       # NETER
constant(0xA2, "err_not_listening")   # NOLISE
constant(0xA3, "err_no_clock")        # NOCLK
constant(0xA4, "err_tx_cb_error")     # TXCBER: TX control block error
constant(0xA5, "err_no_reply")        # NOREPE: no reply from server
constant(0xA8, "err_fs_cutoff")       # ERRCUT: FS errors >= this are FS-specific

# Protocol flags
constant(0x80, "tx_flag")             # TXFLAG: OR with flag byte before transmit
constant(0x7F, "rx_ready")            # RXRDY: flag byte value for reception ready

# FS handle base and retry count
constant(0x20, "handle_base")         # HAND: base value for file handles ($20-$27)

# Control block template markers
constant(0xFE, "cb_stop")             # CBSTOP: stop copying in ctrl_block_setup
constant(0xFD, "cb_skip")             # CBSKIP: skip this byte (leave unchanged)
constant(0xFC, "cb_fill")             # CBFILL: substitute page byte of pointer

# ============================================================
# Inline string subroutine hook
# ============================================================
# print_inline ($853B) prints an inline string following the JSR, terminated by a
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
# NFS zero-page layout from DNFS 3.60 source (NFS00):
#   $B0-$B3  WORK    4-byte variable workspace (load/exec/size)
#   $B4-$B7  (WORK+4) additional workspace (address compare target)
#   $B8      JWORK   3 bytes for timing (also CRFLAG at $B9, SPOOL1 at $BA)
#   $BB      TEMPX   register save: X (also used as options pointer low)
#   $BC      TEMPY   register save: Y (also options pointer high)
#   $BD      TEMPA   register save: A (also last-byte flag)
#   $BE-$BF  POINTR  generic 2-byte pointer (filename, workspace)
#   $C0-$CE  TXCB/RXCB overlapping TX/RX control blocks (15 bytes)
#   $CF      SPOOL0  spool file handle
# NB: $CD-$CE noted as "two bytes free" in original source.

label(0xB0, "fs_load_addr")        # WORK: load/start address (4 bytes)
label(0xB1, "fs_load_addr_hi")
label(0xB2, "fs_load_addr_2")
label(0xB8, "fs_error_ptr")        # JWORK: error pointer / timing workspace
label(0xBB, "fs_options")          # TEMPX: options/control block pointer (low)
label(0xBC, "fs_block_offset")     # TEMPY: block offset / control block pointer (high)
label(0xBD, "fs_last_byte_flag")   # TEMPA: b7=last byte from block / saved A
label(0xBE, "fs_crc_lo")           # POINTR: generic pointer (low)
label(0xBF, "fs_crc_hi")           # POINTR+1: generic pointer (high)
label(0xCD, "fs_temp_cd")          # Free byte, used as temporary by NFS 3.34
label(0xCE, "fs_temp_ce")          # Free byte, used as temporary by NFS 3.34

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
label(0x0D51, "tx_work_51")         # TX workspace (possibly retry count or flags)
label(0x0D57, "tx_work_57")         # TX workspace (possibly timeout or state)

# RX/status
label(0x0D07, "nmi_shim_07")        # NMI shim workspace
label(0x0D38, "rx_status_flags")    # RX status/mode flags
label(0x0D3B, "rx_ctrl_copy")       # Copy of received control byte
label(0x0D52, "tx_in_progress")     # Referenced by NMI handlers
label(0x0D5C, "scout_status")       # Scout/packet status indicator
label(0x0D5D, "rx_extra_byte")      # Extra byte read after data frame completion

# Econet state ($0D60-$0D67) — reference: NFS00 PBUFFP-TBFLAG
label(0x0D61, "printer_buf_ptr")    # PBUFFP: printer buffer pointer
label(0x0D62, "tx_clear_flag")      # TXCLR: b7=Transmission in progress
label(0x0D63, "prot_status")        # LSTAT: protection mask — b0=PEEK, b1=POKE, b2=HALT, b3=JSR, b4=PROC
label(0x0D64, "rx_flags")           # LFLAG: b7=system Rx (into page-zero CB), b6=user Rx, b2=Halted
label(0x0D65, "saved_jsr_mask")     # OLDJSR: old copy of JSR buffer protection bits
label(0x0D66, "econet_init_flag")   # b7=Econet using NMI code ($00=no, $80=yes)
label(0x0D67, "tube_flag")          # TBFLAG: b7=Tube present ($00=no, $FF=yes)

# ============================================================
# Page $0E — Filing system context (reference: NFS00 FSLOCN-CMNDP)
# ============================================================
# Layout from the original source (NFS00):
#   $0E00  FSLOCN   File server station (2 bytes: station + network)
#   $0E02  URD      User root directory handle
#   $0E03  CSD      Current selected directory handle
#   $0E04  LIB      Library directory handle
#   $0E05  OPT      AUTOBOOT option number (*OPT 4,n)
#   $0E06  MESS     Messages on/off flag
#   $0E07  EOF      End-of-file flags
#   $0E08  SEQNOS   Byte stream sequence numbers
#   $0E09  ERROR    Slot for last unknown error code
#   $0E0A  SPARE    (also JCMNDP)
#   $0E0D  RSTAT    Status byte
#   $0E0E  TARGET   Target station (2 bytes)
#   $0E10  CMNDP    Pointer to rest of command line
label(0x0E00, "fs_server_stn")      # FSLOCN: file server station number
label(0x0E01, "fs_server_net")      # FSLOCN+1: file server network number
label(0x0E02, "fs_urd_handle")      # URD: user root directory handle
label(0x0E03, "fs_csd_handle")      # CSD: current selected directory handle
label(0x0E04, "fs_lib_handle")      # LIB: library directory handle
label(0x0E05, "fs_boot_option")     # OPT: AUTOBOOT option number
label(0x0E06, "fs_messages_flag")   # MESS: messages on/off flag
label(0x0E07, "fs_eof_flags")       # EOF: end-of-file flags
label(0x0E08, "fs_sequence_nos")    # SEQNOS: byte stream sequence numbers
label(0x0E09, "fs_last_error")      # ERROR: slot for last unknown error code
label(0x0E0A, "fs_cmd_context")     # SPARE/JCMNDP: command context
label(0x0E0D, "fs_reply_status")    # RSTAT: b0=remote ok, b1=view ok, b2=notify ok, b3=remoted, b7=FS selected
label(0x0E0E, "fs_target_stn")      # TARGET: target station for remote ops (2 bytes)
label(0x0E10, "fs_cmd_ptr")         # CMNDP: pointer to rest of command line

# Other workspace used by NFS
# Relocated code — Tube host zero-page code (BRKV = $0016)
# Reference: NFS11 (NEWBR, TSTART, MAIN)
label(0x0016, "tube_brk_handler")    # BRKV target: send error to Tube via R2, then enter main loop
label(0x0029, "tube_brk_send_loop")  # Loop: send error message bytes via R2 until zero terminator
label(0x0032, "tube_reset_stack")    # Reset SP=$FF, CLI, fall through to main loop
label(0x003A, "tube_main_loop")      # Poll R1 (WRCH) and R2 (commands), dispatch via table
label(0x003F, "tube_handle_wrch")    # R1 data ready: read byte, call OSWRITCH ($FFCB)
label(0x0045, "tube_poll_r2")        # Poll R2 status; if ready, read command and dispatch
label(0x0054, "tube_dispatch_cmd")   # JMP ($0500) — dispatch to handler via table
label(0x0057, "tube_transfer_addr")  # 4-byte transfer start address (written by address claim)
entry(0x0016)
entry(0x0032)
entry(0x003A)

# Relocated code — page 4 (Tube address claim, RDCH, data transfer)
# Reference: NFS12 (BEGIN, ADRR, SENDW, TADDR, SETADR)
label(0x0400, "tube_code_page4")     # Tube host code page 4 (copied from ROM at $934C)
label(0x0403, "tube_escape_entry")   # JMP to tube_escape_check ($06E2)
label(0x0406, "tube_addr_claim")     # Tube address claim protocol (ADRR in reference)
label(0x0414, "tube_post_init")      # Called after ROM→RAM copy; initial Tube setup
label(0x04E0, "tube_setup_transfer")  # Set Y=0, X=$57 (tube_transfer_addr), JMP tube_addr_claim
label(0x04E7, "tube_rdch_handler")   # RDCHV target — send $01 via R2, enter main loop
label(0x04EF, "tube_restore_regs")   # Restore X,Y from $10/$11 (dispatch entry 6)
label(0x04F7, "tube_read_r2")        # Wait for R2 data ready, read byte to A
entry(0x0400)
entry(0x0403)
entry(0x0406)
entry(0x0414)
entry(0x04E0)
entry(0x04E7)
entry(0x04EF)
entry(0x04F7)

# Relocated code — page 5 (Tube dispatch table, WRCH, file I/O handlers)
# Reference: NFS13 (TASKS table, BPUT, BGET, RDCHZ, FIND, ARGS, STRNG, CLI, FILE)
#
# $0500-$051B: 14-entry dispatch table of word addresses.
# JMP ($0500) at $0054 dispatches Tube commands; the address claim
# protocol at $0406 patches $0500-$0501 with the target handler address.
#
# Tube cmd  Entry  Addr   Handler
# ────────  ─────  ─────  ─────────────────────────────
#   $00       0    $055B  tube_osrdch (OSRDCH)
#   $01       1    $05C5  tube_oscli (OSCLI)
#   $02       2    $0626  tube_osbyte_short (2-param, X result)
#   $03       3    $063B  tube_osbyte_long (3-param, X+Y results)
#   $04       4    $065D  tube_osword (variable-length)
#   $05       5    $06A3  tube_osword_rdln (OSWORD 0, read line)
#   $06       6    $04EF  tube_restore_regs (release/no-op)
#   $07       7    $053D  tube_release_return (restore regs, RTS)
#   $08       8    $058C  tube_osargs (OSARGS)
#   $09       9    $0550  tube_osbget (OSBGET)
#   $0A      10    $0543  tube_osbput (OSBPUT)
#   $0B      11    $0569  tube_osfind (OSFIND open)
#   $0C      12    $05D8  tube_osfile (OSFILE)
#   $0D      13    $0602  tube_osgbpb (OSGBPB)
label(0x0500, "tube_dispatch_table")  # 14-entry handler address table
label(0x051C, "tube_wrch_handler")    # WRCHV target — write character via Tube
label(0x051F, "tube_send_and_poll")   # Send byte via R2, poll R2/R1 for reply
label(0x0527, "tube_poll_r1_wrch")    # Service R1 WRCH requests while waiting for R2
label(0x0532, "tube_resume_poll")     # JMP back to R2 poll loop after servicing R1
label(0x053D, "tube_release_return")  # Restore X,Y from $10/$11, PLA, RTS
label(0x0543, "tube_osbput")          # OSBPUT: read channel+byte from R2, call $FFD4
label(0x0550, "tube_osbget")          # OSBGET: read channel from R2, call $FFD7
label(0x055B, "tube_osrdch")          # OSRDCH: call $FFC8, send carry+byte reply
label(0x0561, "tube_rdch_reply")      # Send carry in bit 7 + data byte as reply
label(0x0569, "tube_osfind")          # OSFIND open: read arg+filename, call $FFCE
label(0x0580, "tube_osfind_close")    # OSFIND close: read handle, call $FFCE with A=0
label(0x058C, "tube_osargs")          # OSARGS: read handle+4 bytes+reason, call $FFDA
label(0x0590, "tube_read_params")     # Read parameter bytes from R2 into zero page
label(0x05B1, "tube_read_string")     # Read CR-terminated string from R2 into $0700
label(0x05C5, "tube_oscli")           # OSCLI: read command string, call $FFF7
label(0x05CB, "tube_reply_ack")       # Send $7F acknowledge, return to main loop
label(0x05CD, "tube_reply_byte")      # Poll R2, send byte in A, return to main loop
label(0x05D8, "tube_osfile")          # OSFILE: read 16 params+filename+reason, call $FFDD
entry(0x051C)
# Dispatch table entry points
for addr in [0x055B, 0x05C5, 0x0626, 0x063B, 0x065D, 0x06A3,
             0x04EF, 0x053D, 0x058C, 0x0550, 0x0543, 0x0569,
             0x05D8, 0x0602]:
    entry(addr)

# Relocated code — page 6 (OSGBPB, OSBYTE, OSWORD, RDLN, event handler)
# Reference: NFS13 (GBPB, SBYTE, BYTE, WORD, RDLN, RDCHA, WRIFOR, ESCAPE, EVENT, ESCA)
label(0x0600, "tube_code_page6")      # Tube host code page 6 (copied from ROM at $954C)
label(0x0602, "tube_osgbpb")          # OSGBPB: read 13 params+reason, call $FFD1
label(0x0626, "tube_osbyte_short")    # OSBYTE 2-param: read X+A, call $FFF4, return X
label(0x0630, "tube_osbyte_send_x")   # Poll R2, send X result
label(0x063B, "tube_osbyte_long")     # OSBYTE 3-param: read X+Y+A, call $FFF4, return carry+Y+X
label(0x0653, "tube_osbyte_send_y")   # Poll R2, send Y result, then fall through to send X
label(0x065D, "tube_osword")          # OSWORD variable-length: read A+params, call $FFF1
label(0x0661, "tube_osword_read")     # Poll R2 for param block length, read params
label(0x066C, "tube_osword_read_lp")  # Read param block bytes from R2 into $0130
label(0x0692, "tube_osword_write")    # Write param block bytes from $0130 back to R2
label(0x0695, "tube_osword_write_lp") # Poll R2, send param block byte
label(0x06A0, "tube_return_main")     # JMP tube_main_loop
label(0x06A3, "tube_osword_rdln")     # OSWORD 0 (read line): read 5 params, call $FFF1
label(0x06BB, "tube_rdln_send_line")  # Send input line bytes from $0700 back to Tube
label(0x06C2, "tube_rdln_send_loop")  # Load byte from $0700+X
label(0x06C5, "tube_rdln_send_byte")  # Send byte via R2, loop until CR
label(0x06E2, "tube_escape_check")    # Check $FF escape flag, forward to Tube via R1
label(0x06E8, "tube_event_handler")   # EVNTV target: forward event (A,X,Y) to Tube via R1
label(0x06F7, "tube_send_r1")         # Poll R1 status, write A to R1 data (ESCA in reference)
entry(0x0600)
entry(0x0602)
entry(0x0626)
entry(0x063B)
entry(0x065D)
entry(0x06A3)
entry(0x06E2)
entry(0x06E8)
entry(0x06F7)
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
label(0x8E6A, "read_args_size")        # READRB: get args buffer size from RX block offset $7F
label(0x8E7B, "osword_12_handler")    # OSWORD $12: read/write FS server station and config
label(0x8EF0, "osword_10_handler")    # OSWORD $10: allocate RX slot, copy FS command

# Econet TX/RX handler and OSWORD dispatch
label(0x8F57, "setup_rx_buffer_ptrs") # Set up RX buffer address pointers in workspace
label(0x8F68, "store_16bit_at_y")     # Store 16-bit value at (nfs_workspace)+Y
label(0x8F72, "econet_tx_rx")          # Main TX/RX handler (A=0: send, A>=1: result)
label(0x9007, "osword_dispatch")       # OSWORD-style function dispatch (codes 0-8)
label(0x9020, "osword_trampoline")     # PHA/PHA/RTS trampoline
label(0x902B, "osword_tbl_lo")         # Dispatch table low bytes
label(0x9034, "osword_tbl_hi")         # Dispatch table high bytes
label(0x903D, "osword_fn4")            # Function 4: propagate carry
label(0x904B, "setup_tx_and_send")    # Set up TX ctrl block at ws+$0C and transmit

# Remote operation function handlers (dispatched via osword_tbl)
label(0x903D, "net_write_char")       # NWRCH: Fn 4, write char to screen (zeroes stacked carry)
label(0x9063, "remote_cmd_dispatch")  # Fn 7: dispatch received remote OSBYTE/OSWORD
label(0x90B5, "match_osbyte_code")   # NCALLP: compare A against OSBYTE function table; Z=1 on match
label(0x90CD, "remote_cmd_data")      # Fn 7/8: copy received command data to workspace
label(0x90FC, "remote_boot_handler")  # Remote boot: set up, download, execute at $0100
label(0x912A, "execute_at_0100")      # Zero $0100-$0102, JMP $0100
label(0x913A, "remote_validated")     # Remote op with source address validation
label(0x914A, "insert_remote_key")    # Insert char from RX block into keyboard buffer

# Control block setup
label(0x9159, "ctrl_block_setup_alt")  # Alternate entry into control block setup
label(0x9162, "ctrl_block_setup")     # Main entry: X=$1A, Y=$17, V=0 (nfs_workspace)
label(0x9166, "ctrl_block_setup_clv") # CLV entry: same setup but clears V flag
label(0x918E, "ctrl_block_template")  # Template table for control block initialisation

# Remote printer and display handlers (fn 1/2/3/5)
label(0x91B5, "remote_display_setup") # Fn 5: set up remote display/character position
label(0x91C7, "remote_print_handler") # Fn 1/2/3: handle received chars for remote printing
label(0x91EC, "store_output_byte")    # Store byte at (net_rx_ptr) + current output offset
label(0x9217, "flush_output_block")   # Send current output block and reset buffer

# Network transmit
label(0x9248, "econet_tx_retry")      # Transmit with retry until accepted or timeout

# JSR buffer protection
label(0x92D6, "clear_jsr_protection") # CLRJSR: reset JSR buffer protection bits (4 refs)

# Palette/VDU state save
label(0x9291, "save_palette_vdu")     # Save all 16 palette entries + VDU state
label(0x92DD, "save_vdu_state")       # Save cursor pos ($0355) and 2 VDU OSBYTE values to workspace
label(0x92EE, "read_vdu_osbyte_x0")  # Read next VDU OSBYTE with X=0 parameter
label(0x92F0, "read_vdu_osbyte")     # Read next OSBYTE from table, store result in workspace

# ADLC initialisation and state management
label(0x966F, "adlc_init")           # Full init: reset ADLC, read station ID, install NMI shim
label(0x9681, "adlc_init_workspace") # Init workspace: copy NMI shim, set station ID
label(0x969D, "save_econet_state")   # Save status/protection/tube to RX block offsets 8-10
label(0x96B4, "restore_econet_state")# Restore status/protection/tube from RX block
label(0x96CD, "install_nmi_shim")    # Copy 32-byte NMI shim from ROM to $0D00

# Tube co-processor I/O subroutines (in relocated page 6)
# Reference: RDCHA (R2 write), WRIFOR (R4 write), ESCA (R1 write)
label(0x06D0, "tube_send_r2")       # Poll R2 status, write A to R2 data (RDCHA in reference)
label(0x06D9, "tube_send_r4")       # Poll R4 status, write A to R4 data (WRIFOR in reference)

# ============================================================
# Service call handler labels ($8000-$84FF)
# ============================================================
# Service call numbers and their dispatch table indices:
#   svc 0  → index 1  → return_2 (no-op)
#   svc 1  → index 2  → svc_abs_workspace ($826F)
#   svc 2  → index 3  → svc_private_workspace ($8278)
#   svc 3  → index 4  → svc_autoboot ($81D1)
#   svc 4  → index 5  → svc_star_command ($8172)
#   svc 5  → index 6  → svc_unknown_irq ($966C) → JMP c9b52
#   svc 6  → index 7  → return_2 (BRK — no action)
#   svc 7  → index 8  → dispatch_net_cmd ($8069) (unrecognised OSBYTE)
#   svc 8  → index 9  → fs_osword_dispatch ($8DF7) (unrecognised OSWORD)
#   svc 9  → index 10 → svc_help ($81BC)
#   svc 10 → index 11 → return_2 (no action)
#   svc 11 → index 12 → svc_nmi_claim ($9669) → JMP restore_econet_state
#   svc 12 → index 13 → svc_nmi_release ($9666) → JMP save_econet_state
#
# Special service handling (outside dispatch table):
#   svc $12 (18) with Y=5 → select_nfs ($8184)
#   svc $FE → Tube init (explode character definitions)
#   svc $FF → init_vectors_and_copy ($80C8)

label(0x80AE, "return_1")
label(0x8145, "return_2")
label(0x8275, "return_3")

# --- Service call handlers ---
label(0x826F, "svc_abs_workspace")      # Svc 1: claim absolute workspace up to page $10
label(0x8278, "svc_private_workspace")  # Svc 2: claim private workspace and init NFS
label(0x81D1, "svc_autoboot")           # Svc 3: auto-boot (print station info, set up FS)
label(0x8172, "svc_star_command")       # Svc 4: unrecognised *-command (handles *ROFF, *NET)
label(0x81BC, "svc_help")               # Svc 9: *HELP (print "NFS 3.34")

# --- Trampoline JMPs near ADLC init ($9660-$966C) ---
label(0x9660, "trampoline_tx_setup")    # JMP c9be4 (TX control block setup)
label(0x9663, "trampoline_adlc_init")   # JMP adlc_init ($966F)
label(0x9666, "svc_nmi_release")        # Svc 12: JMP save_econet_state ($969D)
label(0x9669, "svc_nmi_claim")          # Svc 11: JMP restore_econet_state ($96B4)
label(0x966C, "svc_unknown_irq")        # Svc 5: JMP c9b52 (unknown interrupt handler)
entry(0x9660)
entry(0x9663)

# --- Init and vector setup ---
label(0x80C8, "init_vectors_and_copy")  # Svc $FF: set up WRCHV/RDCHV/BRKV/EVNTV, copy code to RAM
label(0x8184, "select_nfs")             # Svc $12 with Y=5: select NFS as active filing system
label(0x8217, "setup_fs_vectors")       # Copy FS vector addresses, set up ROM pointer table
label(0x824D, "fs_vector_addrs")        # 14-byte table: FILEV-FSCV extended vector addresses

# --- FSCV handler and dispatch ---
# FSCV ($808C) dispatches via secondary indices 19-26:
#   FSCV 0 (*OPT)               → index 19 → opt_handler ($89A1)
#   FSCV 1 (EOF)                → index 20 → eof_handler ($881F)
#   FSCV 2 (*/ run)             → index 21 → fscv_star_handler (match known FS commands)
#   FSCV 3 (unrecognised *)     → index 22 → fscv_star_handler (match known FS commands)
#   FSCV 4 (*RUN)               → index 23 → fscv_star_handler (match known FS commands)
#   FSCV 5 (*CAT)               → index 24 → cat_handler ($8BFD)
#   FSCV 6 (shut down)          → index 25 → fscv_shutdown ($82FD)
#   FSCV 7 (read handles/info)  → index 26 → fscv_read_handles ($85DA)
#
# Extended dispatch table entries (indices 27-36):
# These appear to be used by FS reply processing and *NET sub-commands.
#   index 27 → print_dir_name ($8D73)        (print directory path)
#   index 28 → copy_handles_and_boot ($8D1F) (copy handles + run boot command)
#   index 29 → copy_handles ($8D20)          (copy handles only)
#   index 30 → set_csd_handle ($8CFC)        (update CSD handle)
#   index 31 → notify_and_exec ($8D84)       (send FS notify, execute response)
#   index 32 → set_lib_handle ($8CF7)        (update library handle)
#
# *NET sub-commands (base Y=$20, indices 33-36):
#   *NET1 → index 33 → net1_read_handle ($8DAF)
#   *NET2 → index 34 → net2_read_handle_entry ($8DC9)
#   *NET3 → index 35 → net3_close_handle ($8DDF)
#   *NET4 → index 36 → net4_resume_remote ($8DF2)
# --- Filing system vector entry points ---
# Extended vector table entries set up at init ($82E5):
#   FILEV → $8694    ARGSV → $88E1    BGETV → $8485
#   BPUTV → $83A2    GBPBV → $89EA    FINDV → $8949
#   FSCV  → $808C
# Labels and entry points for FSCV, FILEV, ARGSV, FINDV, GBPBV
# are created by subroutine() calls below in the comment sections.
label(0x8485, "bgetv_handler")          # BGETV entry: SEC then JSR handle_bput_bget
label(0x83A2, "bputv_handler")          # BPUTV entry: CLC then fall into handle_bput_bget
entry(0x8485)
entry(0x83A2)

# --- Helper routines in header/init section ---
label(0x81CC, "call_fscv_shutdown")     # LDA #6; JMP (FSCV) — notify FS of shutdown
label(0x819B, "match_rom_string")       # Match command text against ROM string at $8008+X
label(0x81AC, "cmd_name_matched")       # MATCH2: full name matched, check terminator byte
label(0x81B3, "skip_cmd_spaces")         # SKPSP: skip spaces in command text; Z=1 if CR follows
label(0x822E, "issue_vectors_claimed")  # OSBYTE $8F: issue service $0F (vectors claimed)
label(0x82D1, "setup_rom_ptrs_netv")    # Read ROM pointer table, set up NETV
label(0x82E5, "store_rom_ptr_pair")     # Write 2-byte address + ROM bank to ROM pointer table
label(0x82FD, "fscv_shutdown")          # FSCV 6: save FS state to workspace, go dormant

# --- TX control block and FS command setup ---
label(0x830E, "init_tx_ctrl_data")      # Init TX control block for data port ($90)
label(0x8310, "init_tx_ctrl_port")      # Init TX control block with port in A (2 JSR refs)
label(0x831C, "init_tx_ctrl_block")     # Init TX control block from template at $8334
label(0x8334, "tx_ctrl_template")       # 12-byte TX control block template
label(0x8340, "prepare_cmd_with_flag")  # Prepare FS command with '*' flag and carry set
label(0x8346, "prepare_cmd_clv")        # Prepare FS command with V cleared
# prepare_fs_cmd and build_send_fs_cmd labels created by subroutine() calls below.
label(0x8351, "prepare_fs_cmd_v")       # Prepare FS command, V flag preserved
label(0x8380, "send_fs_reply_cmd")      # Send FS command with reply processing

# --- Byte I/O and escape ---
# handle_bput_bget label created by subroutine() call below.
label(0x83CB, "store_retry_count")      # RAND1: store retry count to $0FDD, initiate TX
label(0x8402, "store_fs_error")         # FSERR: save error number to fs_last_error
label(0x841B, "update_sequence_return") # RAND3: update sequence numbers and pull A/Y/X/return
label(0x842C, "set_listen_offset")      # NLISN2: use reply code as table offset for listen
label(0x8448, "send_to_fs_star")        # Send '*' command to fileserver
label(0x844A, "send_to_fs")             # Send command to fileserver and handle reply loop
label(0x8470, "fs_wait_cleanup")        # WAITEX: tidy stack, restore rx_status_flags
label(0x847A, "check_escape")           # Check and handle escape condition

# --- Pointer arithmetic helpers ---
label(0x84A4, "add_5_to_y")             # INY * 5; RTS
label(0x84A5, "add_4_to_y")             # INY * 4; RTS
label(0x84AA, "sub_4_from_y")           # DEY * 4; RTS
label(0x84AB, "sub_3_from_y")           # DEY * 3; RTS

# --- Error messages and data tables ---
label(0x84AF, "error_msg_table")        # Econet error message strings (8 entries)
label(0x8146, "resume_after_remote")    # Resume after remote op: re-enable keyboard, send fn $0A
label(0x815C, "clear_osbyte_ce_cf")     # Reset OSBYTE $CE/$CF intercept masks to $7F (restore MOS vectors)

# --- * command forwarding and BYE ---
label(0x8079, "forward_star_cmd")       # Forward unrecognised * command to fileserver
label(0x8349, "bye_handler")            # *BYE: close spool/exec files, fall into prepare_fs_cmd

# --- Page $0F workspace (FS command buffer) ---
# NFS00 layout: BIGBUF=$0F00, TXBUF/RXBUF=$0F05, RXBUFE=$0FFF
#   $0F00 HDRREP: reply header / command type
#   $0F01 HDRFN:  function code
#   $0F02 HDRURD: URD handle slot
#   $0F03 HDRCSD/RXCC: CSD slot / RX control code
#   $0F04 HDRLIB/RXRC: LIB slot / RX return code
#   $0F05 TXBUF/RXBUF: start of TX/RX data area
#   $0FDC PUTB/PUTB1: single-byte random access buffer (4 bytes)
#   $0FDD PUTB2/GETB2: shared GET/PUT byte workspace
label(0x0F00, "fs_cmd_type")            # HDRREP: reply header / command type
label(0x0F01, "fs_cmd_y_param")         # HDRFN: function code
label(0x0F02, "fs_cmd_urd")             # HDRURD: URD handle slot
label(0x0F03, "fs_cmd_csd")             # HDRCSD: CSD handle / RX control code
label(0x0F04, "fs_cmd_lib")             # HDRLIB/RXRC: LIB slot / RX return code
label(0x0F05, "fs_cmd_data")            # TXBUF/RXBUF: start of TX/RX data area
label(0x0FDC, "fs_putb_buf")            # PUTB: single-byte random access buffer (4 bytes)
label(0x0FDD, "fs_getb_buf")            # PUTB2/GETB2: shared GET/PUT byte workspace

# ============================================================
# Filing system protocol client ($8500-$86FF)
# ============================================================
# Core routines shared by all FS commands: argument saving,
# file handle conversion, number parsing/printing, TX/RX,
# file info display, and attribute decoding.

# --- Argument save and file handle conversion ---
label(0x8508, "save_fscv_args")         # Store A→$BD, X→$BB/$BE, Y→$BC/$BF (6 refs)
label(0x8513, "decode_attribs_6bit")    # Read attribute byte at offset $0E, mask 6 bits
label(0x851D, "decode_attribs_5bit")    # Mask A to 5 bits, build access bitmask
label(0x8530, "access_bit_table")       # Lookup table for attribute bit mapping (11 bytes)
label(0x8555, "skip_spaces")            # Skip leading spaces in (fs_options),Y; sets C if >= 'A'

# --- Decimal number parser ($8560-$8587, undecoded code region) ---
# Reads ASCII digits from (fs_options),Y and accumulates in $B2.
# Terminates on chars >= '@', '.', or negative.
# Returns with C=0 on normal exit, value in A and $B2.
label(0x8560, "parse_decimal")          # TAX; LDA #0; parse digits from (fs_options),Y
entry(0x8560)

# --- File handle ↔ bitmask conversion ---
label(0x8588, "handle_to_mask_a")       # TAY; CLC; fall into handle_to_mask
label(0x8589, "handle_to_mask_clc")     # CLC; fall into handle_to_mask (always convert)
label(0x858A, "handle_to_mask")         # Convert handle in Y to bitmask; C=0: convert, C=1 & Y=0: skip
label(0x85A5, "mask_to_handle")         # Convert single-bit mask in A to handle number (+$1E)

# --- Number and hex printing ---
label(0x85AF, "print_decimal")          # Print byte in A as 3-digit decimal (100/10/1)
label(0x85BC, "print_decimal_digit")    # Divide: print digit of Y÷A, remainder in Y
label(0x85EB, "print_hex")              # Print byte in A as two hex digits
label(0x85F6, "print_hex_nibble")       # Print low nibble of A as hex digit

# --- Address comparison ---
label(0x85CE, "compare_addresses")      # Compare 4-byte addresses at $B0 vs $B4; Z=1 if equal

# --- FSCV 7: read FS handles ---
label(0x85DA, "fscv_read_handles")      # Return X=$20 (base handle), Y=$27 (top handle)

# --- FS flags manipulation ---
label(0x85DF, "clear_fs_flag")          # Clear bit(s) in $0E07: A inverted, AND'd into flags
label(0x85E4, "set_fs_flag")            # Set bit(s) in $0E07: A OR'd into flags

# --- File info display ---
label(0x8600, "print_file_info")        # Print catalogue line: filename + load/exec/length
label(0x8617, "pad_filename_spaces")    # MONL2: pad filename display to 12 chars with spaces
label(0x862A, "print_exec_and_len")     # Print exec address (4 bytes) and length (3 bytes)
label(0x8635, "print_hex_bytes")        # Print X bytes from (fs_options)+Y as hex (high→low)
label(0x8640, "print_space")            # Print a space character via OSASCI

# --- TX control and transmission ---
label(0x8644, "setup_tx_ptr_c0")        # Set net_tx_ptr = $00C0 (TX control block address)
label(0x864C, "tx_poll_ff")             # Transmit with A=$FF, Y=$60 (full retry)
label(0x864E, "tx_poll_timeout")        # Transmit with Y=$60 (specified timeout)
label(0x8650, "tx_poll_core")           # Core transmit: send TX block, poll for result
label(0x8684, "delay_1ms")              # MSDELY: 1ms delay loop (nested DEX/DEY)

# ============================================================
# File operations: FILEV, ARGSV, FINDV, GBPBV ($8694-$8B91)
# ============================================================
# The FS vector handlers for file I/O. Each handler saves
# args via save_fscv_args, processes the request by building
# FS commands and sending them to the fileserver, then restores
# args and returns via restore_args_return ($892C).

# --- FILEV handler ($8694) and helpers ---
label(0x86D0, "send_fs_examine")       # Send FS command $92 (examine/load file)
label(0x8716, "send_data_blocks")      # Send file data in multi-block $80-byte chunks
label(0x8746, "filev_save")            # OSFILE $00 (save): copy addresses, send to FS
label(0x87AD, "copy_load_addr_from_params")  # Copy 4-byte load address from param block to $B0
label(0x87BA, "copy_reply_to_params")  # Copy FS reply data into parameter block
label(0x87C8, "transfer_file_blocks")  # Multi-block file data transfer loop

# --- FSCV 1: EOF handler ($881F) ---
label(0x881F, "eof_handler")           # FSCV 1: check end-of-file on handle

# --- FILEV attribute dispatch ($8844) ---
label(0x8844, "filev_attrib_dispatch") # FILEV function dispatch (A=1-6)
label(0x886E, "get_file_protection")  # CHA1: decode attribute byte for protection status
label(0x8883, "copy_filename_to_cmd") # CHASK2: copy filename string into FS command buffer
label(0x88C5, "copy_fs_reply_to_cb")  # COPYFS: copy FS reply buffer data to control block

# --- Common return point ($892C) ---
label(0x8912, "save_args_handle")      # SETARG: save handle for OSARGS operation
label(0x892C, "restore_args_return")   # Restore A/X/Y from saved workspace and return

# --- FSCV 0: *OPT handler ($89A1) ---
label(0x8985, "close_handle")           # CLOSE: detect CLOSE#0, close spool/exec or single handle
label(0x898F, "close_single_handle")   # CLOSE1: send close command for specific handle to FS
label(0x89A1, "opt_handler")           # FSCV 0: *OPT X,Y (set boot option or FS config)

# --- Address adjustment helpers ($89CA-$89E9) ---
label(0x89CA, "adjust_addrs_9")        # Adjust 4-byte addresses at param block offset 9
label(0x89CF, "adjust_addrs_1")        # Adjust 4-byte addresses at param block offset 1
label(0x89D1, "adjust_addrs_clc")      # CLC entry: clear carry before address adjustment
label(0x89D2, "adjust_addrs")          # Bidirectional 4-byte address adjustment
label(0x8AAD, "osgbpb_info")          # OSINFO: OSGBPB 5-8 handler, check Tube addresses
label(0x8AFB, "copy_reply_to_caller") # Copy FS reply data to caller buffer (direct or via Tube)
label(0x8B8A, "tube_claim_loop")      # TCLAIM: claim Tube with $C3, retry until acquired

# ============================================================
# *-Command handlers and FSCV dispatch ($8B92-$8DFF)
# ============================================================
# FSCV 2/3/4 (unrecognised *) routes through fscv_star_handler
# which matches against known FS commands before forwarding.
# The *CAT/*EX handlers display directory listings.
# *NET1-4 sub-commands manage file handles in local workspace.

# --- FSCV unrecognised * and command matching ---
label(0x8B92, "fscv_star_handler")     # FSCV 2/3/4: match * command against known FS commands
label(0x8BD6, "fs_cmd_match_table")    # Command match table: "I.", "I AM", "EX", "BYE", catch-all

# --- *EX and *CAT handlers ---
label(0x8BF2, "ex_handler")            # *EX: set 80-column format, branch into cat_handler
label(0x8BFD, "cat_handler")           # *CAT: display directory listing (20-column format)

# --- Boot command strings and option tables ---
label(0x8CEA, "boot_cmd_strings")      # Boot command strings: overlaps with JMP at $8CE7
label(0x8CF7, "set_lib_handle")        # Store Y into $0E04 (library handle)
label(0x8CFC, "set_csd_handle")        # Store Y into $0E03 (CSD handle)
label(0x8D02, "boot_option_offsets")   # Boot option → OSCLI string offset table (4 entries)
label(0x8D06, "i_am_handler")          # "I AM" command: parse station.network, forward to FS
label(0x8D1F, "copy_handles_and_boot") # Copy FS reply handles to workspace + execute boot
label(0x8D20, "copy_handles")          # Copy FS reply handles to workspace only (C=0 entry)
label(0x8D3A, "option_name_strings")   # Option name table: "Off", "Load", "Run", "Exec"
label(0x8D4B, "option_name_offsets")   # Offsets into option_name_strings (4 entries)
label(0x8D4F, "print_reply_bytes")     # Print Y bytes from FS reply buffer starting at offset X
label(0x8D51, "print_reply_counted")   # STRIN1: sub-entry of print_reply_bytes with caller-supplied Y count
label(0x8D5C, "print_spaces")          # Print X space characters
label(0x8D63, "copy_filename")         # Copy filename from (fs_crc_lo) to $0F05+ (X=0)
label(0x8D65, "copy_string_to_cmd")    # Copy string from (fs_crc_lo)+Y to $0F05+X until CR
label(0x8D67, "copy_string_from_offset") # COPLP1: sub-entry of copy_string_to_cmd with caller-supplied Y offset
label(0x8D73, "print_dir_name")        # Print directory name from reply buffer
label(0x8D75, "print_dir_from_offset") # INFOLP: sub-entry of print_dir_name with caller-supplied X offset
label(0x8D84, "notify_and_exec")       # Send FS command $4A, execute response or jump via ($0F09)

# --- *NET sub-command handlers ---
label(0x8DAF, "net1_read_handle")      # *NET1: read file handle from RX buffer offset $6F
label(0x8DB7, "calc_handle_offset")    # Calculate handle workspace offset: A → Y (A*12)
label(0x8DC9, "net2_read_handle_entry")# *NET2: look up handle in workspace
label(0x8DDF, "net3_close_handle")     # *NET3: mark handle as closed ($3F) in workspace
label(0x8DF2, "net4_resume_remote")    # *NET4: resume after remote operation
label(0x8DF7, "osword_fs_entry")       # OSWORD filing system entry: subtract $0F from command code

# ============================================================
# Named labels for ADLC NMI handler routines
# ============================================================
# These replace auto-generated c/sub_ prefixed labels with
# descriptive names based on analysis of the NFS ROM's ADLC
# interaction and four-way handshake state machine.

# --- ADLC control (BRIAN entry points: NFS02) ---
# BRIANX=+$0000 (transmit), BRIANP=+$0003 (power up),
# BRIANC=+$0006 (relinquish NMI), BRIANQ=+$0009 (reclaim NMI),
# BRIANI=+$000C (unknown interrupt handler)
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
label(0x9A34, "discard_reset_listen")  # Full ADLC reset then return to idle listen (5 refs)
label(0x9A40, "discard_listen")        # RX_DISCONTINUE then return to idle listen
label(0x9A43, "discard_after_reset")   # Just adlc_rx_listen + RTI (post-reset entry)
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

# --- NMI shim at end of ROM ---
label(0x9FCA, "nmi_shim_rom_src")      # Source for 32-byte copy to $0D00
label(0x9FCB, "nmi_bootstrap_entry")   # Bootstrap NMI: hardcoded JMP nmi_rx_scout
label(0x9FD9, "rom_set_nmi_vector")    # ROM copy of set_nmi_vector routine
label(0x9FEB, "rom_nmi_tail")          # TX flags update + address calc (purpose unclear)

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

# Indices 19-32: secondary dispatch for *-command parsing and
# filing system operations. Accessed via callers at $8079 (Y=$12)
# and $808C (Y=$13). The exact mapping of indices to individual
# handlers hasn't been fully traced yet.
for i in range(19, 33):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# *NET command handlers (indices 33-36)
# Index 36 overlaps: low byte at $8044 (= high table[0])
for i in range(33, 37):
    rts_code_ptr(0x8020 + i, 0x8044 + i)

# ============================================================
# Filing system OSWORD dispatch table at $8E18/$8E1D
# ============================================================
# Used by the PHA/PHA/RTS dispatch at $8E01 (entered from osword_fs_entry).
# osword_fs_entry subtracts $0F from the command code in $EF, giving a
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

# --- NMI shim at end of ROM ($9FCA-$9FFF) ---
# Bootstrap NMI handler and ROM copies of workspace routines.
# $9FCA is the source for the 32-byte copy to $0D00 by install_nmi_shim.
entry(0x9FCB)   # Bootstrap NMI entry (hardcoded JMP nmi_rx_scout, no self-mod)
entry(0x9FD9)   # ROM copy of set_nmi_vector + nmi_rti
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

# Alternate entry into ctrl_block_setup ($9162)
entry(0x9159)   # ADLC setup: LDX #$0D; LDY #$7C; BIT $833A; BVS c9167

# Dispatch targets found in equb data regions
# These are the bodies of the econet function dispatch handlers.
# Functions 1-3 share a handler ($91C7) — possibly different
# sub-operations that share setup logic. Function 5 ($91B5) and
# function 8 ($90CD) are distinct. The exact purpose of each
# function code hasn't been fully determined yet.
entry(0x91B5)   # Function 5 handler
entry(0x91C7)   # Function 1/2/3 handler (shared)
entry(0x90CD)   # Function 8 handler

# --- Code found in third-pass remaining equb regions ---
entry(0x8741)   # BEQ +3; JMP $8844 (called from $8743 region)
entry(0x8E7B)   # CMP #6; BCS ... (after 2-byte inline data table at $8E79)
entry(0x8F57)   # LDY #$28; ... (preceded by RTS, standalone entry)

# --- Code found in fourth-pass small equb regions ---
entry(0x8D5C)   # JSR ... (preceded by RTS, standalone entry)

# --- Dispatch targets from fs_cmd_match_table (PHA/PHA/RTS) ---
entry(0x8BF2)   # *EX handler (dispatch from command match table)
entry(0x8D06)   # "I AM" handler (dispatch from command match table)
entry(0x982D)   # LDA #$82; STA $FEA0; installs NMI handler $9839

# ============================================================
# Inline comments for key instructions
# ============================================================
# Note: acorn.bbc()'s hooks auto-annotate OSBYTE/OSWORD calls, so
# we only add comments where the auto-annotations don't reach.

# ============================================================
# Save FSCV arguments ($8508)
# ============================================================
comment(0x8508, """\
Save FSCV/vector arguments
Stores A, X, Y into the filing system workspace. Called at the
start of every FS vector handler (FILEV, ARGSV, BGETV, BPUTV,
GBPBV, FINDV, FSCV). NFS repurposes CFS/RFS workspace locations:
  $BD (fs_last_byte_flag) = A (function code / command)
  $BB (fs_options)        = X (control block ptr low)
  $BC (fs_block_offset)   = Y (control block ptr high)
  $BE/$BF (fs_crc_lo/hi)  = X/Y (duplicate for indexed access)""")

# ============================================================
# Attribute decoding ($8513 / $851D)
# ============================================================
comment(0x8513, """\
Decode file attributes: FS → BBC format (FSBBC, 6-bit variant)
Reads attribute byte at offset $0E from the parameter block,
masks to 6 bits, then falls through to the shared bitmask
builder. Converts fileserver protection format (5-6 bits) to
BBC OSFILE attribute format (8 bits) via the lookup table at
$8530. The two formats use different bit layouts for file
protection attributes.""")

comment(0x851D, """\
Decode file attributes: BBC → FS format (BBCFS, 5-bit variant)
Masks A to 5 bits and builds an access bitmask via the
lookup table at $8530. Each input bit position maps to a
different output bit via the table. The conversion is done
by iterating through the source bits and OR-ing in the
corresponding destination bits from the table, translating
between BBC (8-bit) and fileserver (5-bit) protection formats.""")

# ============================================================
# Skip spaces ($8555)
# ============================================================
comment(0x8555, """\
Skip leading spaces in parameter block
Advances Y past space characters in (fs_options),Y.
Returns with the first non-space character in A.
Sets carry if the character is >= 'A' (alphabetic).""")

# ============================================================
# Decimal number parser ($8560)
# ============================================================
comment(0x8560, """\
Parse decimal number from (fs_options),Y (DECIN)
Reads ASCII digits and accumulates in $B2 (fs_load_addr_2).
Multiplication by 10 uses the identity: n*10 = n*8 + n*2,
computed as ASL $B2 (×2), then A = $B2*4 via two ASLs,
then ADC $B2 gives ×10.
Terminates on "." (pathname separator), control chars, or space.
The delimiter handling was revised to support dot-separated path
components (e.g. "1.$.PROG") — originally stopped on any char
>= $40 (any letter), but the revision allows numbers followed
by dots. Returns: value in A and $B2, C=0 on normal termination.""")

# ============================================================
# File handle conversion ($8588-$858A)
# ============================================================
comment(0x858A, """\
Convert file handle to bitmask (Y2FS)
Converts fileserver handles to single-bit masks segregated inside
the BBC. NFS handles occupy the $20-$27 range (base HAND=$20),
which cannot collide with local filing system or cassette handles
— the MOS routes OSFIND/OSBGET/OSBPUT to the correct filing
system based on the handle value alone. The power-of-two encoding
allows the EOF hint byte to track up to 8 files simultaneously
with one bit per file, and enables fast set operations (ORA to
add, EOR to toggle, AND to test) without loops. Handle 0 passes
through unchanged (means "no file"). The bit-shift conversion loop
has a built-in validity check: if the handle is out of range, the
repeated ASL shifts all bits out, leaving A=0, which is converted
to Y=$FF as a sentinel — bad handles fail gracefully rather than
indexing into garbage.
Entry: Y = handle number, C flag controls behaviour:
  C=0: convert handle to bitmask (subtract $1F base, shift bit)
  C=1 with Y=0: return Y unchanged (skip conversion)
  C=1 with Y≠0: convert (used by FINDOP for close-by-handle)
Three entry points:
  $858A: direct entry (caller sets C and Y)
  $8589: CLC first (always convert)
  $8588: TAY first (handle in A, always convert)""")

# ============================================================
# Mask to handle ($85A5)
# ============================================================
comment(0x85A5, """\
Convert bitmask to handle number (FS2A)
Inverse of Y2FS. Converts from the power-of-two FS format
back to a sequential handle number by counting right shifts
until A=0. Adds $1E to convert the 1-based bit position to
a handle number (handles start at $1F+1 = $20). Used when
receiving handle values from the fileserver in reply packets.""")

# ============================================================
# Print decimal number ($85AF)
# ============================================================
comment(0x85AF, """\
Print byte as 3-digit decimal number
Prints A as a decimal number using repeated subtraction
for each digit position (100, 10, 1). Leading zeros are
printed (no suppression). Used to display station numbers.""")

comment(0x85BC, """\
Print one decimal digit by repeated subtraction
Divides Y by A using repeated subtraction. Prints the
quotient as an ASCII digit ('0'-'9'). Returns with the
remainder in Y. X starts at $2F ('0'-1) and increments
once per subtraction, giving the ASCII digit directly.""")

# ============================================================
# Address comparison ($85CE)
# ============================================================
comment(0x85CE, """\
Compare two 4-byte addresses
Compares bytes at $B0-$B3 against $B4-$B7 using EOR.
Returns Z=1 if all 4 bytes match, Z=0 on first mismatch.
Used by the OSFILE save handler to compare the current
transfer address ($C8-$CB, copied to $B0) against the end
address ($B4-$B7) during multi-block file data transfers.""")

# ============================================================
# FS flags ($85DF / $85E4)
# ============================================================
comment(0x85DF, """\
Clear bit(s) in FS flags ($0E07)
Inverts A (EOR #$FF), then ANDs into fs_work_0e07 to clear
the specified bits. Falls through to set_fs_flag to store.""")

comment(0x85E4, """\
Set bit(s) in FS flags ($0E07)
ORs A into fs_work_0e07 (EOF hint byte). Each bit represents
one of up to 8 open file handles. When clear, the file is
definitely NOT at EOF. When set, the fileserver must be queried
to confirm EOF status. This negative-cache optimisation avoids
expensive network round-trips for the common case. The hint is
cleared when the file pointer is updated (since seeking away
from EOF invalidates the hint) and set after BGET/OPEN/EOF
operations that might have reached the end.""")

# ============================================================
# Print file info ($8600)
# ============================================================
comment(0x8600, """\
Print file catalogue line
Displays a formatted catalogue entry: filename (padded to 12
chars with spaces), load address (4 hex bytes at offset 5-2),
exec address (4 hex bytes at offset 9-6), and file length
(3 hex bytes at offset $0C-$0A), followed by a newline.
Data is read from (fs_crc_lo) for the filename and from
(fs_options) for the numeric fields. Returns immediately
if fs_work_0e06 is zero (no info available).""")

# ============================================================
# Hex printing ($85EB / $85F6)
# ============================================================
comment(0x85EB, """\
Print byte as two hex digits
Prints the high nibble first (via 4× LSR), then the low
nibble. Each nibble is converted to ASCII '0'-'9' or 'A'-'F'
and output via OSASCI.""")

# ============================================================
# TX control ($8644-$8650)
# ============================================================
comment(0x8644, """\
Set up TX pointer to control block at $00C0
Points net_tx_ptr to $00C0 where the TX control block has
been built by init_tx_ctrl_block. Falls through to tx_poll_ff
to initiate transmission with full retry.""")

comment(0x864C, """\
Transmit and poll for result (full retry)
Sets A=$FF (retry count) and Y=$60 (timeout parameter).
Falls through to tx_poll_core.""")

comment(0x8650, """\
Core transmit and poll routine (XMIT)
Saves parameters, then claims the TX semaphore (tx_ctrl_status)
via ASL — a busy-wait spinlock where carry=0 means the semaphore
is held by another operation. Only after claiming the semaphore
is the TX pointer copied to nmi_tx_block, ensuring the low-level
transmit code sees a consistent pointer. Then calls the ADLC TX
setup routine and polls the control byte for completion:
  bit 7 set = still busy (loop)
  bit 6 set = error (check escape or report)
  bit 6 clear = success (clean return)
On error, checks for escape condition and handles retries.
Two entry points: setup_tx_ptr_c0 always uses the standard TXCB
(for fileserver traffic); tx_poll_core is general-purpose.""")

# ============================================================
# print_inline subroutine ($853B)
# ============================================================
comment(0x853B, """\
Print inline string (high-bit terminated) (VSTRNG)
Pops the return address from the stack, prints each byte via OSASCI
until a byte with bit 7 set is found, then jumps to that address.
The high-bit byte serves as both the string terminator and the opcode
of the first instruction after the string. N.B. Cannot be used for
BRK error messages — the stack manipulation means a BRK in the
inline data would corrupt the stack rather than invoke the error
handler.""")

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

# ============================================================
# Service handler entry ($80AF)
# ============================================================
comment(0x80AF, """\
Service handler entry
Intercepts three special service calls before normal dispatch:
  $FE: Tube init — explode character definitions (OSBYTE $14, X=6)
  $FF: Full init — set up WRCHV/RDCHV/BRKV/EVNTV, copy NMI handler
       code from ROM to RAM pages $04-$06, copy workspace init to
       $0016-$0076, then fall through to select NFS.
  $12 with Y=5: Select NFS as active filing system.
All other service calls dispatch via dispatch_service ($8127).""")

# ============================================================
# Init: set up vectors and copy code ($80C8)
# ============================================================
comment(0x80C8, """\
NFS initialisation (service $FF: full reset)
Sets up OS vectors for Tube co-processor support:
  WRCHV = $051C (page 5 — WRCH handler)
  RDCHV = $04E7 (page 4 — RDCH handler)
  BRKV  = $0016 (workspace — BRK/error handler)
  EVNTV = $06E8 (page 6 — event handler)
Writes $8E to Tube control register ($FEE0).
Then copies 3 pages of Tube host code from ROM ($934C/$944C/$954C)
to RAM ($0400/$0500/$0600), calls tube_post_init ($0414), and copies
97 bytes of workspace init from ROM ($9307) to $0016-$0076.""")

comment(0x80C8, "Set WRCHV = $051C (Tube WRCH handler)", inline=True)
comment(0x80D2, "Set RDCHV = $04E7 (Tube RDCH handler)", inline=True)
comment(0x80DC, "Set BRKV = $0016 (BRK handler in workspace)", inline=True)
comment(0x80E6, "Set EVNTV = $06E8 (event handler in page 6)", inline=True)
comment(0x80F0, "Write $8E to Tube control register", inline=True)

# ============================================================
# Select NFS as active filing system ($8184)
# ============================================================
comment(0x8184, """\
Select NFS as active filing system (INIT)
Reached from service $12 (select FS) with Y=5, or when *NET command
selects NFS. Notifies the current FS of shutdown via FSCV A=6 —
this triggers the outgoing FS to save its context back to its
workspace page, allowing restoration if re-selected later (the
FSDIE handoff mechanism). Then sets up the standard OS vector
indirections (FILEV through FSCV) to NFS entry points, claims the
extended vector table entries, and issues service $0F (vectors
claimed) to notify other ROMs. If fs_temp_cd is zero (auto-boot
not inhibited), injects the synthetic command "I .BOOT" through
the command decoder to trigger auto-boot login.""")

# ============================================================
# Set up filing system vectors ($8217)
# ============================================================
comment(0x8217, """\
Set up filing system vectors
Copies 14 bytes from fs_vector_addrs ($824D) into FILEV-FSCV ($0212).
These set all 7 filing system vectors to the standard extended vector
dispatch addresses ($FF1B, $FF1E, $FF21, $FF24, $FF27, $FF2A, $FF2D).
Then calls setup_rom_ptrs_netv to install the extended vector table
entries with the actual NFS handler addresses, and issues service
requests to notify other ROMs.""")

comment(0x8217, "Copy 14 bytes: FS vector addresses → FILEV-FSCV", inline=True)

# ============================================================
# Service 1: claim absolute workspace ($826F)
# ============================================================
comment(0x826F, """\
Service 1: claim absolute workspace
Claims pages up to $10 for NMI workspace ($0D), FS state ($0E),
and FS command buffer ($0F). If Y >= $10, workspace already
allocated — returns unchanged.""")

# ============================================================
# Service 2: claim private workspace ($8278)
# ============================================================
comment(0x8278, """\
Service 2: claim private workspace and initialise NFS
Y = next available workspace page on entry.
Sets up net_rx_ptr (Y) and nfs_workspace (Y+1) page pointers.
On soft break (OSBYTE $FD returns 0): skips FS state init,
preserving existing login state, file server selection, and
control block configuration — this is why pressing BREAK
keeps the user logged in.
On power-up/CTRL-BREAK (result non-zero):
  - Sets FS server station to $FE (FS, the default; no server)
  - Sets printer server to $EB (PS, the default)
  - Clears FS handles, OPT byte, message flag, SEQNOS
  - Initialises all RXCBs with $3F flag (available)
In both cases: reads station ID from $FE18 (only valid during
reset), calls adlc_init, enables user-level RX (LFLAG=$40).""")

comment(0x828F, "OSBYTE $FD: read type of last reset", inline=True)
comment(0x8295, "Soft break (X=0): skip FS init", inline=True)
comment(0x829B, "Station $FE = no server selected", inline=True)
comment(0x82C3, "Read station ID (also INTOFF)", inline=True)
comment(0x82C9, "Initialise ADLC hardware", inline=True)

# ============================================================
# Service 3: auto-boot ($81D1)
# ============================================================
comment(0x81D1, """\
Service 3: auto-boot
Notifies current FS of shutdown via FSCV A=6. Scans keyboard
(OSBYTE $7A): if no key is pressed, auto-boot proceeds; if the
'N' key is pressed (matrix address $55), the boot is declined
and the key is forgotten via OSBYTE $78. Any other key also
declines. Prints "Econet Station <n>" and checks the ADLC SR2
for the network clock signal — prints "No Clock" if absent (no
network communication possible without it). Then falls through
to set up NFS vectors (selecting NFS as the filing system).""")

# ============================================================
# Service 4: unrecognised * command ($8172)
# ============================================================
comment(0x8172, """\
Service 4: unrecognised * command
Matches the command text against ROM string table entries:
  X=8: matches "ROFF" at $8010 (within copyright string) → *ROFF
       (log off from fileserver) — jumps to resume_after_remote
  X=1: matches "NET" at $8009 (ROM title) → *NET (select NFS)
       — falls through to select_nfs
If neither matches, returns with the service call unclaimed.""")

# ============================================================
# Service 9: *HELP ($81BC)
# ============================================================
comment(0x81BC, """\
Service 9: *HELP
Prints the ROM identification string using print_inline.""")

# ============================================================
# Match ROM string ($819B)
# ============================================================
comment(0x819B, """\
Match command text against ROM string table
Compares characters from (os_text_ptr)+Y against bytes starting
at binary_version+X ($8008+X). Input is uppercased via AND $DF.
Returns with Z=1 if the ROM string's NUL terminator was reached
(match), or Z=0 if a mismatch was found. On match, Y points
past the matched text; on return, skips trailing spaces.""")

# ============================================================
# Call FSCV shutdown ($81CC)
# ============================================================
comment(0x81CC, """\
Notify filing system of shutdown
Loads A=6 (FS shutdown notification) and JMP (FSCV).
The FSCV handler's RTS returns to the caller of this routine
(JSR/JMP trick saves one level of stack).""")

# ============================================================
# Issue service: vectors claimed ($822E)
# ============================================================
comment(0x822E, """\
Issue 'vectors claimed' service and optionally auto-boot
Issues service $0F (vectors claimed) via OSBYTE $8F, then
service $0A. If fs_temp_cd is zero (auto-boot not inhibited),
sets up the command string "I .BOOT" at $8245 and jumps to
the FSCV 3 unrecognised-command handler (which matches against
the command table at $8BD6). The "I." prefix triggers the
catch-all entry which forwards the command to the fileserver.""")

# ============================================================
# Set up ROM pointer table and NETV ($82D1)
# ============================================================
comment(0x82D1, """\
Set up ROM pointer table and NETV
Reads the ROM pointer table base address via OSBYTE $A8, stores
it in osrdsc_ptr ($F6). Sets NETV low byte to $36. Then copies
one 3-byte extended vector entry (addr=$9007, rom=current) into
the ROM pointer table at offset $36, installing osword_dispatch
as the NETV handler.""")

# ============================================================
# FSCV shutdown: save FS state ($82FD)
# ============================================================
comment(0x82FD, """\
FSCV 6: Filing system shutdown / save state (FSDIE)
Called when another filing system (e.g. DFS) is selected. Saves
the current NFS context (FSLOCN station number, URD/CSD/LIB
handles, OPT byte, etc.) from page $0E into the dynamic workspace
backup area. This allows the state to be restored when *NET is
re-issued later, without losing the login session. Finally calls
OSBYTE $77 (FXSPEX: close SPOOL and EXEC files) to avoid leaving
dangling file handles across the FS switch.""")

# ============================================================
# Init TX control block ($831C)
# ============================================================
comment(0x831C, """\
Initialise TX control block at $00C0 from template
Copies 12 bytes from tx_ctrl_template ($8334) to $00C0.
For the first 2 bytes (Y=0,1), also copies the fileserver
station/network from $0E00/$0E01 to $00C2/$00C3.
The template sets up: control=$80, port=$99 (FS command port),
command data length=$0F, plus padding bytes.""")

comment(0x8334, """\
TX control block template (TXTAB, 12 bytes)
$00C0: $80 (control flag)    $00C1: $99 (port — FS command port)
$00C2: server station        $00C3: server network
$00C4: $00 (data low)        $00C5: $0F (data high — buffer page)
$00C6-$00CB: $FF (FILLER)
The $FF padding in the address fields is a recurring pattern:
Econet control blocks use 4-byte addresses but NFS only needs
2-byte addresses, so the upper two bytes are filled with $FF.""")

# ============================================================
# Prepare FS command ($8350)
# ============================================================
subroutine(0x8350, "prepare_fs_cmd",
    title="Prepare FS command buffer (12 references)",
    description="""\
Builds the 5-byte FS protocol header at $0F00:
  $0F00 HDRREP = reply port (set downstream, typically $90/PREPLY)
  $0F01 HDRFN  = Y parameter (function code)
  $0F02 HDRURD = URD handle (from $0E02)
  $0F03 HDRCSD = CSD handle (from $0E03)
  $0F04 HDRLIB = LIB handle (from $0E04)
Command-specific data follows at $0F05 (TXBUF). Also clears V flag.
Called before building specific FS commands for transmission.""")

# ============================================================
# Build and send FS command ($836A)
# ============================================================
subroutine(0x836A, "build_send_fs_cmd",
    title="Build and send FS command (DOFSOP)",
    description="""\
Sets reply port to $90 (PREPLY) at $0F00, initialises the TX
control block, then adjusts TXCB's high pointer (HPTR) to X+5
-- the 5-byte FS header (reply port, function code, URD, CSD,
LIB) plus the command data -- so only meaningful bytes are
transmitted, conserving Econet bandwidth. If carry is set on
entry (DOFSBX byte-stream path), takes the alternate path
through econet_tx_retry for direct BSXMIT transmission.
Otherwise sets up the TX pointer via setup_tx_ptr_c0 and falls
through to send_fs_reply_cmd for reply handling. The carry flag
is the sole discriminator between byte-stream and standard FS
protocol paths -- set by SEC at the BPUTV/BGETV entry points.
On return from WAITFS/BSXMIT, Y=0; INY advances past the
command code to read the return code. Error $D6 ("not found")
is detected via ADC #($100-$D6) with C=0 -- if the return code
was exactly $D6, the result wraps to zero (Z=1). This is a
branchless comparison returning C=1, A=0 as a soft error that
callers can handle, vs hard errors which go through FSERR.""",
    on_entry={"x": "buffer extent (command-specific data bytes)",
              "y": "function code",
              "a": "timeout period for FS reply",
              "c": "0 for standard FS path, 1 for byte-stream (BSXMIT)"})

# ============================================================
# FS error handler ($8402)
# ============================================================
comment(0x8402, """\
Handle fileserver error replies (FSERR)
The fileserver returns errors as: zero command code + error number +
CR-terminated message string. This routine converts the reply buffer
in-place to a standard MOS BRK error packet by:
  1. Storing the error code at fs_last_error ($0E09)
  2. Normalizing error codes below $A8 to $A8 (the standard FS error
     number), since the MOS error space below $A8 has other meanings
  3. Scanning for the CR terminator and replacing it with $00
  4. JMPing indirect through (l00c4) to execute the buffer as a BRK
     instruction — the zero command code serves as the BRK opcode
N.B. This relies on the fileserver always returning a zero command
code in position 0 of the reply buffer.""")

# ============================================================
# Handle BPUT/BGET ($83A3)
# ============================================================
subroutine(0x83A3, "handle_bput_bget",
    title="Handle BPUT/BGET file byte I/O",
    description="""\
BPUTV enters at $83A2 (CLC; fall through) and BGETV enters
at $8485 (SEC; JSR here). The carry flag is preserved via
PHP/PLP through the call chain and tested later (BCS) to
select byte-stream transmission (BSXMIT) vs normal FS
transmission (FSXMIT) -- a control-flow encoding using
processor flags to avoid an extra flag variable.

BSXMIT uses handle=0 for print stream transactions (which
sidestep the SEQNOS sequence number manipulation) and non-zero
handles for file operations. After transmission, the high
pointer bytes of the CB are reset to $FF -- "The BGET/PUT byte
fix" which prevents stale buffer pointers corrupting subsequent
byte-level operations.""",
    on_entry={"c": "0 for BPUT (write byte), 1 for BGET (read byte)",
              "a": "byte to write (BPUT only)",
              "y": "file handle"},
    on_exit={"a": "byte read (BGET only)"})

# ============================================================
# Send command to fileserver ($844A)
# ============================================================
comment(0x844A, """\
Send command to fileserver and handle reply (WAITFS)
Performs a complete FS transaction: transmit then wait for reply.
Sets bit 7 of rx_status_flags (mark FS transaction in progress),
builds a TX frame from the data at (net_tx_ptr), and transmits
it. The system RX flag (LFLAG bit 7) is only set when receiving
into the page-zero control block — if RXCBP's high byte is
non-zero, setting the system flag would interfere with other RX
operations. The timeout counter uses the stack (indexed via TSX)
rather than memory to avoid bus conflicts with Econet hardware
during the tight polling loop. Handles multi-block replies and
checks for escape conditions between blocks.""")

# ============================================================
# Check escape ($847A)
# ============================================================
comment(0x847A, """\
Check and handle escape condition (ESC)
Two-level escape gating: the MOS escape flag ($FF bit 7) is ANDed
with the software enable flag ESCAP. Both must have bit 7 set for
escape to fire. ESCAP is set non-zero during data port operations
(LOADOP stores the data port $90, serving double duty as both the
port number and the escape-enable flag). ESCAP is disabled via LSR
in the ENTER routine, which clears bit 7 — PHP/PLP around the LSR
preserves the carry flag since ENTER is called from contexts where
carry has semantic meaning (e.g., PUTBYT vs BGET distinction).
This architecture allows escape between retransmission attempts
but prevents interruption during critical FS transactions. If
escape fires: acknowledges via OSBYTE $7E, then checks whether
the failing handle is the current SPOOL or EXEC handle (OSBYTE
$C6/$C7); if so, issues "*SP." or "*E." via OSCLI to gracefully
close the channel before raising the error — preventing the system
from continuing to spool output to a broken file handle.""")

# ============================================================
# Error message table ($84AF)
# ============================================================
comment(0x84AF, """\
Econet error message table (ERRTAB, 8 entries)
Each entry: error number byte followed by NUL-terminated string.
  $A0: "Line Jammed"     $A1: "Net Error"
  $A2: "Not listening"   $A3: "No Clock"
  $A4: "Bad Txcb"        $11: "Escape"
  $CB: "Bad Option"      $A5: "No reply"
Indexed by the low 3 bits of the TXCB flag byte (AND #$07),
which encode the specific Econet failure reason. The NREPLY
and NLISTN routines build a MOS BRK error block at $100 on the
stack page: NREPLY fires when the fileserver does not respond
within the timeout period; NLISTN fires when the destination
station actively refused the connection.
Indexed via the error dispatch at c8424/c842c.""")

# ============================================================
# Resume after remote operation ($8146)
# ============================================================
comment(0x8146, """\
Resume after remote operation / *ROFF handler (NROFF)
Checks byte 4 of (net_rx_ptr): if non-zero, the keyboard was
disabled during a remote operation (peek/poke/boot). Clears
the flag, re-enables the keyboard via OSBYTE $C9, and sends
function $0A to notify completion. Also handles *ROFF and the
triple-plus escape sequence (+++), which resets system masks
via OSBYTE $CE and returns control to the MOS, providing an
escape route when a remote session becomes unresponsive.""")

# ============================================================
# FSCV handler ($808C)
# ============================================================
subroutine(0x808C, "fscv_handler",
    title="FSCV dispatch entry",
    description="""\
Entered via the extended vector table when the MOS calls FSCV.
Stores A/X/Y via save_fscv_args, compares A (function code) against 8,
and dispatches codes 0-7 via the shared dispatch table at $8020
with base offset Y=$12 (table indices 19-26).
Function codes: 0=*OPT, 1=EOF, 2=*/, 3=unrecognised *,
4=*RUN, 5=*CAT, 6=shutdown, 7=read handles.""",
    on_entry={"a": "function code (0-7)",
              "x": "depends on function",
              "y": "depends on function"})

comment(0x808C, "Store A/X/Y in FS workspace", inline=True)
comment(0x808F, "Function code >= 8? Return (unsupported)", inline=True)
comment(0x8095, "Y=$12: base offset for FSCV dispatch (indices 19+)", inline=True)

# ============================================================
# FILEV handler ($8694)
# ============================================================
subroutine(0x8694, "filev_handler",
    title="FILEV handler (OSFILE entry point)",
    description="""\
Saves A/X/Y, copies the filename pointer from the parameter block
to os_text_ptr, then uses GSINIT/GSREAD to parse the filename into
$0FC5+. Sets fs_crc_lo/hi to point at the parsed filename buffer.
Dispatches by function code A:
  A=$FF: load file (send_fs_examine at $86D0)
  A=$00: save file (filev_save at $8746)
  A=$01-$06: attribute operations (filev_attrib_dispatch at $8844)
  Other: restore_args_return (unsupported, no-op)""",
    on_entry={"a": "function code ($FF=load, $00=save, $01-$06=attrs)",
              "x": "parameter block address low byte",
              "y": "parameter block address high byte"})

comment(0x86D0, """\
Send FS examine command
Sends FS command $03 (FCEXAM: examine file) to the fileserver.
Sets $0F02=$03 and error pointer to '*'. Called for OSFILE $FF
(load file) with the filename already in the command buffer.
The FS reply contains load/exec addresses and file length which
are used to set up the data transfer. The header URD field
is repurposed to carry the Econet data port number (PLDATA=$92)
for the subsequent block data transfer.""")

comment(0x8716, """\
Send file data in multi-block chunks
Repeatedly sends $80-byte (128-byte) blocks of file data to the
fileserver using command $7F. Compares current address ($C8-$CB)
against end address ($B4-$B7) via compare_addresses, and loops
until the entire file has been transferred. Each block is sent
via send_to_fs_star.""")

comment(0x8746, """\
OSFILE save handler (A=$00)
Copies 4-byte load/exec/length addresses from the parameter block
to the FS command buffer, along with the filename. Sends FS
command $91 with function $14 to initiate the save, then
calls print_file_info to display the filename being saved.
Handles both host and Tube-based data sources.
When receiving the save acknowledgement, the RX low pointer is
incremented by 1 to skip the command code (CC) byte, which
indicates the FS type and must be preserved. N.B. this assumes
the RX buffer does not cross a page boundary.""")

comment(0x87AD, """\
Copy load address from parameter block
Copies 4 bytes from (fs_options)+2..5 (the load address in the
OSFILE parameter block) to $AE-$B3 (local workspace).""")

comment(0x87BA, """\
Copy FS reply data to parameter block
Copies bytes from the FS command reply buffer ($0F02+) into the
parameter block at (fs_options)+2..13. Used to fill in the OSFILE
parameter block with information returned by the fileserver.""")

comment(0x87C8, """\
Multi-block file data transfer
Manages the transfer of file data in chunks between the local
machine and the fileserver. Entry conditions: WORK ($B0-$B3) and
WORK+4 ($B4-$B7) hold the low and high addresses of the data
being sent/received. Sets up source ($C4-$C7) and destination
($C8-$CB) from the FS reply, sends $80-byte (128-byte) blocks
with command $91, and continues until all data has been
transferred. Handles address overflow and Tube co-processor
transfers. For SAVE, WORK+8 holds the port on which to receive
byte-level ACKs for each data block (flow control).""")

comment(0x881F, """\
FSCV 1: EOF handler
Checks whether a file handle has reached end-of-file. Converts
the handle via handle_to_mask_clc, tests the result against the
EOF hint byte ($0E07). If the hint bit is clear, returns X=0
immediately (definitely not at EOF — no network call needed).
If the hint bit is set, sends FS command $11 (FCEOF) to query
the fileserver for definitive EOF status. Returns X=$FF if at
EOF, X=$00 if not. This two-level check avoids an expensive
network round-trip when the file is known to not be at EOF.""")

comment(0x8844, """\
FILEV attribute dispatch (A=1-6)
Dispatches OSFILE operations by function code:
  A=1: write catalogue info (load/exec/length/attrs) — FS $14
  A=2: write load address only
  A=3: write exec address only
  A=4: write file attributes
  A=5: read catalogue info, returns type in A — FS $12
  A=6: delete named object — FS $14 (FCDEL)
  A>=7: falls through to restore_args_return (no-op)
Each handler builds the appropriate FS command, sends it to
the fileserver, and copies the reply into the parameter block.
The control block layout uses dual-purpose fields: the 'data
start' field doubles as 'length' and 'data end' doubles as
'protection' depending on whether reading or writing attrs.""")

comment(0x892C, """\
Restore arguments and return
Common exit point for FS vector handlers. Reloads A from
fs_last_byte_flag ($BD), X from fs_options ($BB), and Y from
fs_block_offset ($BC) — the values saved at entry by
save_fscv_args — and returns to the caller.""")

comment(0x89A1, """\
FSCV 0: *OPT handler (OPTION)
Handles *OPT X,Y to set filing system options:
  *OPT 1,Y (Y=0/1): set local user option in $0E06 (OPT)
  *OPT 4,Y (Y=0-3): set boot option via FS command $16 (FCOPT)
Other combinations generate error $CB (OPTER: "bad option").""")

comment(0x89D2, """\
Bidirectional 4-byte address adjustment
Adjusts a 4-byte value in the parameter block at (fs_options)+Y:
  If fs_load_addr_2 ($B2) is positive: adds fs_lib_handle+X values
  If fs_load_addr_2 ($B2) is negative: subtracts fs_lib_handle+X
Starting offset X=$FC means it reads from $0E06-$0E09 area.
Used to convert between absolute and relative file positions.""")

# ============================================================
# ARGSV handler ($88E1)
# ============================================================
subroutine(0x88E1, "argsv_handler",
    title="ARGSV handler (OSARGS entry point)",
    description="""\
  A=0, Y=0: return filing system number (10 = network FS)
  A=0, Y>0: read file pointer via FS command $0A (FCRDSE)
  A=1, Y>0: write file pointer via FS command $14 (FCWRSE)
  A>=3 (ensure): silently returns -- NFS has no local write buffer
     to flush, since all data is sent to the fileserver immediately
The handle in Y is converted via handle_to_mask_clc. For writes
(A=1), the carry flag from the mask conversion is used to branch
to save_args_handle, which records the handle for later use.""",
    on_entry={"a": "function code (0=query, 1=write ptr, >=3=ensure)",
              "y": "file handle (0=FS-level query, >0=per-file)"},
    on_exit={"a": "filing system number (10) when A=0, Y=0"})

# ============================================================
# FINDV handler ($8949)
# ============================================================
subroutine(0x8949, "findv_handler",
    title="FINDV handler (OSFIND entry point)",
    description="""\
  A=0: close file -- delegates to close_handle ($8985)
  A>0: open file -- modes $40=read, $80=write/update, $C0=read/write
For open: the mode byte is converted to the fileserver's two-flag
format by flipping bit 7 (EOR #$80) and shifting. This produces
Flag 1 (read/write direction) and Flag 2 (create/existing),
matching the fileserver protocol. After a successful open, the
new handle's bit is OR'd into the EOF hint byte (marks it as
"might be at EOF, query the server"), and into the sequence
number tracking byte for the byte-stream protocol.""",
    on_entry={"a": "operation (0=close, $40=read, $80=write, $C0=R/W)",
              "x": "filename pointer low (open)",
              "y": "file handle (close) or filename pointer high (open)"},
    on_exit={"a": "file handle (open) or preserved (close)"})

# ============================================================
# CLOSE handler ($8985)
# ============================================================
comment(0x8985, """\
Close file handle(s) (CLOSE)
  Y=0: close all files — first calls OSBYTE $77 (close SPOOL and
       EXEC files) to coordinate with the MOS before sending the
       close-all command to the fileserver. This ensures locally-
       managed file handles are released before the server-side
       handles are invalidated, preventing the MOS from writing to
       a closed spool file.
  Y>0: close single handle — sends FS close command and clears
       the handle's bit in both the EOF hint byte and the sequence
       number tracking byte.""")

# ============================================================
# GBPBV handler ($89EA)
# ============================================================
subroutine(0x89EA, "gbpbv_handler",
    title="GBPBV handler (OSGBPB entry point)",
    description="""\
  A=1-4: file read/write operations (handle-based)
  A=5-8: info queries (disc title, current dir, lib, filenames)
Calls 1-4 are standard file data transfers via the fileserver.
Calls 5-8 were a late addition to the MOS spec and are the only
NFS operations requiring Tube data transfer -- described in the
original source as "untidy but useful in theory." The data format
uses length-prefixed strings (<name length><object name>) rather
than the CR-terminated strings used elsewhere in the FS.""",
    on_entry={"a": "call number (1-8)",
              "x": "parameter block address low byte",
              "y": "parameter block address high byte"},
    on_exit={"c": "set if transfer incomplete"})

# ============================================================
# OSGBPB info handler ($8AAD)
# ============================================================
comment(0x8AAD, """\
OSGBPB 5-8 info handler (OSINFO)
Handles the "messy interface tacked onto OSGBPB" (original source).
Checks whether the destination address is in Tube space by comparing
the high bytes against TBFLAG. If in Tube space, data must be
copied via the Tube FIFO registers (with delays to accommodate the
slow 16032 co-processor) instead of direct memory access.""")

# ============================================================
# Forward unrecognised * command to fileserver ($8079)
# ============================================================
comment(0x8079, """\
Forward unrecognised * command to fileserver (COMERR)
Copies command text from (fs_crc_lo) to $0F05+ via copy_filename,
prepares an FS command with function code 0, and sends it to the
fileserver to request decoding. The server returns a command code
indicating what action to take (e.g. code 4=INFO, 7=DIR, 9=LIB,
5=load-as-command). This mechanism allows the fileserver to extend
the client's command set without ROM updates. Called from the "I."
and catch-all entries in the command match table at $8BD6, and
from FSCV 2/3/4 indirectly. If CSD handle is zero (not logged
in), returns without sending.""")

# ============================================================
# *BYE handler ($8349)
# ============================================================
comment(0x8349, """\
*BYE handler (logoff)
Closes any open *SPOOL and *EXEC files via OSBYTE $77 (FXSPEX),
then falls into prepare_fs_cmd with Y=$17 (FCBYE: logoff code).
Dispatched from the command match table at $8BD6 for "BYE".""")

# ============================================================
# FSCV unrecognised * handler ($8B92)
# ============================================================
comment(0x8B92, """\
FSCV 2/3/4: unrecognised * command handler (DECODE)
CLI parser originally by Roger Wilson (later Sophie Wilson,
co-designer of ARM). Matches command text against the table
at $8BD6 using case-insensitive comparison with abbreviation
support — commands can be shortened with '.' (e.g. "I." for
"INFO"). The "I." entry is a special fudge placed first in the
table: since "I." could match multiple commands, it jumps to
forward_star_cmd to let the fileserver resolve the ambiguity.

The matching loop compares input characters against table
entries. On mismatch, it skips to the next entry. On match
of all table characters, or when '.' abbreviation is found,
it dispatches via PHA/PHA/RTS to the entry's handler address.

After matching, adjusts fs_crc_lo/fs_crc_hi to point past
the matched command text.""")

comment(0x8BD6, """\
FS command match table (COMTAB)
Format: command letters (bit 7 clear), then dispatch address
as two bytes: high|(bit 7 set), low. The PHA/PHA/RTS trick
adds 1 to the stored (address-1). Matching is case-insensitive
(AND $DF) and supports '.' abbreviation (standard Acorn pattern).

Entries:
  "I."     → $8079 (forward_star_cmd) — placed first as a fudge
             to catch *I. abbreviation before matching *I AM
  "I AM"   → $8D06 (i_am_handler: parse station.net, logon)
  "EX "    → $8BF2 (ex_handler: extended catalogue)
  "EX"\\r   → $8BF2 (same, exact match at end of line)
  "BYE"\\r  → $8349 (bye_handler: logoff)
  <catch-all> → $8079 (forward anything else to FS)""")

# ============================================================
# *EX and *CAT handlers ($8BF2 / $8BFD)
# ============================================================
comment(0x8BF2, """\
*EX handler (extended catalogue)
Sets column width $B6=$50 (80 columns, one file per line with
full details) and $B7=$01, then branches into cat_handler at
$8C07, bypassing cat_handler's default 20-column setup.""")

comment(0x8BFD, """\
*CAT handler (directory catalogue)
Sets column width $B6=$14 (20 columns, four files per 80-column
line) and $B7=$03. The catalogue protocol is multi-step: first
sends FCUSER ($15: read user environment) to get CSD, disc, and
library names, then sends FCREAD ($12: examine) repeatedly to
fetch entries in batches until zero are returned (end of dir).
The receive buffer abuts the examine request buffer and ends at
RXBUFE, allowing seamless data handling across request cycles.

The command code byte in the fileserver reply indicates FS type:
zero means an old-format FS (client must format data locally),
non-zero means new-format (server returns pre-formatted strings).
This enables backward compatibility with older Acorn fileservers.

Display format:
  - Station number in parentheses
  - "Owner" or "Public" access level
  - Boot option with name (Off/Load/Run/Exec)
  - Current directory and library paths
  - Directory entries: CRFLAG ($CF) cycles 0-3 for multi-column
    layout; at count 0 a newline is printed, others get spaces.
    *EX sets CRFLAG=$FF to force one entry per line.""")

# ============================================================
# Boot command strings ($8CEA)
# ============================================================
comment(0x8CEA, """\
Boot command strings for auto-boot
The four boot options use OSCLI strings at offsets within page $8C:
  Option 0 (Off):  offset $F6 → $8CF6 = bare CR (empty command)
  Option 1 (Load): offset $E7 → $8CE7 = "L.!BOOT" (dual-purpose:
      the JMP $212E instruction at $8CE7 has opcode $4C='L' and
      operand bytes $2E='.' $21='!', forming the string "L.!")
  Option 2 (Run):  offset $E9 → $8CE9 = "!BOOT" (bare filename = *RUN)
  Option 3 (Exec): offset $EF → $8CEF = "E.!BOOT"

This is a classic BBC ROM space optimisation: the JMP instruction's
bytes serve double duty as both executable code and ASCII text.""")

# ============================================================
# Handle workspace management ($8CF7-$8CFF)
# ============================================================
comment(0x8CF7, """\
Set library handle
Stores Y into $0E04 (library directory handle in FS workspace).
Falls through to c8cff (JMP c892c) if Y is non-zero.""")

comment(0x8CFC, """\
Set CSD handle
Stores Y into $0E03 (current selected directory handle).
Falls through to c8cff (JMP c892c).""")

# ============================================================
# Boot option table and "I AM" handler ($8D02-$8D1E)
# ============================================================
comment(0x8D02, """\
Boot option → OSCLI string offset table
Four bytes indexed by the boot option value (0-3). Each byte
is the low byte of a pointer into page $8C, where the OSCLI
command string for that boot option lives. See boot_cmd_strings.""")

comment(0x8D06, """\
"I AM" command handler
Dispatched from the command match table when the user types
"*I AM <station>" or "*I AM <station>.<network>".
Parses the station number (and optional network number after '.')
using skip_spaces and parse_decimal. Stores the results in:
  $0E00 = station number (or fileserver station)
  $0E01 = network number
Then forwards the command to the fileserver via forward_star_cmd.""")

# ============================================================
# Copy handles and boot ($8D1F / $8D20)
# ============================================================
comment(0x8D1F, """\
Copy FS reply handles to workspace and execute boot command
SEC entry (LOGIN): copies 4 bytes from $0F05-$0F08 (FS reply) to
$0E02-$0E05 (URD, CSD, LIB handles and boot option), then
looks up the boot option in boot_option_offsets to get the
OSCLI command string and executes it via JMP oscli.
The carry flag distinguishes LOGIN (SEC) from SDISC (CLC) — both
share the handle-copying code, but only LOGIN executes the boot
command. This use of the carry flag to select behaviour between
two callers avoids duplicating the handle-copy loop.""")

comment(0x8D20, """\
Copy FS reply handles to workspace (no boot)
CLC entry (SDISC): copies handles only, then jumps to c8cff.
Called when the FS reply contains updated handle values
but no boot action is needed.""")

# ============================================================
# Option name display ($8D3A-$8D4E)
# ============================================================
comment(0x8D3A, """\
Option name strings
Null-terminated strings for the four boot option names:
  "Off", "Load", "Run", "Exec"
Used by cat_handler to display the current boot option setting.""")

comment(0x8D4B, """\
Option name offsets
Four-byte table of offsets into option_name_strings:
  0, 4, 9, $0D — one per boot option value (0-3).""")

# ============================================================
# Reply buffer display helpers ($8D4F-$8D72)
# ============================================================
comment(0x8D4F, """\
Print reply buffer bytes
Prints Y characters from the FS reply buffer ($0F05+X) to
the screen via OSASCI. X = starting offset, Y = count.
Used by cat_handler to display directory and library names.""")

comment(0x8D5C, """\
Print spaces
Prints X space characters via print_space. Used by cat_handler
to align columns in the directory listing.""")

# ============================================================
# Filename copy helpers ($8D63-$8D72)
# ============================================================
comment(0x8D63, """\
Copy filename to FS command buffer
Entry with X=0: copies from (fs_crc_lo),Y to $0F05+X until CR.
Used to place a filename into the FS command buffer before
sending to the fileserver. Falls through to copy_string_to_cmd.""")

comment(0x8D65, """\
Copy string to FS command buffer
Entry with X and Y specified: copies bytes from (fs_crc_lo),Y
to $0F05+X, stopping when a CR ($0D) is encountered. The CR
itself is also copied. Returns with X pointing past the last
byte written.""")

# ============================================================
# Print directory name ($8D73)
# ============================================================
comment(0x8D73, """\
Print directory name from reply buffer
Prints characters from the FS reply buffer ($0F05+X onwards).
Null bytes ($00) are replaced with CR ($0D) for display.
Stops when a byte with bit 7 set is encountered (high-bit
terminator). Used by cat_handler to display Dir. and Lib. paths.""")

# ============================================================
# Notify and execute ($8D84)
# ============================================================
comment(0x8D84, """\
Send FS load-as-command and execute response
Sets up an FS command with function code $05 (FCCMND: load as
command) using send_fs_examine. If a Tube co-processor is
present (tx_in_progress != 0), transfers the response data
to the Tube via tube_addr_claim. Otherwise jumps via the
indirect pointer at ($0F09) to execute at the load address.""")

# ============================================================
# *NET sub-command handlers ($8DAF-$8DF5)
# ============================================================
comment(0x8DAF, """\
*NET1: read file handle from received packet
Reads a file handle byte from offset $6F in the RX buffer
(net_rx_ptr), stores it in $F0, then falls through to the
common handle workspace cleanup at c8dda (clear fs_temp_ce).""")

comment(0x8DB7, """\
Calculate handle workspace offset
Converts a file handle number (in A) to a byte offset (in Y)
into the NFS handle workspace. The calculation is A*12:
  ASL A (A*2), ASL A (A*4), PHA, ASL A (A*8),
  ADC stack (A*8 + A*4 = A*12).
Validates that the offset is < $48 (max 6 handles × 12 bytes
per handle entry = 72 bytes). If invalid (>= $48), returns
with C set and Y=0, A=0 as an error indicator.""")

comment(0x8DC9, """\
*NET2: read handle entry from workspace
Looks up the handle in $F0 via calc_handle_offset. If the
workspace slot contains $3F ('?', meaning unused/closed),
returns 0. Otherwise returns the stored handle value.
Clears fs_temp_ce on exit.""")

comment(0x8DDF, """\
*NET3: close handle (mark as unused)
Looks up the handle in $F0 via calc_handle_offset. Writes
$3F ('?') to mark the handle slot as closed in the NFS
workspace. Preserves the carry flag state across the write
using ROL/ROR on rx_status_flags. Clears fs_temp_ce on exit.""")

comment(0x8DF2, """\
*NET4: resume after remote operation
Calls resume_after_remote ($8146) to re-enable the keyboard
and send a completion notification. The BVC always branches
to c8dda (clear fs_temp_ce) since resume_after_remote
returns with V clear (from CLV in prepare_cmd_clv).""")

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
NETVEC dispatch handler (ENTRY)
Indirected from NETVEC at $0224. Saves all registers and flags,
retrieves the reason code from the stacked A, and dispatches to
one of 9 handlers (codes 0-8) via the PHA/PHA/RTS trampoline at
$9020. Reason codes >= 9 are ignored.

Dispatch targets (from NFS09):
  0:   no-op (RTS)
  1-3: PRINT — chars in printer buffer / Ctrl-B / Ctrl-C
  4:   NWRCH — write character to screen (net write char)
  5:   SELECT — printer selection changed
  6:   no-op (net read char — not implemented)
  7:   NBYTE — remote OSBYTE call
  8:   NWORD — remote OSWORD call""")

comment(0x900E, "Retrieve original A (function code) from stack", inline=True)
comment(0x9020, "PHA/PHA/RTS trampoline: push handler addr-1, RTS jumps to it", inline=True)

# ============================================================
# NWRCH: net write character ($903D)
# ============================================================
comment(0x903D, """\
Fn 4: net write character (NWRCH)
Writes a character (passed in Y) to the screen via OSWRITCH.
Before the write, uses TSX to reach into the stack and zero the
carry flag in the caller's saved processor status byte — ROR
followed by ASL on the stacked P byte ($0106,X) shifts carry
out and back in as zero. This ensures the calling code's PLP
restores carry=0, signalling "character accepted" without needing
a separate CLC/PHP sequence. A classic 6502 trick for modifying
return flags without touching the actual processor status.""")

comment(0x903E, "ROR/ASL on stacked P: zeros carry to signal success", inline=True)

# ============================================================
# Setup TX and send ($904B)
# ============================================================
comment(0x904B, """\
Set up TX control block and send
Builds a TX control block at (nfs_workspace)+$0C from the current
workspace state, then initiates transmission via the ADLC TX path.
This is the common send routine used after command data has been
prepared. The exact control block layout and field mapping need
further analysis.""")

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
  ctrl_block_setup:   X=$1A (26) down, Y=$17 (23) down, V=0
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
# OSWORD handler block comments
# ============================================================
comment(0x8E33, """\
OSWORD $0F handler: initiate transmit (CALLTX)
Checks the TX semaphore (TXCLR at $0D62) via ASL — if carry is
clear, a TX is already in progress and the call returns an error,
preventing user code from corrupting a system transmit. Otherwise
copies 16 bytes from the caller's OSWORD parameter block into the
user TX control block (UTXCB) in static workspace. The TXCB
pointer is copied to LTXCBP only after the semaphore is claimed,
ensuring the low-level transmit code (BRIANX) sees a consistent
pointer — if copied before claiming, another transmitter could
modify TXCBP between the copy and the claim.""")

comment(0x8E53, """\
OSWORD $11 handler: read JSR arguments (READRA)
Copies the JSR (remote procedure call) argument buffer from the
static workspace page back to the caller's OSWORD parameter block.
Reads the buffer size from workspace offset JSRSIZ, then copies
that many bytes. After the copy, clears the old LSTAT byte via
CLRJSR to reset the protection status. Also provides READRB as
a sub-entry ($8E6A) to return just the buffer size and args size
without copying the data.""")

comment(0x8E7B, """\
OSWORD $12 handler: read/set state information (RS)
Dispatches on the sub-function code (0-9):
  0: read FS station (FSLOCN at $0E00)
  1: set FS station
  2: read printer server station (PSLOCN)
  3: set printer server station
  4: read protection masks (LSTAT at $D63)
  5: set protection masks
  6: read context handles (URD/CSD/LIB, converted from
     internal single-bit form back to handle numbers)
  7: set context handles (converted to internal form)
  8: read local station number
  9: read JSR arguments buffer size
Even-numbered sub-functions read; odd-numbered ones write.
Uses the bidirectional copy at $8E22 for station read/set.""")

comment(0x8EF0, """\
OSWORD $10 handler: open/read RX control block (OPENRX)
If the first byte of the caller's parameter block is zero, scans
for a free RXCB (flag byte = $3F = deleted) starting from RXCB #3
(RXCBs 0-2 are dedicated: printer, remote, FS). Returns the RXCB
number in the first byte, or zero if none free. If the first byte
is non-zero, reads the specified RXCB's data back into the caller's
parameter block (12 bytes) and then deletes the RXCB by setting
its flag byte to $3F — a consume-once semantic so user code reads
received data and frees the CB in a single atomic operation,
preventing double-reads. The low-level user RX flag (LFLAG) is
temporarily disabled via ROR/ROL during the operation to prevent
the interrupt-driven receive code from modifying a CB that is
being read or opened.""")

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

comment(0x913A, """\
Remote operation with source validation (REMOT)
Validates that the source station/network in the received packet
matches the controlling station stored in the remote RXCB. This
ensures that only the station that initiated the remote session
can send commands — characters from other stations are rejected.
Full init sequence: 1) disable keyboard, 2) set workspace ptr,
3) set status busy, 4) set R/W/byte/word masks, 5) set up CB,
6) set MODE 7 (the only mode guaranteed for terminal emulation),
7) set auto repeat rates, 8) enter current language. This is
essentially a "thin terminal" setup — the local machine becomes
a remote display/keyboard for the controlling station.
Bit 0 of the status byte disallows further remote takeover
attempts (preventing re-entrant remote control), while bit 3
marks the machine as currently remoted.""")

comment(0x914A, """\
Insert remote keypress
Reads a character from RX block offset $82 and inserts it into
keyboard input buffer 0 via OSBYTE $99.""")

# ============================================================
# Remote operation support routines ($8F57-$92F0)
# ============================================================
comment(0x8F57, """\
Set up RX buffer pointers in NFS workspace
Calculates the start address of the RX data area ($F0+1) and stores
it at workspace offset $28. Also reads the data length from ($F0)+1
and adds it to $F0 to compute the end address at offset $2C.""")

comment(0x9063, """\
Fn 7: remote OSBYTE handler (NBYTE)
Full RPC mechanism for OSBYTE calls across the network. When a
machine is remoted, OSBYTE/OSWORD calls that affect terminal-side
hardware (keyboard scanning, flash rates, etc.) must be indirected
across the net. OSBYTE calls are classified into three categories:
  Y>0 (NCTBPL table): executed on BOTH machines (flash rates etc.)
  Y<0 (NCTBMI table): executed on terminal only, result sent back
  Y=0: not recognised, passed through unhandled
Results returned via stack manipulation: the saved processor status
byte at $0106 has V-flag (bit 6) forced on to tell the MOS the
call was claimed (preventing dispatch to other ROMs), and the I-bit
(bit 2) forced on to disable interrupts during register restoration,
preventing race conditions. The carry flag in the saved P is also
manipulated via ROR/ASL to zero it, signaling success to the caller.
OSBYTE $81 (INKEY) gets special handling as it must read the
terminal's keyboard.""")

comment(0x90CD, """\
Fn 8: remote OSWORD handler (NWORD)
Only intercepts OSWORD 7 (make a sound) and OSWORD 8 (define an
envelope). Unlike NBYTE which returns results, NWORD is entirely
fire-and-forget — no return path is implemented. The developer
explicitly noted this was acceptable since sound/envelope commands
don't return meaningful results. Copies up to 14 parameter bytes
from the RX buffer to workspace, tags the message as RWORD, and
transmits.""")

comment(0x91B5, """\
Fn 5: printer selection changed (SELECT)
Called when the printer selection changes. Compares the new
selection (in PARMX) against the network printer (buffer 4).
If it matches, initialises the printer buffer pointer (PBUFFP)
and sets the initial flag byte ($41). Otherwise just updates
the printer status flags (PFLAGS).""")

comment(0x91C7, """\
Fn 1/2/3: network printer handler (PRINT)
Handles network printer output. Reason 1 = chars in buffer (extract
from MOS buffer 3 and accumulate), reason 2 = Ctrl-B (start print),
reason 3 = Ctrl-C (end print). The printer status byte PFLAGS uses:
  bit 7 = sequence number (toggles per packet for dup detection)
  bit 6 = always 1 (validity marker)
  bit 0 = 0 when print active
Print streams reuse the BSXMIT (byte-stream transmit) code with
handle=0, which causes the AND SEQNOS to produce zero and sidestep
per-file sequence tracking. After transmission, TXCB pointer bytes
are filled with $FF to prevent stale values corrupting subsequent
BGET/BPUT operations (a historically significant bug fix).
N.B. The printer and REMOTE facility share the same dynamically
allocated static workspace page via WORKP1 ($9E,$9F) — care must
be taken to never leave the pointer corrupted, as corruption would
cause one subsystem to overwrite the other's data.
Only handles buffer 4 (network printer); others are ignored.""")

comment(0x91EC, """\
Store output byte to network buffer
Stores byte A at the current output offset in the RX buffer
pointed to by (net_rx_ptr). Advances the offset counter and
triggers a flush if the buffer is full.""")

comment(0x9217, """\
Flush output block
Sends the accumulated output block over the network, resets the
buffer pointer, and prepares for the next block of output data.""")

comment(0x92DD, """\
Save VDU workspace state
Stores the cursor position value from $0355 into NFS workspace,
then reads cursor position (OSBYTE $85), shadow RAM (OSBYTE $C2),
and screen start (OSBYTE $C3) via read_vdu_osbyte, storing
each result into consecutive workspace bytes.""")

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
Tube BRK handler (BRKV target) — reference: NFS11 NEWBR
Sends error information to the Tube co-processor via R2 and R4:
  1. Sends $FF to R4 (WRIFOR) to signal error
  2. Reads R2 data (flush any pending byte)
  3. Sends $00 via R2, then error number from ($FD),0
  4. Loops sending error string bytes via R2 until zero terminator
  5. Falls through to tube_reset_stack → tube_main_loop
The main loop continuously polls R1 for WRCH requests (forwarded
to OSWRITCH $FFCB) and R2 for command bytes (dispatched via the
14-entry table at $0500). The R2 command byte is stored at $55
before dispatch via JMP ($0500).""")

comment(0x0400, """\
Tube host code page 4 — reference: NFS12 (BEGIN, ADRR, SENDW)
Copied from ROM at $934C during init. The first 28 bytes ($0400-$041B)
overlap with the end of the ZP block (the same ROM bytes serve both
the ZP copy at $005B-$0076 and this page at $0400-$041B). Contains:
  $0400: JMP $0473 (BEGIN — CLI parser / startup entry)
  $0403: JMP $06E2 (tube_escape_check)
  $0406: tube_addr_claim — Tube address claim protocol (ADRR)
  $0414: tube_post_init — called after ROM→RAM copy
  $0473: BEGIN — startup/CLI entry, break type check
  $04E7: tube_rdch_handler — RDCHV target
  $04EF: tube_restore_regs — restore X,Y, dispatch entry 6
  $04F7: tube_read_r2 — poll R2 status, read data byte to A""")

comment(0x0500, """\
Tube host code page 5 — reference: NFS13 (TASKS, BPUT-FILE)
Copied from ROM at $944C during init. Contains:
  $0500: tube_dispatch_table — 14-entry handler address table
  $051C: tube_wrch_handler — WRCHV target
  $051F: tube_send_and_poll — send byte via R2, poll for reply
  $0527: tube_poll_r1_wrch — service R1 WRCH while waiting for R2
  $053D: tube_release_return — restore regs and RTS
  $0543: tube_osbput — write byte to file
  $0550: tube_osbget — read byte from file
  $055B: tube_osrdch — read character
  $0569: tube_osfind — open file
  $0580: tube_osfind_close — close file (A=0)
  $058C: tube_osargs — file argument read/write
  $05B1: tube_read_string — read CR-terminated string into $0700
  $05C5: tube_oscli — execute * command
  $05CB: tube_reply_ack — send $7F acknowledge
  $05CD: tube_reply_byte — send byte and return to main loop
  $05D8: tube_osfile — whole file operation""")

comment(0x0600, """\
Tube host code page 6 — reference: NFS13 (GBPB-ESCA)
Copied from ROM at $954C during init. $0600-$0601 is the tail
of tube_osfile (BEQ to tube_reply_byte when done). Contains:
  $0602: tube_osgbpb — multi-byte file I/O
  $0626: tube_osbyte_short — 2-param OSBYTE (returns X)
  $063B: tube_osbyte_long — 3-param OSBYTE (returns carry+Y+X)
  $065D: tube_osword — variable-length OSWORD (buffer at $0130)
  $06A3: tube_osword_rdln — OSWORD 0 (read line, 5-byte params)
  $06BB: tube_rdln_send_line — send input line from $0700
  $06D0: tube_send_r2 — poll R2 status, write A to R2 data
  $06D9: tube_send_r4 — poll R4 status, write A to R4 data
  $06E2: tube_escape_check — check $FF, forward escape to R1
  $06E8: tube_event_handler — EVNTV: forward event (A,X,Y) via R1
  $06F7: tube_send_r1 — poll R1 status, write A to R1 data""")

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
# Econet TX retry ($9248)
# ============================================================
comment(0x9248, """\
Transmit with retry loop (XMITFS/XMITFY)
Calls the low-level transmit routine (BRIANX) with FSTRY ($FF = 255)
retries and FSDELY ($60 = 96) ms delay between attempts. On each
iteration, checks the result code: zero means success, non-zero
means retry. After all retries exhausted, reports a 'Net error'.
Entry point XMITFY allows a custom delay in Y.""")

# ============================================================
# Save palette and VDU state ($9291)
# ============================================================
comment(0x9291, """\
Save palette and VDU state (CVIEW)
Part of the VIEW facility (second iteration, started 27/7/82).
Uses dynamically allocated buffer store. The WORKP1 pointer
($9E,$9F) serves double duty: non-zero indicates data ready AND
provides the buffer address — an efficient use of scarce zero-
page space. This code must be user-transparent as the NFS may not
be the dominant filing system.
Reads all 16 palette entries using OSWORD $0B (read palette) and
stores the results. Then reads cursor position (OSBYTE $85),
shadow RAM allocation (OSBYTE $C2), and screen start address
(OSBYTE $C3) using the 3-entry table at $9304 (osbyte_vdu_table).
On completion, restores the JSR buffer protection bits (LSTAT)
from OLDJSR to re-enable JSR reception, which was disabled during
the screen data capture to prevent interference.""")

# ============================================================
# Post-ACK scout processing ($99BB)
# ============================================================
comment(0x99BB, """\
Post-ACK scout processing
Called after the scout ACK has been transmitted. Processes the
received scout data stored in the buffer at $0D3D-$0D48.
Checks the port byte ($0D40) against open receive blocks to
find a matching listener. If a match is found, sets up the
data RX handler chain for the four-way handshake data phase.
If no match, discards the frame.""")

# ============================================================
# Immediate operation handler ($9A59)
# ============================================================
comment(0x9A59, """\
Immediate operation handler (port = 0)
Handles immediate (non-data-transfer) operations received via
scout frames with port byte = 0. The control byte ($0D3F)
determines the operation type:
  $81 = PEEK (read memory)
  $82 = POKE (write memory)
  $83 = JSR (remote procedure call)
  $84 = user procedure
  $85 = OS procedure
  $86 = HALT
  $87 = CONTINUE
The protection mask (LSTAT at $D63) controls which operations
are permitted — each bit enables or disables an operation type.
If the operation is not permitted by the mask, it is silently
ignored. LSTAT can be read/set via OSWORD $12 sub-functions 4/5.""")

# ============================================================
# Discard paths ($9A34 / $9A40 / $9A43)
# ============================================================
comment(0x9A34, """\
Discard with full ADLC reset
Performs adlc_full_reset (CR1=$C1, reset both TX and RX sections),
then falls through to discard_after_reset. Used when the ADLC is
in an unexpected state and needs a hard reset before returning
to idle listen mode. 5 references — the main error recovery path.""")

comment(0x9A40, """\
Discard frame (gentle)
Sends RX_DISCONTINUE (CR1=$A2: RIE|RX_DISCONTINUE) to abort the
current frame reception without a full reset, then falls through
to discard_after_reset. Used for clean rejection of frames that
are correctly formatted but not for us (wrong station/network).""")

comment(0x9A43, """\
Return to idle listen after reset/discard
Just calls adlc_rx_listen (CR1=$82, CR2=$67) to re-enter idle
RX mode, then RTI. The simplest of the three discard paths —
used as the tail of both discard_reset_listen and discard_listen.""")

# ============================================================
# Transfer size calculation ($9F5B)
# ============================================================
comment(0x9F5B, """\
Calculate transfer size
Computes the number of bytes actually transferred during a data
frame reception. Subtracts the low pointer (LPTR, offset 4 in
the RXCB) from the current buffer position to get the byte count,
and stores it back into the RXCB's high pointer field (HPTR,
offset 8). This tells the caller how much data was received.""")

# ============================================================
# NMI shim at end of ROM ($9FCA-$9FFF)
# ============================================================
comment(0x9FCB, """\
Bootstrap NMI entry point (in ROM)
An alternate NMI handler that lives in the ROM itself rather than
in the RAM workspace at $0D00. Unlike the RAM shim (which uses a
self-modifying JMP to dispatch to different handlers), this one
hardcodes JMP nmi_rx_scout ($96F6). Used as the initial NMI handler
before the workspace has been properly set up during initialisation.
Same sequence as the RAM shim: BIT $FE18 (INTOFF), PHA, TYA, PHA,
LDA romsel, STA $FE30, JMP $96F6.""")

comment(0x9FD9, """\
ROM copy of set_nmi_vector + nmi_rti
A version of the NMI vector-setting subroutine and RTI sequence
that lives in ROM. The RAM workspace copy at $0D0E/$0D14 is the
one normally used at runtime; this ROM copy is used during early
initialisation before the RAM workspace has been set up, and as
the source for the initial copy to RAM.""")

# ============================================================
# Secondary dispatch entries (indices 19-32)
# ============================================================
# These are accessed via callers at $8079 and $808C which set
# different Y base offsets. They handle *-command parsing and
# filing system operations. The exact mapping of indices to
# handlers hasn't been fully traced yet.

# ============================================================
# OSWORD $12 handler detail
# ============================================================
# OSWORD $12 (RS) dispatches on sub-function codes 0-9:
# read/set FS station, printer station, protection masks,
# context handles (URD/CSD/LIB), local station, JSR buffer size.
# Uses the bidirectional copy at $8E22 to transfer data between
# the OSWORD parameter block and the FS workspace.

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
