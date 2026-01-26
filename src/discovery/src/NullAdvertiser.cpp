// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include <beebium/discovery/Advertiser.hpp>

namespace beebium::discovery {

/// Null implementation of Advertiser for platforms without mDNS support.
///
/// Always reports available=false and advertising=false. All operations
/// are no-ops. Used as a fallback when platform-specific mDNS APIs are
/// not available.
class NullAdvertiser final : public Advertiser {
public:
    bool start(const ServiceInfo& /*info*/) override {
        // mDNS not available on this platform
        return false;
    }

    void stop() override {
        // Nothing to do
    }

    AdvertiserState state() const override {
        return AdvertiserState{
            .available = false,
            .advertising = false,
            .actual_name = {},
        };
    }
};

// Factory function implementation for platforms without native mDNS.
// This is overridden by platform-specific implementations when available.
#if !defined(BEEBIUM_HAS_BONJOUR) && !defined(BEEBIUM_HAS_AVAHI) && \
    !defined(BEEBIUM_HAS_WINDOWS_MDNS)

std::unique_ptr<Advertiser> create_advertiser() {
    return std::make_unique<NullAdvertiser>();
}

#endif

}  // namespace beebium::discovery
