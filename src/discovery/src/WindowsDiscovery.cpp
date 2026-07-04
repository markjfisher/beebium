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

#if defined(BEEBIUM_HAS_WINDOWS_MDNS) || defined(BEEBIUM_HAS_BONJOUR_DYNAMIC)

#include "DiscoveryProviders.hpp"
#include "DnssdApi.hpp"

// Windows service-discovery factory. Windows has two possible mDNS providers,
// and which one works depends on the machine:
//
// - Apple's Bonjour (dnssd.dll): present when the user has iTunes, Adobe apps,
//   etc. installed. When present it owns the mDNS responder (UDP 5353), so it
//   is the provider most likely to actually function -- prefer it.
// - The native Windows DnsService API (windns.h): works on a clean machine
//   without Bonjour, but Bonjour's presence can prevent it from registering.
//
// So prefer Bonjour when its runtime is installed, and fall back to the native
// implementation otherwise. The native API is always present on Windows 10
// 1903+, so there is always a provider.

namespace beebium::discovery {

std::unique_ptr<Advertiser> create_advertiser() {
#if defined(BEEBIUM_HAS_BONJOUR_DYNAMIC)
    if (dnssd_available()) {
        return make_bonjour_advertiser();
    }
#endif
    return make_windows_native_advertiser();
}

std::unique_ptr<Browser> create_browser() {
#if defined(BEEBIUM_HAS_BONJOUR_BROWSE_DYNAMIC)
    if (dnssd_available()) {
        return make_bonjour_browser();
    }
#endif
    return make_windows_native_browser();
}

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_WINDOWS_MDNS || BEEBIUM_HAS_BONJOUR_DYNAMIC
