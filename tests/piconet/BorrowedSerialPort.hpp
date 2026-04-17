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

#pragma once

// Non-owning SerialPort adapter for tests where the backing SerialPort
// outlives the PiconetBackend that uses it. PiconetBackend's constructor
// takes a std::unique_ptr<SerialPort> -- it owns and destroys the port.
// In some test scenarios we want the port to remain alive after the
// backend is destroyed (e.g. for further inspection or reuse), or to be
// shared across multiple backends. BorrowedSerialPort holds a raw
// SerialPort* and forwards every method to it; close() is intentionally
// a no-op so the borrowed lifecycle is unaffected.

#include "beebium/econet/piconet/SerialPort.hpp"

#include <cstdint>
#include <span>

namespace beebium::piconet::test {

class BorrowedSerialPort : public SerialPort {
public:
    explicit BorrowedSerialPort(SerialPort* inner) : inner_(inner) {}

    ReadResult read(std::span<std::uint8_t> buffer) override {
        return inner_->read(buffer);
    }
    WriteResult write(std::span<const std::uint8_t> bytes) override {
        return inner_->write(bytes);
    }
    bool is_open() const override { return inner_->is_open(); }

    // Intentional no-op: the borrowed port's lifecycle belongs to the
    // owner, not to the PiconetBackend that holds the BorrowedSerialPort.
    void close() override {}

private:
    SerialPort* inner_;
};

}  // namespace beebium::piconet::test
