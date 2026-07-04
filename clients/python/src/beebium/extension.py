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

"""Client-side extension-adapter framework.

An *extension adapter* is a typed Python client for one Beebium server
extension: it knows the extension's logical RPC service name and messages and
exposes a friendly façade over the core's ExtensionRpc channel. Adapters are
registered as ``beebium.ext`` entry points (keyed by the kebab-case server
manifest name) and discovered via stevedore, so a third-party extension can
ship its own adapter and have it picked up with no change to the core.

This module holds the ``ExtensionAdapter`` base class and the list/describe/
create factory over the entry-point registry. Binding a *loaded* extension to
an adapter instance is done by the ``bbc.extensions`` bridge (see
``beebium.extensions``); the pattern mirrors sixty-north/asyoulikeit and
demonstrable-visning.
"""

from __future__ import annotations

import inspect
from abc import ABC
from collections.abc import Iterator
from typing import TYPE_CHECKING, ClassVar, Self

import stevedore
import stevedore.driver
import stevedore.exception

from beebium.exceptions import ExtensionAdapterNotInstalledError, ExtensionError
from beebium.extension_rpc import ExtensionChannel

if TYPE_CHECKING:
    from beebium.client import Beebium

# The entry-point group third-party adapters register into. The key of each
# entry point is the server manifest name (kebab-case: "aun", "host-serial").
ENTRY_POINT_GROUP = "beebium.ext"


class ExtensionAdapter(ABC):
    """Base class for a typed client adapter of a Beebium server extension.

    Subclasses set :attr:`EXTENSION_NAME` to the server manifest name they
    drive, and add typed methods that tunnel their extension's messages over
    the shared ExtensionRpc channel. Metadata (name, description, version) is
    derived from the class the way the reference frameworks do it -- the
    description is the class docstring.
    """

    #: The server manifest name (== ``beebium.ext`` entry-point key) this
    #: adapter drives. Subclasses MUST set this.
    EXTENSION_NAME: ClassVar[str] = ""

    def __init__(
        self,
        name: str,
        channel: ExtensionChannel,
        *,
        extension_id: str = "",
    ):
        """Bind the adapter to a live channel and (optionally) an instance.

        Args:
            name: The extension's manifest name (its entry-point key).
            channel: The ExtensionRpc channel that carries this extension's
                messages.
            extension_id: The server-assigned instance id to target. Empty
                routes by service name (correct while an extension type is a
                singleton, the common case).
        """
        self._name = name
        self._channel = channel
        self._extension_id = extension_id

    @property
    def name(self) -> str:
        """The extension's manifest name."""
        return self._name

    @property
    def extension_id(self) -> str:
        """The server-assigned instance id this adapter targets (may be empty)."""
        return self._extension_id

    def _invoke_bytes(self, service: str, method: str, payload: bytes) -> bytes:
        """Unary call routed to this adapter's instance. Returns reply bytes."""
        return self._channel.invoke(
            service, method, payload, extension_id=self._extension_id
        )

    def _server_stream_bytes(
        self, service: str, method: str, payload: bytes
    ) -> Iterator[bytes]:
        """Server-streaming call routed to this instance. Yields reply bytes."""
        yield from self._channel.server_stream(
            service, method, payload, extension_id=self._extension_id
        )

    @classmethod
    def describe(cls, *, single_line: bool = False) -> str:
        """A description of the adapter, taken from its class docstring."""
        doc = inspect.getdoc(cls)
        if not doc:
            return "No description available."
        doc = doc.strip()
        if single_line:
            return doc.splitlines()[0]
        return doc

    @classmethod
    def version(cls) -> str:
        """The adapter's version. Override to report a real version."""
        return "1.0.0"

    @classmethod
    def attach(cls, bbc: Beebium) -> Self:
        """Return this adapter bound to ``bbc``, typed as the concrete class.

        The concrete-first equivalent of ``bbc.extensions[cls]``: use whichever
        reads better. Raises :class:`ExtensionNotLoadedError` if the server has
        not loaded this extension.
        """
        return bbc.extensions[cls]


def _on_load_failure(manager, entrypoint, exception) -> None:
    """Turn a stevedore load failure into a beebium ExtensionError."""
    raise ExtensionError(
        f"Could not load the {entrypoint.name!r} adapter from the "
        f"{manager.namespace!r} entry-point group: {exception}"
    ) from exception


def installed_adapter_names() -> list[str]:
    """The names of every installed ``beebium.ext`` adapter (no instantiation)."""
    manager = stevedore.ExtensionManager(
        namespace=ENTRY_POINT_GROUP,
        invoke_on_load=False,
        on_load_failure_callback=_on_load_failure,
    )
    return sorted(manager.names())


def adapter_type(name: str) -> type[ExtensionAdapter]:
    """The adapter *class* registered for ``name`` (no instantiation).

    Raises:
        ExtensionAdapterNotInstalledError: if no adapter is registered for
            ``name``.
    """
    try:
        manager = stevedore.driver.DriverManager(
            namespace=ENTRY_POINT_GROUP,
            name=name,
            invoke_on_load=False,
            on_load_failure_callback=_on_load_failure,
        )
    except stevedore.exception.NoMatches:
        installed = installed_adapter_names()
        raise ExtensionAdapterNotInstalledError(
            f"No client adapter is installed for extension {name!r}. "
            f"Installed adapters: {', '.join(installed) or '(none)'}."
        ) from None
    return manager.driver


def describe_adapter(name: str, *, single_line: bool = False) -> str:
    """Describe the adapter registered for ``name`` without instantiating it."""
    return adapter_type(name).describe(single_line=single_line)


def create_adapter(
    name: str, channel: ExtensionChannel, extension_id: str = ""
) -> ExtensionAdapter:
    """Instantiate the adapter registered for ``name``, bound to ``channel``."""
    cls = adapter_type(name)
    return cls(name, channel, extension_id=extension_id)
