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

#include "Rfc2217Codec.hpp"

namespace beebium::rfc2217 {

using namespace telnet;

namespace {

// Append a byte, escaping a literal IAC (0xFF) as IAC IAC.
void append_escaped(std::uint8_t b, std::vector<std::uint8_t>& out) {
    out.push_back(b);
    if (b == IAC) out.push_back(IAC);
}

}  // namespace

std::vector<std::uint8_t> Rfc2217Codec::initial_negotiation() {
    std::vector<std::uint8_t> out;
    if (role_ != Role::Client) {
        return out;  // the server answers reactively in decode()
    }
    // Offer COM-PORT control and an 8-bit-clean (BINARY) session both ways.
    const std::uint8_t will[] = {OPT_COM_PORT, OPT_BINARY, OPT_SGA};
    for (std::uint8_t opt : will) {
        out.insert(out.end(), {IAC, WILL, opt});
        sent_will_.insert(opt);
    }
    const std::uint8_t doo[] = {OPT_BINARY, OPT_SGA};
    for (std::uint8_t opt : doo) {
        out.insert(out.end(), {IAC, DO, opt});
        sent_do_.insert(opt);
    }
    return out;
}

void Rfc2217Codec::encode_data(std::span<const std::uint8_t> data,
                               std::vector<std::uint8_t>& out) const {
    for (std::uint8_t b : data) append_escaped(b, out);
}

void Rfc2217Codec::encode_subneg(std::uint8_t command,
                                 std::span<const std::uint8_t> value,
                                 std::vector<std::uint8_t>& out) const {
    out.insert(out.end(), {IAC, SB, OPT_COM_PORT, command});
    for (std::uint8_t b : value) append_escaped(b, out);
    out.insert(out.end(), {IAC, SE});
}

void Rfc2217Codec::encode_set_baudrate(std::uint32_t baud,
                                       std::vector<std::uint8_t>& out) const {
    const std::uint8_t value[] = {
        static_cast<std::uint8_t>((baud >> 24) & 0xFF),
        static_cast<std::uint8_t>((baud >> 16) & 0xFF),
        static_cast<std::uint8_t>((baud >> 8) & 0xFF),
        static_cast<std::uint8_t>(baud & 0xFF),
    };
    encode_subneg(command_for_role(comport::SET_BAUDRATE), value, out);
}

void Rfc2217Codec::encode_set_control(std::uint8_t value,
                                      std::vector<std::uint8_t>& out) const {
    const std::uint8_t payload[] = {value};
    encode_subneg(command_for_role(comport::SET_CONTROL), payload, out);
}

void Rfc2217Codec::encode_notify_modemstate(std::uint8_t state,
                                            std::vector<std::uint8_t>& out) const {
    const std::uint8_t payload[] = {state};
    encode_subneg(command_for_role(comport::NOTIFY_MODEMSTATE), payload, out);
}

bool Rfc2217Codec::supported_option(std::uint8_t option) const {
    return option == OPT_COM_PORT || option == OPT_BINARY || option == OPT_SGA;
}

void Rfc2217Codec::handle_negotiation(std::uint8_t verb, std::uint8_t option,
                                      std::vector<std::uint8_t>& out) {
    if (supported_option(option)) {
        if (verb == DO) {
            if (option == OPT_COM_PORT) com_port_agreed_ = true;
            if (sent_will_.insert(option).second) {
                out.insert(out.end(), {IAC, WILL, option});  // agree, once
            }
        } else if (verb == WILL) {
            if (option == OPT_COM_PORT) com_port_agreed_ = true;
            if (sent_do_.insert(option).second) {
                out.insert(out.end(), {IAC, DO, option});
            }
        } else if (verb == DONT) {
            sent_will_.erase(option);
        } else if (verb == WONT) {
            sent_do_.erase(option);
        }
    } else {
        // Refuse anything else, once.
        if (verb == DO) out.insert(out.end(), {IAC, WONT, option});
        else if (verb == WILL) out.insert(out.end(), {IAC, DONT, option});
    }
}

void Rfc2217Codec::handle_subneg(std::vector<ComPortCommand>& commands) {
    // subneg_ == [option, command, value...]; payload was un-escaped on the way in.
    if (subneg_.size() >= 2 && subneg_[0] == OPT_COM_PORT) {
        ComPortCommand cmd;
        cmd.command = subneg_[1];
        cmd.value.assign(subneg_.begin() + 2, subneg_.end());
        commands.push_back(std::move(cmd));
    }
    subneg_.clear();
}

void Rfc2217Codec::decode(std::span<const std::uint8_t> in,
                          std::vector<std::uint8_t>& data,
                          std::vector<ComPortCommand>& commands,
                          std::vector<std::uint8_t>& out) {
    for (std::uint8_t b : in) {
        switch (state_) {
            case State::Normal:
                if (b == IAC) state_ = State::Iac;
                else data.push_back(b);
                break;
            case State::Iac:
                if (b == IAC) {
                    data.push_back(IAC);  // escaped literal 0xFF
                    state_ = State::Normal;
                } else if (b == SB) {
                    subneg_.clear();
                    state_ = State::Subneg;
                } else if (b == WILL || b == WONT || b == DO || b == DONT) {
                    neg_verb_ = b;
                    state_ = State::Negotiate;
                } else {
                    state_ = State::Normal;  // other Telnet command: ignore
                }
                break;
            case State::Negotiate:
                handle_negotiation(neg_verb_, b, out);
                state_ = State::Normal;
                break;
            case State::Subneg:
                if (b == IAC) state_ = State::SubnegIac;
                else subneg_.push_back(b);
                break;
            case State::SubnegIac:
                if (b == IAC) {
                    subneg_.push_back(IAC);  // escaped literal 0xFF in payload
                    state_ = State::Subneg;
                } else if (b == SE) {
                    handle_subneg(commands);
                    state_ = State::Normal;
                } else {
                    subneg_.clear();  // malformed; abandon
                    state_ = State::Normal;
                }
                break;
        }
    }
}

}  // namespace beebium::rfc2217
