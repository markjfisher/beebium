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

#ifndef BEEBIUM_EXT_IP232_CODEC_HPP
#define BEEBIUM_EXT_IP232_CODEC_HPP

#include <cstdint>
#include <span>
#include <vector>

namespace beebium::ip232 {

// The IP232 wire protocol -- a thin serial-over-TCP framing defined by tcpser
// and BeebEm (there is no formal spec). Two modes:
//
//   raw   : a pure byte pipe. No escaping. (The connection lifecycle tracks the
//           BBC's RTS line instead; that is the endpoint's job, not the codec's.)
//   ip232 : a persistent connection with 0xFF as an in-band escape/flag byte.
//           Outbound: a data byte of 0xFF is doubled -> 0xFF 0xFF; an RTS change
//           is conveyed as 0xFF <1|0>. Inbound: 0xFF 0x01 = DTR high, 0xFF 0x00
//           = DTR low, 0xFF 0xFF = a literal 0xFF data byte.
//
// Authoritative source: BeebEm Src/IP232.cpp (EthernetReceivedData / IP232SetRTS)
// and Src/Serial.cpp (the outbound 0xFF doubling). The BBC RS423 connector has
// no DTR/DCD pin, so inbound DTR events are informational only.
//
// The decoder is stateful: a 0xFF flag may straddle two read chunks, so the
// flag state is carried across decode() calls.
enum class DtrEvent : std::uint8_t { High, Low };

class Ip232Codec {
public:
    explicit Ip232Codec(bool raw) : raw_(raw) {}

    bool raw() const { return raw_; }

    // Outbound: append the wire bytes for one transmitted data byte.
    void encode_data(std::uint8_t value, std::vector<std::uint8_t>& out) const {
        out.push_back(value);
        if (!raw_ && value == 0xFF) {
            out.push_back(0xFF);  // double a literal flag byte
        }
    }

    // Outbound: the RTS-change escape (asserted -> 0xFF 0x01, deasserted ->
    // 0xFF 0x00). Only meaningful in ip232 mode with handshaking; the caller
    // decides when to emit it.
    static void encode_rts(bool asserted, std::vector<std::uint8_t>& out) {
        out.push_back(0xFF);
        out.push_back(asserted ? 0x01 : 0x00);
    }

    // Inbound: decode received wire bytes into data bytes (appended to `data`)
    // and DTR events (appended to `events`). Carries the flag state across calls.
    void decode(std::span<const std::uint8_t> in, std::vector<std::uint8_t>& data,
                std::vector<DtrEvent>& events) {
        for (std::uint8_t b : in) {
            if (flag_received_) {
                flag_received_ = false;
                if (b == 0x01) {
                    events.push_back(DtrEvent::High);
                } else if (b == 0x00) {
                    events.push_back(DtrEvent::Low);
                } else if (b == 0xFF) {
                    data.push_back(0xFF);  // 0xFF 0xFF -> literal 0xFF
                }
                // Any other value after a flag is undefined; BeebEm drops it.
            } else if (b == 0xFF && !raw_) {
                flag_received_ = true;  // await the escape's second byte
            } else {
                data.push_back(b);
            }
        }
    }

private:
    bool raw_;
    bool flag_received_ = false;
};

}  // namespace beebium::ip232

#endif  // BEEBIUM_EXT_IP232_CODEC_HPP
