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

"""6502 CPU register access for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client._proto import debugger_pb2, debugger_pb2_grpc


@dataclass(frozen=True)
class StatusRegister:
    """The 6502 processor status register (P), decoded into named flags.

    An immutable value object wrapping the raw status byte. Bit positions
    follow the NMOS 6502 (bit 5 is unused and always reads as set).
    """

    value: int  # Raw status byte (0-255)

    @property
    def carry(self) -> bool:
        """Carry flag (bit 0)."""
        return bool(self.value & 0x01)

    @property
    def zero(self) -> bool:
        """Zero flag (bit 1)."""
        return bool(self.value & 0x02)

    @property
    def interrupt_disable(self) -> bool:
        """Interrupt disable flag (bit 2)."""
        return bool(self.value & 0x04)

    @property
    def decimal(self) -> bool:
        """Decimal mode flag (bit 3)."""
        return bool(self.value & 0x08)

    @property
    def break_flag(self) -> bool:
        """Break flag (bit 4)."""
        return bool(self.value & 0x10)

    @property
    def overflow(self) -> bool:
        """Overflow flag (bit 6)."""
        return bool(self.value & 0x40)

    @property
    def negative(self) -> bool:
        """Negative flag (bit 7)."""
        return bool(self.value & 0x80)

    def __int__(self) -> int:
        return self.value

    def __str__(self) -> str:
        """Render as the conventional flag string (uppercase = set)."""
        return (
            ("N" if self.negative else "n")
            + ("V" if self.overflow else "v")
            + "-"
            + ("B" if self.break_flag else "b")
            + ("D" if self.decimal else "d")
            + ("I" if self.interrupt_disable else "i")
            + ("Z" if self.zero else "z")
            + ("C" if self.carry else "c")
        )


@dataclass(frozen=True)
class Registers:
    """An immutable snapshot of the 6502 CPU registers.

    ``bbc.cpu.registers`` returns one coherent snapshot. Registers are never
    written by mutating a snapshot (that would mean nothing) -- writes go
    through ``bbc.cpu.update(...)`` or the individual setters -- hence frozen.
    """

    a: int  # Accumulator (0-255)
    x: int  # X index register (0-255)
    y: int  # Y index register (0-255)
    sp: int  # Stack pointer (0-255, stack at $0100-$01FF)
    pc: int  # Program counter (0-65535)
    p: int  # Raw processor status byte

    # Interrupt handler tracking
    in_nmi_handler: bool = False
    in_irq_handler: bool = False
    nmi_pending: bool = False
    irq_pending: bool = False
    device_irq_flags: int = 0
    device_nmi_flags: int = 0

    @property
    def status(self) -> StatusRegister:
        """The processor status register (P) decoded into named flags."""
        return StatusRegister(self.p)

    def __str__(self) -> str:
        """Format registers for display."""
        result = (
            f"A={self.a:02X} X={self.x:02X} Y={self.y:02X} "
            f"SP={self.sp:02X} PC={self.pc:04X} P={self.p:02X} [{self.status}]"
        )
        interrupts = []
        if self.in_nmi_handler:
            interrupts.append("in-NMI")
        if self.in_irq_handler:
            interrupts.append("in-IRQ")
        if self.nmi_pending:
            interrupts.append("NMI-pending")
        if self.irq_pending:
            interrupts.append("IRQ-pending")
        if interrupts:
            result += f" {{{', '.join(interrupts)}}}"
        return result


def _registers_from_proto(state: debugger_pb2.Cpu6502State) -> Registers:
    """Build a Registers snapshot from a Cpu6502State proto message."""
    return Registers(
        a=state.a,
        x=state.x,
        y=state.y,
        sp=state.sp,
        pc=state.pc,
        p=state.p,
        in_nmi_handler=state.in_nmi_handler,
        in_irq_handler=state.in_irq_handler,
        nmi_pending=state.nmi_pending,
        irq_pending=state.irq_pending,
        device_irq_flags=state.device_irq_flags,
        device_nmi_flags=state.device_nmi_flags,
    )


class CPU:
    """6502 CPU register access.

    Reads return a coherent snapshot; writes are atomic and return the
    resulting snapshot.

    Usage:
        # Read all registers as one coherent snapshot (one request)
        regs = bbc.cpu.registers
        print(regs)                     # A=.. X=.. ... PC=.. P=.. [flags]
        if regs.status.carry:
            ...

        # Convenience single-register access (each read is its own snapshot)
        if bbc.cpu.a == 0:
            ...

        # Atomic partial write; returns the complete new register state
        new = bbc.cpu.update(pc=0xC000, a=0x42)

        # The individual setters route through update()
        bbc.cpu.pc = 0xC000
    """

    def __init__(self, stub: debugger_pb2_grpc.DebuggerControlStub):
        """Create a CPU interface.

        Args:
            stub: The gRPC stub for the DebuggerControl service.
        """
        self._stub = stub

    @property
    def registers(self) -> Registers:
        """Read all registers as one coherent snapshot."""
        response = self._stub.Get6502State(debugger_pb2.Get6502StateRequest())
        return _registers_from_proto(response)

    # Individual register properties (read)

    @property
    def a(self) -> int:
        """Accumulator (0-255)."""
        return self.registers.a

    @property
    def x(self) -> int:
        """X index register (0-255)."""
        return self.registers.x

    @property
    def y(self) -> int:
        """Y index register (0-255)."""
        return self.registers.y

    @property
    def sp(self) -> int:
        """Stack pointer (0-255)."""
        return self.registers.sp

    @property
    def pc(self) -> int:
        """Program counter (0-65535)."""
        return self.registers.pc

    @property
    def p(self) -> int:
        """Processor status flags (0-255)."""
        return self.registers.p

    # Individual register setters

    @a.setter
    def a(self, value: int) -> None:
        self.update(a=value)

    @x.setter
    def x(self, value: int) -> None:
        self.update(x=value)

    @y.setter
    def y(self, value: int) -> None:
        self.update(y=value)

    @sp.setter
    def sp(self, value: int) -> None:
        self.update(sp=value)

    @pc.setter
    def pc(self, value: int) -> None:
        self.update(pc=value)

    @p.setter
    def p(self, value: int) -> None:
        self.update(p=value)

    def update(
        self,
        *,
        a: int | None = None,
        x: int | None = None,
        y: int | None = None,
        sp: int | None = None,
        pc: int | None = None,
        p: int | None = None,
    ) -> Registers:
        """Atomically write one or more registers and return the new snapshot.

        Only the registers explicitly provided are modified; the rest are left
        unchanged. The server applies the writes and reads back the resulting
        state as a single operation, so the returned ``Registers`` is a
        coherent post-write snapshot -- there is no separate read and no race.
        """
        request = debugger_pb2.Set6502StateRequest()
        if a is not None:
            request.a = a
        if x is not None:
            request.x = x
        if y is not None:
            request.y = y
        if sp is not None:
            request.sp = sp
        if pc is not None:
            request.pc = pc
        if p is not None:
            request.p = p

        return _registers_from_proto(self._stub.Set6502State(request))
