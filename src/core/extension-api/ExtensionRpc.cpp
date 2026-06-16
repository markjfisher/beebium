// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

// Vtable anchors + the default server_stream() for the ExtensionRpc ABI.
// As with ExtensionUi, the out-of-line virtual definitions live in the
// exporting DLL (beebium_extension_api) so consumer modules resolve the
// vtables/typeinfo across the shared-library boundary.

#include "beebium/extension/ExtensionRpc.hpp"

namespace beebium {

RpcContext::~RpcContext() = default;
RpcResponseWriter::~RpcResponseWriter() = default;
ExtensionRpcDispatcher::~ExtensionRpcDispatcher() = default;

RpcStatus ExtensionRpcDispatcher::server_stream(std::string_view /*method*/,
                                                std::string_view /*request*/,
                                                RpcResponseWriter& /*writer*/,
                                                RpcContext& /*ctx*/) {
    return RpcStatus::error(kRpcUnimplemented,
                            "this dispatcher does not implement server streaming");
}

}  // namespace beebium
