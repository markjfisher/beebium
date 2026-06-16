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

#include "RpcSerialExtension.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#ifdef BEEBIUM_BUILD_SERVICE
#include "RpcSerialDispatcher.hpp"
#endif

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

namespace beebium {

RpcSerialExtension::RpcSerialExtension() = default;
RpcSerialExtension::~RpcSerialExtension() = default;

std::span<const std::string_view> RpcSerialExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"serial-port"};
    return deps;
}

std::span<const std::string_view> RpcSerialExtension::provides() const {
    return {};
}

void RpcSerialExtension::init(ExtensionContext& ctx) {
    std::size_t tx_buffer = serial::kDefaultTxBackPressure;
    if (auto value = config_value("tx_buffer"); value && !value->empty()) {
        const long parsed = std::atol(std::string(*value).c_str());
        if (parsed > 0) {
            tx_buffer = static_cast<std::size_t>(parsed);
        }
    }
    endpoint_ = std::make_unique<RpcSerialEndpoint>(tx_buffer);
    ctx.get<SerialPort>().attach(*endpoint_);
    std::cout << "rpc-serial: client-driven serial peer attached "
                 "(drive it via the RpcSerial service)\n";
}

void RpcSerialExtension::shutdown() {
#ifdef BEEBIUM_BUILD_SERVICE
    dispatcher_.reset();
#endif
    endpoint_.reset();
}

std::vector<ExtensionRpcDispatcher*> RpcSerialExtension::rpc_dispatchers() {
#ifdef BEEBIUM_BUILD_SERVICE
    if (!endpoint_) {
        return {};  // init() not yet called
    }
    if (!dispatcher_) {
        dispatcher_ = std::make_unique<RpcSerialDispatcher>(*endpoint_);
    }
    return {dispatcher_.get()};
#else
    return {};
#endif
}

}  // namespace beebium
