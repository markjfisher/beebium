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

"""WFSINIT hang diagnostic test.

Boots a BBC Model B with ROM/RAM board, Tube 65C02, SCSI hard disc, and DFS
floppy, then runs the Acorn WFSINIT v0.90 utility. WFSINIT hangs after
printing "Please wait ...." — this test captures comprehensive diagnostic
state to identify the root cause.

The test is expected to FAIL (the hang is the bug we are diagnosing).
The diagnostic output in the failure message shows where the system is stuck.

Run with:
    cd integration_tests/wfsinit
    uv run pytest -m slow tests/test_wfsinit_hang.py -v -s
"""

from __future__ import annotations

import threading
import time

import grpc
import pytest

from beebium.client import Beebium
from beebium.disassemble import disassemble
from beebium.screen import screen_contains, dump_screen, read_mode7_screen

# Import SCSI gRPC stubs directly (not wrapped by the Beebium client).
from beebium._proto import scsi_host_adapter_pb2, scsi_host_adapter_pb2_grpc


def _wait_for_screen_text(bbc, text, timeout_seconds=120.0, poll_interval=0.5):
    """Wait until the given text appears on the MODE 7 screen."""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc.memory)
        screen_text = "\n".join(rows)
        if text in screen_text:
            return True
        time.sleep(poll_interval)
    return False


def _wait_for_screen_change(bbc, baseline_text, timeout_seconds=60.0, poll_interval=0.5):
    """Wait until the screen content differs from the baseline.

    Returns True if the screen changed, False on timeout.
    """
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc.memory)
        current_text = "\n".join(rows)
        if current_text != baseline_text:
            return True
        time.sleep(poll_interval)
    return False


def _collect_scsi_events(stub, stop_event):
    """Collect SCSI bus events in a background thread.

    Returns a list of ScsiBusEvent messages.
    """
    events = []
    try:
        request = scsi_host_adapter_pb2.WatchScsiBusEventsRequest(
            include_register_access=False,
        )
        stream = stub.WatchBusEvents(request, timeout=600)
        for event in stream:
            if stop_event.is_set():
                stream.cancel()
                break
            events.append(event)
    except grpc.RpcError:
        pass
    return events


def _format_scsi_event(event):
    """Format a single SCSI bus event for diagnostic output."""
    cycle = event.cycle_count
    kind = event.WhichOneof("event")

    if kind == "phase_change":
        e = event.phase_change
        return f"  [{cycle:>12d}] PHASE: {e.from_phase} -> {e.to_phase}"

    if kind == "command":
        e = event.command
        cdb_hex = " ".join(f"{b:02X}" for b in e.cdb)
        return (
            f"  [{cycle:>12d}] CMD:   target={e.target_id} "
            f"op=0x{e.opcode:02X} ({e.opcode_name}) "
            f"LBA={e.lba} blocks={e.block_count} CDB=[{cdb_hex}]"
        )

    if kind == "data_transfer":
        e = event.data_transfer
        return (
            f"  [{cycle:>12d}] DATA:  {e.direction} "
            f"{e.bytes_transferred}/{e.bytes_expected} bytes "
            f"{'COMPLETE' if e.complete else 'in progress'}"
        )

    if kind == "status":
        e = event.status
        return (
            f"  [{cycle:>12d}] STATUS: target={e.target_id} "
            f"status=0x{e.status_byte:02X} ({e.status_name}) "
            f"msg=0x{e.message_byte:02X}"
        )

    if kind == "selection":
        e = event.selection
        return (
            f"  [{cycle:>12d}] SELECT: target={e.target_id} "
            f"{'OK' if e.success else 'FAILED'}"
        )

    if kind == "register_access":
        e = event.register_access
        return (
            f"  [{cycle:>12d}] REG:   {e.operation} "
            f"{e.register_name}[{e.register_index}] = 0x{e.value:02X} "
            f"phase={e.phase}"
        )

    return f"  [{cycle:>12d}] UNKNOWN: {kind}"


def _dump_diagnostics(bbc, scsi_events):
    """Stop both processors and dump comprehensive diagnostic state."""
    lines = []
    lines.append("")
    lines.append("=" * 70)
    lines.append("WFSINIT HANG DIAGNOSTIC DUMP")
    lines.append("=" * 70)

    # 1. Screen contents
    lines.append("")
    lines.append("--- SCREEN ---")
    lines.append(dump_screen(bbc.memory))

    # 2. Host CPU state
    try:
        bbc.debugger.stop()
        host_regs = bbc.cpu.registers
        lines.append("--- HOST CPU ---")
        lines.append(f"  PC=${host_regs.pc:04X}  A=${host_regs.a:02X}  "
                      f"X=${host_regs.x:02X}  Y=${host_regs.y:02X}  "
                      f"SP=${host_regs.sp:02X}  P=${host_regs.p:02X}")

        # Disassembly around PC
        host_pc = host_regs.pc
        start = max(0, host_pc - 16)
        code_bytes = bytes(bbc.memory.address.peek[start:start + 48])
        lines.append(f"  Code around ${host_pc:04X}:")
        for line in disassemble(code_bytes, start=start, length=48):
            marker = " >>>" if line.startswith(f"${host_pc:04X}") else "    "
            lines.append(f"  {marker} {line}")
    except Exception as e:
        lines.append(f"  [Host CPU state unavailable: {e}]")

    # 3. Parasite CPU state
    try:
        parasite = bbc.connect_parasite()
        parasite.debugger.stop()
        para_regs = parasite.cpu.registers
        lines.append("")
        lines.append("--- PARASITE CPU ---")
        lines.append(f"  PC=${para_regs.pc:04X}  A=${para_regs.a:02X}  "
                      f"X=${para_regs.x:02X}  Y=${para_regs.y:02X}  "
                      f"SP=${para_regs.sp:02X}  P=${para_regs.p:02X}")

        para_pc = para_regs.pc
        start = max(0, para_pc - 16)
        code_bytes = bytes(parasite.memory.address.peek[start:start + 48])
        lines.append(f"  Code around ${para_pc:04X}:")
        for line in disassemble(code_bytes, start=start, length=48):
            marker = " >>>" if line.startswith(f"${para_pc:04X}") else "    "
            lines.append(f"  {marker} {line}")
    except Exception as e:
        lines.append(f"  [Parasite CPU state unavailable: {e}]")

    # 4. Tube ULA state
    try:
        tube_state = bbc.tube_ula.state
        lines.append("")
        lines.append("--- TUBE ULA ---")
        lines.append(str(tube_state))
    except Exception as e:
        lines.append(f"  [Tube ULA state unavailable: {e}]")

    # 5. SCSI bus status
    try:
        scsi_channel = grpc.insecure_channel(bbc.target)
        scsi_stub = scsi_host_adapter_pb2_grpc.ScsiHostAdapterServiceStub(scsi_channel)

        bus_status = scsi_stub.GetBusStatus(
            scsi_host_adapter_pb2.GetScsiBusStatusRequest()
        )
        lines.append("")
        lines.append("--- SCSI BUS STATUS ---")
        lines.append(f"  Phase: {bus_status.phase}")
        lines.append(f"  Selected target: {bus_status.selected_target}")
        lines.append(f"  Status register: 0x{bus_status.status_register:02X}")
        lines.append(f"  IRQ pending: {bus_status.irq_pending}")

        # 6. SCSI targets
        targets = scsi_stub.ListTargets(
            scsi_host_adapter_pb2.ListScsiTargetsRequest()
        )
        lines.append("")
        lines.append("--- SCSI TARGETS ---")
        for t in targets.targets:
            lines.append(
                f"  ID {t.id}: {t.device_type} - {t.description} "
                f"(present={t.present})"
            )

        scsi_channel.close()
    except Exception as e:
        lines.append(f"  [SCSI status unavailable: {e}]")

    # 7a. Parasite stack dump
    try:
        parasite = bbc.connect_parasite()
        para_regs = parasite.cpu.registers
        sp = para_regs.sp
        lines.append("")
        lines.append("--- PARASITE STACK ---")
        lines.append(f"  SP=${sp:02X}")
        # Dump 16 bytes above SP (the return addresses and saved state)
        for i in range(16):
            addr = 0x0100 + ((sp + 1 + i) & 0xFF)
            val = parasite.memory.address.peek[addr]
            lines.append(f"  ${addr:04X}: ${val:02X}")
    except Exception as e:
        lines.append(f"  [Parasite stack unavailable: {e}]")

    # 7b. Parasite zero-page Tube state
    try:
        lines.append("")
        lines.append("--- PARASITE ZERO PAGE (TUBE STATE) ---")
        # NMIV at $0D00 (NMI vector), but it's stored in zero page at $F4-$F5
        # (transfer address) and the NMI vector itself
        for addr, name in [
            (0xF4, "Transfer addr low (&F4)"),
            (0xF5, "Transfer addr high (&F5)"),
            (0xF6, "Entry addr low (&F6)"),
            (0xF7, "Entry addr high (&F7)"),
            (0xF8, "CB pointer low (&F8)"),
            (0xF9, "CB pointer high (&F9)"),
            (0xFC, "Saved A (&FC)"),
            (0xFF, "Transfer type/escape (&FF)"),
        ]:
            val = parasite.memory.address.peek[addr]
            lines.append(f"  {name}: ${val:02X}")
        # NMI vector at $0D00
        nmi_lo = parasite.memory.address.peek[0x0D00]
        nmi_hi = parasite.memory.address.peek[0x0D01]
        lines.append(f"  NMI vector ($0D00): ${nmi_hi:02X}{nmi_lo:02X}")
    except Exception as e:
        lines.append(f"  [Parasite zero page unavailable: {e}]")

    # 7c. Host zero-page and Tube Host Code state
    try:
        lines.append("")
        lines.append("--- HOST STACK ---")
        host_regs = bbc.cpu.registers
        sp = host_regs.sp
        lines.append(f"  SP=${sp:02X}")
        for i in range(16):
            addr = 0x0100 + ((sp + 1 + i) & 0xFF)
            val = bbc.memory.address.peek[addr]
            lines.append(f"  ${addr:04X}: ${val:02X}")
    except Exception as e:
        lines.append(f"  [Host stack unavailable: {e}]")

    # 7. SCSI bus events log
    lines.append("")
    lines.append(f"--- SCSI BUS EVENTS ({len(scsi_events)} captured) ---")
    if scsi_events:
        for event in scsi_events:
            lines.append(_format_scsi_event(event))
    else:
        lines.append("  (no events captured)")

    lines.append("")
    lines.append("=" * 70)

    output = "\n".join(lines)
    print(output)
    return output


@pytest.mark.slow
@pytest.mark.timeout(300)
def test_wfsinit_diagnose_hang(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    wfsinit_ssd_filepath, scsi_hdd_filepath,
):
    """Run WFSINIT and diagnose where it hangs after 'Please wait ....'.

    This test is expected to FAIL — the hang is the bug we are diagnosing.
    The diagnostic output shows where the system is stuck.
    """
    extra_args = [
        "--sideways", f"9:rom:{anfs_filepath}",
        "--sideways", f"10:rom:{adfs_filepath}",
        "--sideways", f"11:rom:{dfs_filepath}",
        "--fdc", "acorn-1770",
        "--floppy", f"0:{wfsinit_ssd_filepath}",
        "--acorn-scsi",
        "--scsi-hdd", f"0:{scsi_hdd_filepath}",
        "--station", "254",
        "--aun-port", "0",
        "--acorn-rtc", "layout=7bit-year-in-r7:time=1985-10-21T0000",
        "--tube-65c02",
    ]

    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=server_filepath,
        extra_args=extra_args,
        startup_timeout=30.0,
    ) as bbc:
        # Phase 1: Wait for BASIC prompt (Tube boot is slower).
        assert _wait_for_screen_text(bbc, ">", timeout_seconds=30), \
            f"Boot to BASIC prompt failed:\n{dump_screen(bbc.memory)}"
        print("Boot complete, BASIC prompt visible.")

        # Phase 2: Start SCSI bus event collection in background.
        scsi_channel = grpc.insecure_channel(bbc.target)
        scsi_stub = scsi_host_adapter_pb2_grpc.ScsiHostAdapterServiceStub(scsi_channel)
        stop_event = threading.Event()
        scsi_events = []

        def collect():
            nonlocal scsi_events
            scsi_events = _collect_scsi_events(scsi_stub, stop_event)

        collector_thread = threading.Thread(target=collect, daemon=True)
        collector_thread.start()

        # Phase 3: Drive WFSINIT through its prompts.
        print("CHAINing WFSINIT...")
        bbc.keyboard.type('CHAIN "WFSINIT"\r')

        assert _wait_for_screen_text(bbc, "Drive number", timeout_seconds=60), \
            f"WFSINIT did not reach 'Drive number' prompt:\n{dump_screen(bbc.memory)}"
        print("Answering prompts...")
        bbc.keyboard.type("0\r")

        assert _wait_for_screen_text(bbc, "Disc name", timeout_seconds=30), \
            f"WFSINIT did not reach 'Disc name' prompt:\n{dump_screen(bbc.memory)}"
        bbc.keyboard.type("WFSTST\r")

        assert _wait_for_screen_text(bbc, "Next drive", timeout_seconds=30), \
            f"WFSINIT did not reach 'Next drive' prompt:\n{dump_screen(bbc.memory)}"
        bbc.keyboard.type("0\r")

        assert _wait_for_screen_text(bbc, "Date", timeout_seconds=30), \
            f"WFSINIT did not reach 'Date' prompt:\n{dump_screen(bbc.memory)}"
        bbc.keyboard.type("21/10/85\r")

        # Phase 4: Wait for "Please wait" to appear.
        assert _wait_for_screen_text(bbc, "Please wait", timeout_seconds=60), \
            f"WFSINIT did not reach 'Please wait':\n{dump_screen(bbc.memory)}"
        print("'Please wait' appeared. Waiting for progress...")

        # Phase 5: Snapshot screen and wait for any change.
        # If the screen does not change within 60 seconds, the program is hung.
        baseline_rows = read_mode7_screen(bbc.memory)
        baseline_text = "\n".join(baseline_rows)

        progressed = _wait_for_screen_change(bbc, baseline_text, timeout_seconds=60)

        # Phase 6: Stop collection and dump diagnostics.
        stop_event.set()
        collector_thread.join(timeout=5)
        scsi_channel.close()

        if progressed:
            print("WFSINIT progressed past 'Please wait' -- no hang detected!")
            print(f"Current screen:\n{dump_screen(bbc.memory)}")
            # If we get here, the bug is fixed (or was intermittent).
            return

        # Phase 7: Hang confirmed. Dump everything.
        diag_output = _dump_diagnostics(bbc, scsi_events)

        pytest.fail(
            "WFSINIT hung after 'Please wait ....' -- "
            "no screen change in 60 seconds.\n"
            "See diagnostic output above for CPU, Tube, and SCSI state."
        )
