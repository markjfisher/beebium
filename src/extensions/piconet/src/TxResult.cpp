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

#include "beebium/econet/piconet/TxResult.hpp"

namespace beebium::piconet {

TxResult parse_tx_result(std::string_view code) {
    if (code == "OK")                  return TxResult::Ok;
    if (code == "UNINITIALISED")       return TxResult::Uninitialised;
    if (code == "OVERFLOW")            return TxResult::Overflow;
    if (code == "UNDERRUN")            return TxResult::Underrun;
    if (code == "LINE_JAMMED")         return TxResult::LineJammed;
    if (code == "NO_SCOUT_ACK")        return TxResult::NoScoutAck;
    if (code == "NO_DATA_ACK")         return TxResult::NoDataAck;
    if (code == "TIMEOUT")             return TxResult::Timeout;
    if (code == "INVALID_RECEIVE_ID")  return TxResult::InvalidReceiveId;
    if (code == "MISC")                return TxResult::Misc;
    return TxResult::Unknown;
}

std::string_view to_string(TxResult result) {
    switch (result) {
        case TxResult::Ok:                return "OK";
        case TxResult::Uninitialised:     return "UNINITIALISED";
        case TxResult::Overflow:          return "OVERFLOW";
        case TxResult::Underrun:          return "UNDERRUN";
        case TxResult::LineJammed:        return "LINE_JAMMED";
        case TxResult::NoScoutAck:        return "NO_SCOUT_ACK";
        case TxResult::NoDataAck:         return "NO_DATA_ACK";
        case TxResult::Timeout:           return "TIMEOUT";
        case TxResult::InvalidReceiveId:  return "INVALID_RECEIVE_ID";
        case TxResult::Misc:              return "MISC";
        case TxResult::Unknown:           return "UNKNOWN";
    }
    return "UNKNOWN";
}

}  // namespace beebium::piconet
