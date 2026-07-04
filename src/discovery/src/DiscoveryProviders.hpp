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

#ifndef BEEBIUM_DISCOVERY_PROVIDERS_HPP
#define BEEBIUM_DISCOVERY_PROVIDERS_HPP

#include <memory>

#include <beebium/discovery/Advertiser.hpp>
#include <beebium/discovery/Browser.hpp>

// Provider constructors used by the per-platform factory. Each is defined only
// where its implementation is compiled (guarded by the same macros). The
// Windows factory (WindowsDiscovery.cpp) picks among them at run time; macOS
// and Linux have a single provider whose file also defines create_advertiser()
// / create_browser() directly.

namespace beebium::discovery {

#if defined(BEEBIUM_HAS_BONJOUR) || defined(BEEBIUM_HAS_BONJOUR_DYNAMIC)
std::unique_ptr<Advertiser> make_bonjour_advertiser();
#endif
#if defined(BEEBIUM_HAS_BONJOUR_BROWSE) || defined(BEEBIUM_HAS_BONJOUR_BROWSE_DYNAMIC)
std::unique_ptr<Browser> make_bonjour_browser();
#endif

#ifdef BEEBIUM_HAS_WINDOWS_MDNS
std::unique_ptr<Advertiser> make_windows_native_advertiser();
#endif
#ifdef BEEBIUM_HAS_WINDOWS_MDNS_BROWSE
std::unique_ptr<Browser> make_windows_native_browser();
#endif

}  // namespace beebium::discovery

#endif  // BEEBIUM_DISCOVERY_PROVIDERS_HPP
