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

#ifndef BEEBIUM_ACORN_RTC_UI_HPP
#define BEEBIUM_ACORN_RTC_UI_HPP

#include "Saf3019p.hpp"

#include <beebium/extension/ExtensionUi.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace beebium {

class AcornRtcExtension;

// Server-driven UI panel for the Acorn RTC. Displays the chip's
// current datetime (minute granularity -- the SAF3019P doesn't track
// seconds) with the active register-layout variant on hover.
//
// First peripheral in the project whose ExtensionUi has live data: a
// background ticker bumps mark_dirty every 30 seconds, forcing the
// framework's poll loop to re-issue build_view and push a fresh View
// to any subscribed client. build_view itself calls advance_counters
// on the chip before reading, so each push reflects current wall-
// clock time rather than the time at last user-port access.
//
// Read-only today: no interactive controls. Future slices may add
// freeze/set-time buttons, layout-picker Choice, etc.
class AcornRtcUi : public ExtensionUi {
public:
    explicit AcornRtcUi(AcornRtcExtension& ext);
    ~AcornRtcUi() override;

    AcornRtcUi(const AcornRtcUi&) = delete;
    AcornRtcUi& operator=(const AcornRtcUi&) = delete;

    void build_view(View* out) const override;
    void handle_event(const DispatchRequest& request) override;

    // ISO-style "YYYY-MM-DD HH:MM" with zero-padded fields. The
    // SAF3019P doesn't expose seconds, so we don't either.
    static std::string format_datetime(int year, int month, int day,
                                       int hour, int minute);

    // Friendly description of a register-layout variant for hover
    // text: human-readable name, era / source software, valid year
    // range. The canonical CLI tokens ("4bit-year-in-r1" etc.) make
    // poor tooltips.
    static std::string format_layout(RegisterLayout layout);

private:
    void worker_loop();

    AcornRtcExtension& ext_;

    // Mutex/cv pair lets the dtor wake the worker out of its sleep
    // immediately instead of waiting up to a full tick interval.
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

}  // namespace beebium

#endif  // BEEBIUM_ACORN_RTC_UI_HPP
