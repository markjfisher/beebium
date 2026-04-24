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

#include <beebium/discovery/Browser.hpp>

namespace beebium::discovery {

/// Null implementation of Browser for platforms without mDNS support.
///
/// Always reports available=false; start() returns false so callers
/// learn that discovery is unavailable in one round-trip.
class NullBrowser final : public Browser {
public:
    bool start(const std::string& /*service_type*/,
               BrowserCallbacks /*callbacks*/) override {
        return false;
    }

    void stop() override {}

    BrowserState state() const override {
        return BrowserState{
            .available = false,
            .browsing = false,
        };
    }
};

// The browse-side counterpart of the platform mDNS API isn't always
// implemented in lockstep with the advertise side. We use distinct
// "_BROWSE" macros so each platform can opt the browser in
// independently of the advertiser. Windows currently advertises but
// does not yet browse, so its create_browser() falls through to here.
#if !defined(BEEBIUM_HAS_BONJOUR_BROWSE) && \
    !defined(BEEBIUM_HAS_AVAHI_BROWSE) && \
    !defined(BEEBIUM_HAS_WINDOWS_MDNS_BROWSE)

std::unique_ptr<Browser> create_browser() {
    return std::make_unique<NullBrowser>();
}

#endif

}  // namespace beebium::discovery
