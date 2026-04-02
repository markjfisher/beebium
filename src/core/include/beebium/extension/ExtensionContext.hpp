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
#include "PeripheralExtension.hpp"
#include "UserPort.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace beebium {

// Provides type-safe access to port handles and initialised extension
// providers during extension init().
//
// Port handles are accessed via get<PortType>().
// Extension providers are accessed via provider(extension_point_name).
class ExtensionContext {
public:
    explicit ExtensionContext(OneMHzBusPort* one_mhz_bus_port = nullptr,
                             UserPort* user_port = nullptr)
        : one_mhz_bus_port_(one_mhz_bus_port)
        , user_port_(user_port) {}

    // Type-safe port handle access.
    template<typename T>
    T& get() {
        if constexpr (std::is_same_v<T, OneMHzBusPort>) {
            if (!one_mhz_bus_port_) {
                throw std::runtime_error(
                    "ExtensionContext: 1mhz-bus port not available on this machine");
            }
            return *one_mhz_bus_port_;
        } else if constexpr (std::is_same_v<T, UserPort>) {
            if (!user_port_) {
                throw std::runtime_error(
                    "ExtensionContext: user-port not available on this machine");
            }
            return *user_port_;
        } else {
            static_assert(!std::is_same_v<T, T>, "Unknown port type");
        }
    }

    // Query whether a port type is available.
    template<typename T>
    bool has() const {
        if constexpr (std::is_same_v<T, OneMHzBusPort>) {
            return one_mhz_bus_port_ != nullptr;
        } else if constexpr (std::is_same_v<T, UserPort>) {
            return user_port_ != nullptr;
        } else {
            return false;
        }
    }

    // Access an initialised extension that provides a named extension point.
    // The dependency resolver guarantees the provider is initialised before
    // any extension that attaches_to the same extension point.
    // Returns nullptr if no provider is registered for the given name.
    PeripheralExtension* provider(std::string_view extension_point) const {
        auto it = providers_.find(std::string(extension_point));
        if (it != providers_.end()) {
            return it->second;
        }
        return nullptr;
    }

    // Qualified provider lookup: find the provider of an extension point
    // that has a specific instance ID. For the case of multiple adapters
    // providing the same extension point (e.g. two SCSI adapters both
    // providing "scsi"), the child specifies which parent via its id.
    PeripheralExtension* provider(std::string_view extension_point,
                                   std::string_view instance_id) const {
        auto key = std::string(extension_point) + ":" + std::string(instance_id);
        auto it = providers_.find(key);
        if (it != providers_.end()) {
            return it->second;
        }
        return nullptr;
    }

    // Register an initialised extension as the provider of a named extension
    // point. Called by ExtensionRegistry after each extension's init().
    // Registers under both the bare name and (if the extension has an id)
    // a qualified "name:id" key for multi-provider resolution.
    void register_provider(std::string_view extension_point, PeripheralExtension* ext) {
        providers_[std::string(extension_point)] = ext;
        auto ext_id = ext->id();
        if (!ext_id.empty()) {
            providers_[std::string(extension_point) + ":" + std::string(ext_id)] = ext;
        }
    }

private:
    OneMHzBusPort* one_mhz_bus_port_;
    UserPort* user_port_;
    std::unordered_map<std::string, PeripheralExtension*> providers_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_CONTEXT_HPP
