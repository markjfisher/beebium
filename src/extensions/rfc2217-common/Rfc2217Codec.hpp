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

#ifndef BEEBIUM_EXT_RFC2217_CODEC_HPP
#define BEEBIUM_EXT_RFC2217_CODEC_HPP

#include <cstdint>
#include <set>
#include <span>
#include <vector>

namespace beebium::rfc2217 {

// Telnet / RFC 2217 wire constants (authority: RFC 854 + RFC 2217, cross-checked
// against pySerial's serial/rfc2217.py).
namespace telnet {
inline constexpr std::uint8_t SE = 240;
inline constexpr std::uint8_t SB = 250;
inline constexpr std::uint8_t WILL = 251;
inline constexpr std::uint8_t WONT = 252;
inline constexpr std::uint8_t DO = 253;
inline constexpr std::uint8_t DONT = 254;
inline constexpr std::uint8_t IAC = 255;

inline constexpr std::uint8_t OPT_BINARY = 0;
inline constexpr std::uint8_t OPT_SGA = 3;
inline constexpr std::uint8_t OPT_COM_PORT = 44;
}  // namespace telnet

namespace comport {
// Client->server command codes; the server's responses are the same +100.
inline constexpr std::uint8_t SERVER_OFFSET = 100;
inline constexpr std::uint8_t SET_BAUDRATE = 1;
inline constexpr std::uint8_t SET_DATASIZE = 2;
inline constexpr std::uint8_t SET_PARITY = 3;
inline constexpr std::uint8_t SET_STOPSIZE = 4;
inline constexpr std::uint8_t SET_CONTROL = 5;
inline constexpr std::uint8_t NOTIFY_LINESTATE = 6;
inline constexpr std::uint8_t NOTIFY_MODEMSTATE = 7;
inline constexpr std::uint8_t FLOWCONTROL_SUSPEND = 8;
inline constexpr std::uint8_t FLOWCONTROL_RESUME = 9;
inline constexpr std::uint8_t SET_LINESTATE_MASK = 10;
inline constexpr std::uint8_t SET_MODEMSTATE_MASK = 11;
inline constexpr std::uint8_t PURGE_DATA = 12;

// SET-CONTROL values.
inline constexpr std::uint8_t CONTROL_FLOW_NONE = 1;      // outbound/both
inline constexpr std::uint8_t CONTROL_FLOW_XONXOFF = 2;
inline constexpr std::uint8_t CONTROL_FLOW_HARDWARE = 3;
inline constexpr std::uint8_t CONTROL_DTR_ON = 8;
inline constexpr std::uint8_t CONTROL_DTR_OFF = 9;
inline constexpr std::uint8_t CONTROL_RTS_ON = 11;
inline constexpr std::uint8_t CONTROL_RTS_OFF = 12;

// SET-PARITY payload values.
inline constexpr std::uint8_t PARITY_NONE = 1;
inline constexpr std::uint8_t PARITY_ODD = 2;
inline constexpr std::uint8_t PARITY_EVEN = 3;
inline constexpr std::uint8_t PARITY_MARK = 4;
inline constexpr std::uint8_t PARITY_SPACE = 5;

// SET-STOPSIZE payload values.
inline constexpr std::uint8_t STOPSIZE_ONE = 1;
inline constexpr std::uint8_t STOPSIZE_TWO = 2;
inline constexpr std::uint8_t STOPSIZE_ONE_HALF = 3;

// SET-DATASIZE payload is the number of data bits (5..8) directly.

// NOTIFY-MODEMSTATE bit for CTS.
inline constexpr std::uint8_t MODEMSTATE_CTS = 0x10;
}  // namespace comport

// One decoded COM-PORT-OPTION subnegotiation. `command` is the raw code (the
// client form 1..12 from a server's view, or the +100 server form from a
// client's view); `value` is the payload after the command byte (un-escaped).
struct ComPortCommand {
    std::uint8_t command = 0;
    std::vector<std::uint8_t> value;
};

// A stateful Telnet + RFC 2217 codec, shared by the client and server endpoints.
// It frames application data with IAC IAC escaping, negotiates the Telnet
// options needed for an 8-bit-clean COM-PORT session, and encodes/decodes the
// minimal COM-PORT command set. The decoder's IAC state machine is carried
// across read chunks.
class Rfc2217Codec {
public:
    enum class Role { Client, Server };

    explicit Rfc2217Codec(Role role) : role_(role) {}

    // Reset the per-session parser/negotiation state for a fresh connection.
    // The role is fixed for the object's lifetime, so this is safe to call while
    // the (const, role-only) encoders run on another thread.
    void reset() {
        state_ = State::Normal;
        subneg_.clear();
        com_port_agreed_ = false;
        sent_will_.clear();
        sent_do_.clear();
    }

    // Negotiation to send on connect. The client offers COM-PORT control and
    // BINARY/SGA both ways; the server stays passive and answers in decode().
    std::vector<std::uint8_t> initial_negotiation();

    bool option_negotiated() const { return com_port_agreed_; }

    // --- Outbound encoding ---
    void encode_data(std::span<const std::uint8_t> data,
                     std::vector<std::uint8_t>& out) const;
    void encode_subneg(std::uint8_t command, std::span<const std::uint8_t> value,
                       std::vector<std::uint8_t>& out) const;
    // SET-BAUDRATE (4-byte big-endian); command code is role-adjusted.
    void encode_set_baudrate(std::uint32_t baud, std::vector<std::uint8_t>& out) const;
    // SET-DATASIZE / SET-PARITY / SET-STOPSIZE (single-byte payloads).
    void encode_set_datasize(std::uint8_t data_bits, std::vector<std::uint8_t>& out) const;
    void encode_set_parity(std::uint8_t parity, std::vector<std::uint8_t>& out) const;
    void encode_set_stopsize(std::uint8_t stop, std::vector<std::uint8_t>& out) const;
    // SET-CONTROL with a CONTROL_* value (client emits the request form).
    void encode_set_control(std::uint8_t value, std::vector<std::uint8_t>& out) const;
    // NOTIFY-MODEMSTATE (server only).
    void encode_notify_modemstate(std::uint8_t state, std::vector<std::uint8_t>& out) const;

    // --- Inbound decoding ---
    // Appends application data to `data`, COM-PORT subnegotiations to `commands`,
    // and any Telnet negotiation replies the codec generates to `out`.
    void decode(std::span<const std::uint8_t> in, std::vector<std::uint8_t>& data,
                std::vector<ComPortCommand>& commands, std::vector<std::uint8_t>& out);

private:
    enum class State { Normal, Iac, Negotiate, Subneg, SubnegIac };

    void handle_negotiation(std::uint8_t verb, std::uint8_t option,
                            std::vector<std::uint8_t>& out);
    void handle_subneg(std::vector<ComPortCommand>& commands);
    bool supported_option(std::uint8_t option) const;
    std::uint8_t command_for_role(std::uint8_t base_command) const {
        return role_ == Role::Server
                   ? static_cast<std::uint8_t>(base_command + comport::SERVER_OFFSET)
                   : base_command;
    }

    Role role_;
    State state_ = State::Normal;
    std::uint8_t neg_verb_ = 0;
    std::vector<std::uint8_t> subneg_;
    bool com_port_agreed_ = false;
    std::set<std::uint8_t> sent_will_;  // options we've offered (avoid neg loops)
    std::set<std::uint8_t> sent_do_;
};

}  // namespace beebium::rfc2217

#endif  // BEEBIUM_EXT_RFC2217_CODEC_HPP
