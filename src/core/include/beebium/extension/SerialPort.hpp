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

#ifndef BEEBIUM_EXTENSION_SERIAL_PORT_HPP
#define BEEBIUM_EXTENSION_SERIAL_PORT_HPP

#include "../serial/SerialDevice.hpp"
#include "../serial/SerialSocket.hpp"

#include <concepts>
#include <stdexcept>

namespace beebium {

// Port handle for the BBC Micro's serial port (RS423) -- the UserPort analogue.
//
// Manages attachment of a single SerialPortDevice to the on-board serial
// hardware (MC6850 ACIA + Serial ULA). A device receives bytes the Beeb
// transmits (add_byte) and supplies bytes for the Beeb to receive
// (has_data/next_byte); the Serial ULA drives both directions through the
// attached device.
//
// Only one device can be attached at a time -- the serial port has a single
// physical connector. Attempting to attach a second device throws.
//
// Attachment is NON-OWNING: the device's lifetime is the caller's
// responsibility, exactly as for UserPort::attach(UserPortDevice&). A
// PeripheralExtension that is itself the device simply attaches *this (it lives
// for the process lifetime).
class SerialPort {
public:
    explicit SerialPort(SerialSocket& socket) : socket_(socket) {}

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // Attach a device to the serial port.
    // Throws std::runtime_error if a device is already attached.
    void attach(SerialPortDevice& device) {
        if (device_) {
            throw std::runtime_error(
                "SerialPort: a device is already attached to the serial port");
        }
        device_ = &device;
        socket_.set_device(&device);
    }

    // Detach the current device, if any (the receive line idles and
    // transmitted bytes are discarded).
    void detach() {
        device_ = nullptr;
        socket_.set_device(nullptr);
    }

    // Query whether a device is currently attached.
    bool is_occupied() const { return device_ != nullptr; }

private:
    SerialSocket& socket_;
    SerialPortDevice* device_ = nullptr;
};

// Concept to detect hardware that has a serial port handle.
template<typename T>
concept HasSerialPort = requires(T& hw) {
    { hw.serial_port() } -> std::convertible_to<SerialPort&>;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_SERIAL_PORT_HPP
