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

#include "AunDiscoveryAnnouncer.hpp"

#include <utility>

namespace beebium {

AunDiscoveryAnnouncer::AunDiscoveryAnnouncer(
        std::uint8_t local_net,
        std::uint8_t local_stn,
        std::uint16_t local_port,
        std::string impl,
        std::string impl_version,
        std::unique_ptr<discovery::Advertiser> advertiser)
    : local_net_(local_net)
    , local_stn_(local_stn)
    , local_port_(local_port)
    , impl_(std::move(impl))
    , impl_version_(std::move(impl_version))
    , advertiser_(advertiser ? std::move(advertiser)
                             : discovery::create_advertiser()) {}

AunDiscoveryAnnouncer::~AunDiscoveryAnnouncer() {
    stop();
}

bool AunDiscoveryAnnouncer::start() {
    if (!advertiser_) return false;
    return advertiser_->start(build_service_info());
}

void AunDiscoveryAnnouncer::stop() {
    if (!advertiser_) return;
    advertiser_->stop();
}

bool AunDiscoveryAnnouncer::is_advertising() const {
    if (!advertiser_) return false;
    return advertiser_->state().advertising;
}

discovery::ServiceInfo AunDiscoveryAnnouncer::build_service_info() const {
    discovery::ServiceInfo info;
    info.service_type = "_aun._udp";
    // Instance name is human-readable; collisions are resolved by the
    // platform (Bonjour appends " (2)", DNS-SD on Windows similar).
    // Including impl + station makes parallel Beebium instances on the
    // same LAN visible at a glance in tools like dns-sd / avahi-browse.
    info.instance_name = "Beebium " + std::to_string(local_net_) + "."
                       + std::to_string(local_stn_);
    info.port = local_port_;
    info.txt_records["version"] = "1";
    info.txt_records["net"] = std::to_string(local_net_);
    info.txt_records["station"] = std::to_string(local_stn_);
    info.txt_records["port"] = std::to_string(local_port_);
    if (!impl_.empty()) {
        info.txt_records["impl"] = impl_;
    }
    if (!impl_version_.empty()) {
        info.txt_records["impl-version"] = impl_version_;
    }
    return info;
}

}  // namespace beebium
