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

#ifndef BEEBIUM_DISCOVERY_ADVERTISER_HPP
#define BEEBIUM_DISCOVERY_ADVERTISER_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace beebium::discovery {

/// Information about the service to advertise.
struct ServiceInfo {
    /// Display name for the service (becomes DNS-SD instance name).
    std::string instance_name;

    /// DNS-SD service type. Defaults to "_beebium._tcp" for the gRPC
    /// machine service. AUN peer discovery uses the vendor-neutral
    /// "_aun._udp" instead so other AUN implementations (BeebEm,
    /// PiEconetBridge, real Acorn hardware with AUN clients) can share
    /// the announcement namespace.
    std::string service_type = "_beebium._tcp";

    /// Port the gRPC server is listening on.
    uint16_t port;

    /// TXT record key-value pairs (e.g., uuid, model, provenance, version).
    std::map<std::string, std::string> txt_records;
};

/// Current state of the advertiser.
struct AdvertiserState {
    /// True if the platform has mDNS support (Bonjour/Avahi/Windows mDNS).
    bool available = false;

    /// True if currently advertising.
    bool advertising = false;

    /// The actual registered name. May differ from requested instance_name
    /// if DNS-SD resolved a name collision (e.g., "Machine" -> "Machine (2)").
    std::string actual_name;
};

/// Interface for DNS-SD service advertisement.
///
/// Implementations handle platform-specific mDNS registration:
/// - macOS: dns_sd.h (Bonjour)
/// - Linux: Avahi
/// - Windows: windns.h
///
/// Use create_advertiser() to get the appropriate implementation for the
/// current platform, or NullAdvertiser if mDNS is unavailable.
class Advertiser {
public:
    virtual ~Advertiser() = default;

    /// Start advertising the service.
    ///
    /// @param info Service information to advertise.
    /// @return true if advertisement started successfully, false if mDNS is
    ///         unavailable or registration failed.
    ///
    /// If already advertising, stops the current advertisement and starts
    /// a new one with the updated info.
    virtual bool start(const ServiceInfo& info) = 0;

    /// Stop advertising. Safe to call if not currently advertising.
    virtual void stop() = 0;

    /// Get current advertiser state.
    virtual AdvertiserState state() const = 0;

    // Non-copyable
    Advertiser(const Advertiser&) = delete;
    Advertiser& operator=(const Advertiser&) = delete;

protected:
    Advertiser() = default;
};

/// Create a platform-appropriate advertiser.
///
/// Returns:
/// - BonjourAdvertiser on macOS
/// - AvahiAdvertiser on Linux (if Avahi is available)
/// - WindowsAdvertiser on Windows (if mDNS is available)
/// - NullAdvertiser if no mDNS support is available
///
/// The returned advertiser's state().available indicates whether
/// mDNS is actually functional on this system.
std::unique_ptr<Advertiser> create_advertiser();

}  // namespace beebium::discovery

#endif  // BEEBIUM_DISCOVERY_ADVERTISER_HPP
