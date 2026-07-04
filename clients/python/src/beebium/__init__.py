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

"""
Beebium - Python client for the Beebium BBC Micro emulator.

Usage:
    from beebium import Beebium

    # Connect to an existing server
    with Beebium.connect() as bbc:
        bbc.debugger.stop()
        print(f"PC = ${bbc.cpu.pc:04X}")

    # Launch and manage a server
    with Beebium.launch(mos_filepath="acorn-mos_1_20.rom") as bbc:
        bbc.keyboard.type("PRINT 42")
        bbc.keyboard.press_return()
"""

from beebium.audio import (
    AudioChunk,
    AudioFormat,
    AudioSource,
    ChannelGroup,
    SourceEncoding,
)
from beebium.aun import AunStatus, PeerInfo, PeerSource
from beebium.client import Beebium
from beebium.extensions import (
    ExtensionInfo,
    Extensions,
    ParameterSchemaInfo,
    StorageDevice,
    StorageKind,
)
from beebium.econet import (
    AdlcStatus,
    EconetStatus,
    HandshakeStatus,
)
from beebium.econet_transport import TransportInfo
from beebium.extension_ui import (
    Button,
    Choice,
    Control,
    ControlKind,
    DispatchResult,
    Group,
    Indicator,
    IndicatorState,
    Label,
    SubscriptionHandle,
    TextInput,
    Toggle,
    View,
)
from beebium.piconet import PiconetStatus
from beebium.exceptions import (
    BeebiumError,
    ConnectionError,
    DebuggerError,
    DiscError,
    EconetError,
    InvalidConditionError,
    MemoryAccessError,
    ProtocolMismatchError,
    ServerNotFoundError,
    ServerStartupError,
    TimeoutError,
)
from beebium._proto.protocol_fingerprint import PROTOCOL_FINGERPRINT
from beebium.system import (
    AdvertisementState,
    MachineIdentity,
    Provenance,
    ServerStatus,
    ServerStatusEvent,
    ShutdownConditionStatus,
    ShutdownMode,
    ShutdownResponse,
)

__version__ = "0.1.0"

# Default gRPC port for beebium servers (0xBEEB = 48875)
DEFAULT_GRPC_PORT = 0xBEEB

__all__ = [
    "AdlcStatus",
    "AdvertisementState",
    "AudioChunk",
    "AudioFormat",
    "AudioSource",
    "AunStatus",
    "Beebium",
    "BeebiumError",
    "Button",
    "ChannelGroup",
    "Choice",
    "ConnectionError",
    "Control",
    "ExtensionInfo",
    "Extensions",
    "PROTOCOL_FINGERPRINT",
    "ProtocolMismatchError",
    "ControlKind",
    "DebuggerError",
    "DEFAULT_GRPC_PORT",
    "DiscError",
    "DispatchResult",
    "EconetError",
    "EconetStatus",
    "Group",
    "HandshakeStatus",
    "Indicator",
    "IndicatorState",
    "Label",
    "MachineIdentity",
    "MemoryAccessError",
    "ParameterSchemaInfo",
    "PeerInfo",
    "PeerSource",
    "PiconetStatus",
    "Provenance",
    "ServerNotFoundError",
    "ServerStartupError",
    "ServerStatus",
    "ServerStatusEvent",
    "ShutdownConditionStatus",
    "ShutdownMode",
    "ShutdownResponse",
    "SourceEncoding",
    "StorageDevice",
    "StorageKind",
    "SubscriptionHandle",
    "TextInput",
    "TimeoutError",
    "Toggle",
    "TransportInfo",
    "View",
    "__version__",
]
