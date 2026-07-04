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

#ifndef BEEBIUM_DISCOVERY_DNSSD_API_HPP
#define BEEBIUM_DISCOVERY_DNSSD_API_HPP

#if defined(BEEBIUM_HAS_BONJOUR) || defined(BEEBIUM_HAS_BONJOUR_DYNAMIC)

#include <dns_sd.h>

namespace beebium::discovery {

/// The DNS-SD (Bonjour) entry points Beebium uses, as a table of function
/// pointers pinned to the real prototypes via decltype (a signature mismatch
/// is a compile error). Both platforms call through this table so the Bonjour
/// advertiser and browser share one implementation:
///
/// - macOS links libSystem, so the table holds the real function addresses and
///   is always available.
/// - Windows resolves them at run time from Apple's Bonjour runtime
///   (dnssd.dll) via GetProcAddress -- no Bonjour SDK needed to build, and the
///   table is null when Bonjour is not installed. This mirrors the Linux Avahi
///   dlopen approach (see AvahiAdvertiser.cpp).
struct DnssdApi {
    decltype(&DNSServiceRegister)      DNSServiceRegister;
    decltype(&DNSServiceBrowse)        DNSServiceBrowse;
    decltype(&DNSServiceResolve)       DNSServiceResolve;
    decltype(&DNSServiceGetAddrInfo)   DNSServiceGetAddrInfo;
    decltype(&DNSServiceRefDeallocate) DNSServiceRefDeallocate;
    decltype(&DNSServiceRefSockFD)     DNSServiceRefSockFD;
    decltype(&DNSServiceProcessResult) DNSServiceProcessResult;
    decltype(&TXTRecordCreate)         TXTRecordCreate;
    decltype(&TXTRecordSetValue)       TXTRecordSetValue;
    decltype(&TXTRecordGetLength)      TXTRecordGetLength;
    decltype(&TXTRecordGetBytesPtr)    TXTRecordGetBytesPtr;
    decltype(&TXTRecordDeallocate)     TXTRecordDeallocate;
    decltype(&TXTRecordGetCount)       TXTRecordGetCount;
    decltype(&TXTRecordGetItemAtIndex) TXTRecordGetItemAtIndex;
};

/// The resolved DNS-SD table, or nullptr when the runtime is unavailable
/// (Windows without Bonjour installed). Resolved once and cached. Non-null on
/// macOS.
const DnssdApi* dnssd_api();

/// True if the DNS-SD runtime is usable on this system.
inline bool dnssd_available() { return dnssd_api() != nullptr; }

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_BONJOUR || BEEBIUM_HAS_BONJOUR_DYNAMIC

#endif  // BEEBIUM_DISCOVERY_DNSSD_API_HPP
