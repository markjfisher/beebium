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

#include "AcornRtcExtension.hpp"
#include "TimeParser.hpp"
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/UserPort.hpp>

#include <iostream>
#include <stdexcept>

namespace beebium {

std::span<const std::string_view> AcornRtcExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"user-port"};
    return deps;
}

std::span<const std::string_view> AcornRtcExtension::provides() const {
    return {};
}

void AcornRtcExtension::init(ExtensionContext& ctx) {
    // Determine the target datetime for initialisation
    Saf3019p::DateTime target_dt;

    auto time_val = config_value("time");
    auto offset_val = config_value("offset");

    if (time_val && offset_val) {
        throw std::runtime_error(
            "Acorn RTC: 'time' and 'offset' are mutually exclusive");
    }

    if (time_val) {
        target_dt = parse_absolute_time(*time_val);
    } else if (offset_val) {
        target_dt = apply_relative_offset(*offset_val);
    } else {
        target_dt = current_local_datetime();
    }

    // Parse register layout
    auto layout_val = config_value("layout");
    if (layout_val) {
        if (*layout_val == "7bit-year") {
            chip_.set_layout(RegisterLayout::SevenBitYear);
        } else if (*layout_val == "4bit-year") {
            chip_.set_layout(RegisterLayout::FourBitYear);
        } else {
            throw std::runtime_error(
                "Acorn RTC: unknown layout '" + std::string(*layout_val) +
                "' (valid: '4bit-year', '7bit-year')");
        }
    }

    // Validate year is representable for the chosen layout
    bool valid = true;
    std::string range;
    if (chip_.layout() == RegisterLayout::SevenBitYear) {
        // V126: 2-digit year (0-99), century inferred. Valid: 1981-2099.
        valid = (target_dt.year >= 1981 && target_dt.year <= 2099);
        range = "1981-2099";
    } else {
        // Acorn: offset 0-19 from 1981. Valid: 1981-2000.
        int offset = target_dt.year - 1981;
        valid = (offset >= 0 && offset <= 19);
        range = "1981-2000";
    }
    if (!valid) {
        throw std::runtime_error(
            "Acorn RTC: year " + std::to_string(target_dt.year) +
            " cannot be represented (valid range: " + range + " for layout '" +
            std::string(layout_val.value_or("4bit-year")) + "').\n"
            "  Use --acorn-rtc time=YYYY-MM-DDThhmm with a date in range,\n"
            "  or use --acorn-rtc offset=-Ny to shift the clock back.");
    }

    // Initialise the chip
    if (!chip_.initialise(target_dt)) {
        throw std::runtime_error("Acorn RTC: failed to initialise SAF3019P");
    }

    std::cout << "Acorn RTC: initialised to "
              << target_dt.year << "-"
              << (target_dt.month < 10 ? "0" : "") << target_dt.month << "-"
              << (target_dt.day < 10 ? "0" : "") << target_dt.day << "T"
              << (target_dt.hour < 10 ? "0" : "") << target_dt.hour << ":"
              << (target_dt.minute < 10 ? "0" : "") << target_dt.minute << "\n";

    // Create gRPC service
    service_ = std::make_unique<AcornRtcServiceImpl>(chip_);

    // Attach to User Port
    ctx.get<UserPort>().attach(*this);
}

void AcornRtcExtension::shutdown() {
    service_.reset();
}

std::vector<grpc::Service*> AcornRtcExtension::grpc_services() {
    if (service_) {
        return {service_.get()};
    }
    return {};
}

uint8_t AcornRtcExtension::update_port_b(uint8_t output, uint8_t ddr) {
    // Detect DDRB reset pattern: bits 0-2 all set to output
    bool ddr_outputs = (ddr & 0x07) == 0x07;
    if (ddr_outputs && !ddr_reset_armed_) {
        chip_.reset_protocol();
        ddr_reset_armed_ = true;
    } else if (!ddr_outputs) {
        ddr_reset_armed_ = false;
    }

    // Advance counters to current wall-clock time
    chip_.advance_counters();

    // Forward Port B output to chip (CBUS protocol)
    chip_.write(output);

    // Return input pin state:
    // PB0 = DATA from chip (read back during TIME READ)
    // PB5 = COMP (comparator output, normally low = no alarm match)
    // PB7 = POWF (power failure, high = no failure)
    // All other input pins pulled high
    uint8_t input = 0xFF;
    input = (input & 0xFE) | chip_.read_data_bit();  // PB0 = DATA
    input &= ~(1 << 5);                               // PB5 = COMP low (no alarm)
    // PB7 stays high (POWF = no power failure)
    return input;
}

}  // namespace beebium
