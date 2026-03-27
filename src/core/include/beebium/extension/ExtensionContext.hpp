// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXTENSION_CONTEXT_HPP
#define BEEBIUM_EXTENSION_CONTEXT_HPP

#include "OneMHzBusPort.hpp"

#include <stdexcept>
#include <type_traits>

namespace beebium {

// Provides type-safe access to port handles during extension init().
//
// Each port type is accessed via get<PortType>(). New port types are added
// by adding a pointer member and an if-constexpr branch in get<>().
class ExtensionContext {
public:
    explicit ExtensionContext(OneMHzBusPort* one_mhz_bus_port = nullptr)
        : one_mhz_bus_port_(one_mhz_bus_port) {}

    template<typename T>
    T& get() {
        if constexpr (std::is_same_v<T, OneMHzBusPort>) {
            if (!one_mhz_bus_port_) {
                throw std::runtime_error(
                    "ExtensionContext: 1mhz-bus port not available on this machine");
            }
            return *one_mhz_bus_port_;
        } else {
            static_assert(!std::is_same_v<T, T>, "Unknown port type");
        }
    }

    // Query whether a port type is available.
    template<typename T>
    bool has() const {
        if constexpr (std::is_same_v<T, OneMHzBusPort>) {
            return one_mhz_bus_port_ != nullptr;
        } else {
            return false;
        }
    }

private:
    OneMHzBusPort* one_mhz_bus_port_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_CONTEXT_HPP
