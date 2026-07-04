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

#if defined(BEEBIUM_HAS_BONJOUR) || defined(BEEBIUM_HAS_BONJOUR_DYNAMIC)

#include "DnssdApi.hpp"

#ifdef BEEBIUM_HAS_BONJOUR_DYNAMIC
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace beebium::discovery {

namespace {

const DnssdApi* load_dnssd_api() {
#ifdef BEEBIUM_HAS_BONJOUR_DYNAMIC
    // Apple's Bonjour runtime installs dnssd.dll into System32. Load it by
    // bare name (the default search path finds the system copy); a missing DLL
    // means Bonjour is not installed and DNS-SD is simply unavailable. The
    // handle is intentionally never freed -- the process keeps it mapped for
    // its lifetime, matching the Avahi dlopen policy.
    HMODULE handle = LoadLibraryW(L"dnssd.dll");
    if (!handle) {
        return nullptr;
    }

    auto* api = new DnssdApi{};
    bool ok = true;

#define BEEBIUM_LOAD_DNSSD_SYM(field)                                        \
    api->field = reinterpret_cast<decltype(api->field)>(                     \
        GetProcAddress(handle, #field));                                     \
    ok = ok && (api->field != nullptr)

    BEEBIUM_LOAD_DNSSD_SYM(DNSServiceRegister);
    BEEBIUM_LOAD_DNSSD_SYM(DNSServiceBrowse);
    BEEBIUM_LOAD_DNSSD_SYM(DNSServiceResolve);
    BEEBIUM_LOAD_DNSSD_SYM(DNSServiceGetAddrInfo);
    BEEBIUM_LOAD_DNSSD_SYM(DNSServiceRefDeallocate);
    BEEBIUM_LOAD_DNSSD_SYM(DNSServiceRefSockFD);
    BEEBIUM_LOAD_DNSSD_SYM(DNSServiceProcessResult);
    BEEBIUM_LOAD_DNSSD_SYM(TXTRecordCreate);
    BEEBIUM_LOAD_DNSSD_SYM(TXTRecordSetValue);
    BEEBIUM_LOAD_DNSSD_SYM(TXTRecordGetLength);
    BEEBIUM_LOAD_DNSSD_SYM(TXTRecordGetBytesPtr);
    BEEBIUM_LOAD_DNSSD_SYM(TXTRecordDeallocate);
    BEEBIUM_LOAD_DNSSD_SYM(TXTRecordGetCount);
    BEEBIUM_LOAD_DNSSD_SYM(TXTRecordGetItemAtIndex);

#undef BEEBIUM_LOAD_DNSSD_SYM

    if (!ok) {
        delete api;
        return nullptr;
    }
    return api;
#else
    // macOS: the symbols are linked from libSystem, so the table just holds
    // their addresses and is always available.
    static const DnssdApi api{
        &DNSServiceRegister,      &DNSServiceBrowse,
        &DNSServiceResolve,       &DNSServiceGetAddrInfo,
        &DNSServiceRefDeallocate, &DNSServiceRefSockFD,
        &DNSServiceProcessResult, &TXTRecordCreate,
        &TXTRecordSetValue,       &TXTRecordGetLength,
        &TXTRecordGetBytesPtr,    &TXTRecordDeallocate,
        &TXTRecordGetCount,       &TXTRecordGetItemAtIndex,
    };
    return &api;
#endif
}

}  // namespace

const DnssdApi* dnssd_api() {
    static const DnssdApi* const cached = load_dnssd_api();
    return cached;
}

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_BONJOUR || BEEBIUM_HAS_BONJOUR_DYNAMIC
