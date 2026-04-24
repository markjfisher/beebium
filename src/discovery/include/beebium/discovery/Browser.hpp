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

#ifndef BEEBIUM_DISCOVERY_BROWSER_HPP
#define BEEBIUM_DISCOVERY_BROWSER_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace beebium::discovery {

/// A peer that has been discovered through DNS-SD browsing.
///
/// Carries everything an AUN consumer needs to add the peer to its
/// peer table: the resolved socket endpoint plus the TXT record
/// payload. Net / station / port live in the TXT records rather than
/// being parsed up-front because the schema is owned by the
/// service-type (e.g. _aun._udp), not by the discovery layer.
struct DiscoveredService {
    /// The DNS-SD instance name as published (and after collision
    /// resolution -- e.g. "Beebium 0.254 (2)" if there was a clash).
    /// Use this as the stable key when correlating add / remove
    /// events; the (host, port) pair is not unique once collisions
    /// kick in.
    std::string instance_name;

    /// Resolved hostname (e.g. "alice.local."). Always present once
    /// the resolver has run; pre-resolution, the browser does not
    /// fire the on_added callback.
    std::string hostname;

    /// Resolved IPv4 address in network byte order. Zero if only
    /// IPv6 was advertised (rare for _aun._udp; the AUN protocol
    /// itself is IPv4-only today).
    uint32_t ipv4_addr_net_byte_order = 0;

    /// SRV port (host byte order).
    uint16_t port = 0;

    /// TXT record key/value pairs as published by the peer. Schema
    /// (e.g. version=, net=, station=) is interpreted by the
    /// service-specific consumer, not by the browser.
    std::map<std::string, std::string> txt_records;
};

/// Callbacks delivered when the browser sees peers come and go.
///
/// Both callbacks may run on a background thread. Implementations
/// must not assume single-threaded access; if downstream state is
/// not internally synchronised, marshal the work or take a lock.
struct BrowserCallbacks {
    /// Fired once per peer when first discovered (after resolution
    /// and address lookup have completed) and again whenever the
    /// peer's TXT records or addresses change. The instance_name
    /// uniquely identifies the peer for correlation with on_removed.
    std::function<void(const DiscoveredService&)> on_added;

    /// Fired when a previously-announced peer goes away. Only the
    /// instance_name is supplied; the consumer is expected to map it
    /// back to whatever state was created in on_added.
    std::function<void(const std::string& instance_name)> on_removed;
};

/// Current state of the browser.
struct BrowserState {
    /// True if the platform has an mDNS resolver we can drive.
    bool available = false;

    /// True between a successful start() and a stop() call.
    bool browsing = false;
};

/// Interface for DNS-SD service browsing.
///
/// Mirrors the shape of Advertiser. Implementations live alongside
/// the corresponding Advertiser implementations so they share
/// per-platform plumbing (Bonjour event loop, Win32 dnsapi, etc.).
class Browser {
public:
    virtual ~Browser() = default;

    /// Start browsing for the given service type (e.g. "_aun._udp").
    ///
    /// Callbacks are stored by value -- the browser keeps them alive
    /// for the duration of the subscription. Returns true if the
    /// platform accepted the browse request, false if mDNS is
    /// unavailable or the request failed.
    ///
    /// If already browsing, stops the current subscription before
    /// starting a new one.
    virtual bool start(const std::string& service_type,
                       BrowserCallbacks callbacks) = 0;

    /// Stop browsing. Safe to call if not currently browsing. After
    /// stop() returns, no further callbacks will fire (modulo
    /// callbacks already in flight on the platform thread).
    virtual void stop() = 0;

    /// Get the current browser state.
    virtual BrowserState state() const = 0;

    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;

protected:
    Browser() = default;
};

/// Create a platform-appropriate browser.
///
/// Returns BonjourBrowser on macOS, WindowsBrowser on Windows (when
/// mDNS is available), and NullBrowser everywhere else. The returned
/// browser's state().available indicates whether mDNS is functional
/// on this system.
std::unique_ptr<Browser> create_browser();

}  // namespace beebium::discovery

#endif  // BEEBIUM_DISCOVERY_BROWSER_HPP
