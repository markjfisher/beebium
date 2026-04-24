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

#ifndef BEEBIUM_ECONET_AUN_DISCOVERY_SUBSCRIBER_HPP
#define BEEBIUM_ECONET_AUN_DISCOVERY_SUBSCRIBER_HPP

// Subscribes to "_aun._udp" DNS-SD announcements and feeds the
// resulting peers into AunBackend's peer table as Discovered entries.
//
// Filtering rules (see docs/discussion/aun-mdns-peer-discovery.md):
//   1. Skip our own announcement -- match by the (net, station) pair
//      from the TXT record against the backend's local (net, stn).
//   2. Operator-configured entries always win; the subscriber relies
//      on AunBackend::add_peer's PeerSource precedence (a Discovered
//      add against an existing operator entry is dropped silently).
//
// Lifetime is RAII-shaped, mirroring AunDiscoveryAnnouncer:
// construction captures the dependencies, start() begins browsing,
// stop() / destruction tears it down.

#include <beebium/discovery/Browser.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace beebium {

class AunBackend;

class AunDiscoverySubscriber {
public:
    // backend must outlive the subscriber. browser is the platform
    // browser to drive; tests inject a fake. If browser is null, the
    // constructor allocates a discovery::create_browser() default.
    AunDiscoverySubscriber(AunBackend& backend,
                           std::uint8_t local_stn,
                           std::unique_ptr<discovery::Browser> browser = nullptr);

    ~AunDiscoverySubscriber();

    AunDiscoverySubscriber(const AunDiscoverySubscriber&) = delete;
    AunDiscoverySubscriber& operator=(const AunDiscoverySubscriber&) = delete;

    // Begin browsing for "_aun._udp". Returns true if the browser
    // accepted the request, false if mDNS is unavailable on this
    // platform (the subscriber becomes a no-op for the rest of its
    // life).
    bool start();

    // Withdraw the subscription. Safe to call before start() or
    // after stop(); also called by the destructor.
    void stop();

    // True between a successful start() and a stop() call.
    bool is_subscribed() const;

    // Test-only: parse a TXT record set into the (net, stn) pair the
    // subscriber would derive. Returns nullopt if the schema is
    // missing or invalid (so the subscriber can't safely act on it).
    static bool parse_txt(const std::map<std::string, std::string>& txt,
                          std::uint8_t& net_out,
                          std::uint8_t& stn_out);

    // Test-only: invoke the on_added handler directly so unit tests
    // don't need a real browser. Pass a fully-populated
    // DiscoveredService.
    void inject_added(const discovery::DiscoveredService& svc);

    // Test-only counterpart to inject_added.
    void inject_removed(const std::string& instance_name);

private:
    AunBackend& backend_;
    std::uint8_t local_stn_;
    std::unique_ptr<discovery::Browser> browser_;

    // Maps DNS-SD instance name -> (net, stn) it was registered as,
    // so on_removed can find which peer to drop. The browser only
    // delivers the instance name on remove, not the TXT records.
    mutable std::mutex name_map_mutex_;
    std::map<std::string, std::pair<std::uint8_t, std::uint8_t>> name_to_peer_;

    void handle_added(const discovery::DiscoveredService& svc);
    void handle_removed(const std::string& instance_name);
};

}  // namespace beebium

#endif  // BEEBIUM_ECONET_AUN_DISCOVERY_SUBSCRIBER_HPP
