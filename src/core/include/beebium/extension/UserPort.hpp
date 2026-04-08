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

#ifndef BEEBIUM_EXTENSION_USER_PORT_HPP
#define BEEBIUM_EXTENSION_USER_PORT_HPP

#include "Export.hpp"
#include "UserPortDevice.hpp"

#include <concepts>
#include <memory>

namespace beebium {

class Via6522;
class ViaPeripheral;

// Port handle for the BBC Micro's User Port.
//
// Manages attachment of a single UserPortDevice to the User VIA's
// Port B and CB1/CB2 lines. Internally creates a ViaPeripheral bridge
// that forwards only Port B and CB1/CB2 calls to the attached device,
// leaving Port A and CA1/CA2 at their defaults.
//
// Only one device can be attached at a time -- the User Port has a
// single physical connector. Attempting to attach a second device throws.
class BEEBIUM_EXT_API UserPort {
public:
    explicit UserPort(Via6522& user_via);
    ~UserPort();

    // Attach a device to the User Port.
    // Throws std::runtime_error if a device is already attached.
    void attach(UserPortDevice& device);

    // Query whether a device is currently attached.
    bool is_occupied() const;

private:
    Via6522& user_via_;
    UserPortDevice* device_ = nullptr;

    // Bridge: implements ViaPeripheral, forwards Port B and CB1/CB2
    // to the attached UserPortDevice. Port A and CA1/CA2 return defaults.
    class Bridge;
    std::unique_ptr<Bridge> bridge_;
};

// Concept to detect hardware that has a User Port.
template<typename T>
concept HasUserPort = requires(T& hw) {
    { hw.user_port() } -> std::convertible_to<UserPort&>;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_USER_PORT_HPP
