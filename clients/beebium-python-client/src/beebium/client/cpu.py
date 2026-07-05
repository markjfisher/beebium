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
from beebium.client.exceptions import DebuggerError


@dataclass
class Registers:
    """6502 CPU register snapshot."""

    a: int  # Accumulator (0-255)
    x: int  # X index register (0-255)
    y: int  # Y index register (0-255)
    sp: int  # Stack pointer (0-255, stack at $0100-$01FF)
    pc: int  # Program counter (0-65535)
    p: int  # Processor status flags

    # Interrupt handler tracking
    in_nmi_handler: bool = False
    in_irq_handler: bool = False
    nmi_pending: bool = False
    irq_pending: bool = False
    device_irq_flags: int = 0
    device_nmi_flags: int = 0

    # Flag accessors

    @property
    def carry(self) -> bool:
        """Carry flag (bit 0)."""
        return bool(self.p & 0x01)

    @property
    def zero(self) -> bool:
        """Zero flag (bit 1)."""
        return bool(self.p & 0x02)

    @property
    def interrupt_disable(self) -> bool:
        """Interrupt disable flag (bit 2)."""
        return bool(self.p & 0x04)

    @property
    def decimal(self) -> bool:
        """Decimal mode flag (bit 3)."""
        return bool(self.p & 0x08)

    @property
    def break_flag(self) -> bool:
        """Break flag (bit 4)."""
        return bool(self.p & 0x10)

    @property
    def overflow(self) -> bool:
        """Overflow flag (bit 6)."""
        return bool(self.p & 0x40)

    @property
    def negative(self) -> bool:
        """Negative flag (bit 7)."""
        return bool(self.p & 0x80)

    def __str__(self) -> str:
        """Format registers for display."""
        flags = (
            ("N" if self.negative else "n")
            + ("V" if self.overflow else "v")
            + "-"
            + ("B" if self.break_flag else "b")
            + ("D" if self.decimal else "d")
            + ("I" if self.interrupt_disable else "i")
            + ("Z" if self.zero else "z")
            + ("C" if self.carry else "c")
        )
        result = (
            f"A={self.a:02X} X={self.x:02X} Y={self.y:02X} "
            f"SP={self.sp:02X} PC={self.pc:04X} P={self.p:02X} [{flags}]"
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


class CPU:
    """6502 CPU register access.

    Provides both read and write access to CPU registers with
    property-based syntax.

    Usage:
        # Read all registers
        regs = bbc.cpu.registers
        print(f"A={regs.a:02X} X={regs.x:02X} PC={regs.pc:04X}")

        # Read individual registers
        if bbc.cpu.a == 0:
            ...

        # Write registers
        bbc.cpu.pc = 0xC000
        bbc.cpu.a = 0x42
    """

    def __init__(self, stub: debugger_pb2_grpc.DebuggerControlStub):
        """Create a CPU interface.

        Args:
            stub: The gRPC stub for the DebuggerControl service.
        """
        self._stub = stub

    @property
    def registers(self) -> Registers:
        """Read all registers at once."""
        response = self._stub.Get6502State(debugger_pb2.Get6502StateRequest())
        return Registers(
            a=response.a,
            x=response.x,
            y=response.y,
            sp=response.sp,
            pc=response.pc,
            p=response.p,
            in_nmi_handler=response.in_nmi_handler,
            in_irq_handler=response.in_irq_handler,
            nmi_pending=response.nmi_pending,
            irq_pending=response.irq_pending,
            device_irq_flags=response.device_irq_flags,
            device_nmi_flags=response.device_nmi_flags,
        )

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
        self._write(a=value)

    @x.setter
    def x(self, value: int) -> None:
        self._write(x=value)

    @y.setter
    def y(self, value: int) -> None:
        self._write(y=value)

    @sp.setter
    def sp(self, value: int) -> None:
        self._write(sp=value)

    @pc.setter
    def pc(self, value: int) -> None:
        self._write(pc=value)

    @p.setter
    def p(self, value: int) -> None:
        self._write(p=value)

    def _write(
        self,
        a: int | None = None,
        x: int | None = None,
        y: int | None = None,
        sp: int | None = None,
        pc: int | None = None,
        p: int | None = None,
    ) -> None:
        """Write one or more registers.

        Only the registers that are explicitly provided will be modified.
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

        response = self._stub.Set6502State(request)
        if not response.success:
            raise DebuggerError("Failed to write registers")
