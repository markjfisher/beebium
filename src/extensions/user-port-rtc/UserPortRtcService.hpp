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

#ifndef BEEBIUM_USER_PORT_RTC_SERVICE_HPP
#define BEEBIUM_USER_PORT_RTC_SERVICE_HPP

#include "user_port_rtc.grpc.pb.h"
#include "Saf3019p.hpp"
#include "TimeParser.hpp"

#include <grpcpp/grpcpp.h>

#include <iomanip>
#include <sstream>

namespace beebium {

class UserPortRtcServiceImpl final : public UserPortRtcService::Service {
public:
    explicit UserPortRtcServiceImpl(Saf3019p& chip)
        : chip_(chip) {}

    grpc::Status GetTime(
            grpc::ServerContext*,
            const GetRtcTimeRequest*,
            GetRtcTimeResponse* response) override {
        chip_.advance_counters();
        auto dt = chip_.current_datetime();

        response->set_year(dt.year);
        response->set_month(dt.month);
        response->set_day(dt.day);
        response->set_hour(dt.hour);
        response->set_minute(dt.minute);

        std::ostringstream iso;
        iso << dt.year << "-"
            << std::setfill('0') << std::setw(2) << dt.month << "-"
            << std::setfill('0') << std::setw(2) << dt.day << "T"
            << std::setfill('0') << std::setw(2) << dt.hour << ":"
            << std::setfill('0') << std::setw(2) << dt.minute;
        response->set_iso8601(iso.str());

        return grpc::Status::OK;
    }

    grpc::Status SetTime(
            grpc::ServerContext*,
            const SetRtcTimeRequest* request,
            SetRtcTimeResponse*) override {
        Saf3019p::DateTime dt;
        try {
            dt = parse_absolute_time(request->iso8601());
        } catch (const std::invalid_argument& e) {
            return {grpc::StatusCode::INVALID_ARGUMENT, e.what()};
        }

        int year_offset = dt.year - 1981;
        if (year_offset < 0 || year_offset > 19) {
            return {grpc::StatusCode::INVALID_ARGUMENT,
                "Year " + std::to_string(dt.year) +
                " cannot be represented by the SAF3019P (valid range: 1981-2000)"};
        }

        if (!chip_.initialise(dt)) {
            return {grpc::StatusCode::INTERNAL, "Failed to initialise SAF3019P"};
        }

        return grpc::Status::OK;
    }

    grpc::Status GetRegisters(
            grpc::ServerContext*,
            const GetRtcRegistersRequest*,
            GetRtcRegistersResponse* response) override {
        chip_.advance_counters();
        for (int i = 0; i < 8; i++) {
            response->add_registers(chip_.read_register(i));
        }
        return grpc::Status::OK;
    }

    grpc::Status SetRegister(
            grpc::ServerContext*,
            const SetRtcRegisterRequest* request,
            SetRtcRegisterResponse*) override {
        auto reg = static_cast<int>(request->register_index());
        if (reg < 0 || reg > 7) {
            return {grpc::StatusCode::INVALID_ARGUMENT,
                "Register index must be 0-7, got " + std::to_string(reg)};
        }
        chip_.write_register(reg, static_cast<uint8_t>(request->bcd_value()));
        return grpc::Status::OK;
    }

private:
    Saf3019p& chip_;
};

}  // namespace beebium

#endif  // BEEBIUM_USER_PORT_RTC_SERVICE_HPP
