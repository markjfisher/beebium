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

#include "AunDiscoverySubscriber.hpp"

#include <beebium/econet/AunBackend.hpp>

#include <charconv>
#include <utility>

namespace beebium {

namespace {

bool parse_byte(const std::string& s, std::uint8_t& out) {
    if (s.empty()) return false;
    unsigned long v = 0;
    auto first = s.data();
    auto last = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, v);
    if (ec != std::errc{} || ptr != last || v > 255) return false;
    out = static_cast<std::uint8_t>(v);
    return true;
}

}  // namespace

AunDiscoverySubscriber::AunDiscoverySubscriber(
        AunBackend& backend,
        std::uint8_t local_stn,
        std::unique_ptr<discovery::Browser> browser)
    : backend_(backend)
    , local_stn_(local_stn)
    , browser_(browser ? std::move(browser) : discovery::create_browser()) {}

AunDiscoverySubscriber::~AunDiscoverySubscriber() {
    stop();
}

bool AunDiscoverySubscriber::start() {
    if (!browser_) return false;
    discovery::BrowserCallbacks cbs;
    cbs.on_added = [this](const discovery::DiscoveredService& svc) {
        handle_added(svc);
    };
    cbs.on_removed = [this](const std::string& name) {
        handle_removed(name);
    };
    return browser_->start(service_type_, std::move(cbs));
}

void AunDiscoverySubscriber::stop() {
    if (!browser_) return;
    browser_->stop();
    std::lock_guard lock(name_map_mutex_);
    name_to_peer_.clear();
}

bool AunDiscoverySubscriber::is_subscribed() const {
    if (!browser_) return false;
    return browser_->state().browsing;
}

void AunDiscoverySubscriber::set_on_peers_changed(std::function<void()> cb) {
    std::lock_guard lock(callback_mutex_);
    on_peers_changed_ = std::move(cb);
}

bool AunDiscoverySubscriber::parse_txt(
        const std::map<std::string, std::string>& txt,
        std::uint8_t& net_out,
        std::uint8_t& stn_out) {
    auto v_it = txt.find("version");
    if (v_it == txt.end() || v_it->second != "1") return false;
    auto net_it = txt.find("net");
    auto stn_it = txt.find("station");
    if (net_it == txt.end() || stn_it == txt.end()) return false;
    if (!parse_byte(net_it->second, net_out)) return false;
    if (!parse_byte(stn_it->second, stn_out)) return false;
    return true;
}

void AunDiscoverySubscriber::inject_added(
        const discovery::DiscoveredService& svc) {
    handle_added(svc);
}

void AunDiscoverySubscriber::inject_removed(const std::string& instance_name) {
    handle_removed(instance_name);
}

void AunDiscoverySubscriber::handle_added(
        const discovery::DiscoveredService& svc) {
    std::uint8_t net = 0;
    std::uint8_t stn = 0;
    if (!parse_txt(svc.txt_records, net, stn)) {
        return;  // Schema invalid -- safer to ignore than to guess.
    }

    // Skip our own announcement: same (net, stn) means it's us.
    // (We compare net to backend_.local_net() so a Beebium that
    // changes its local_net at runtime correctly stops self-filtering
    // its old announcement -- though that's not currently possible.)
    if (net == backend_.local_net() && stn == local_stn_) {
        return;
    }

    if (svc.ipv4_addr_net_byte_order == 0 || svc.port == 0) {
        return;  // No usable endpoint yet (e.g. IPv6-only peer).
    }

    // Skip the change-callback if the operator already pinned this
    // (net, stn) -- add_peer will refuse to overwrite, so the peer
    // table didn't actually change.
    bool operator_pinned =
        backend_.is_operator_configured(net, stn);

    backend_.add_peer(net, stn, svc.ipv4_addr_net_byte_order, svc.port,
                      PeerSource::Discovered);

    {
        std::lock_guard lock(name_map_mutex_);
        name_to_peer_[svc.instance_name] = {net, stn};
    }

    if (!operator_pinned) {
        notify_peers_changed();
    }
}

void AunDiscoverySubscriber::handle_removed(const std::string& instance_name) {
    std::pair<std::uint8_t, std::uint8_t> peer{};
    {
        std::lock_guard lock(name_map_mutex_);
        auto it = name_to_peer_.find(instance_name);
        if (it == name_to_peer_.end()) return;
        peer = it->second;
        name_to_peer_.erase(it);
    }

    // Don't yank an operator-configured entry just because the
    // discovered shadow went away -- that would surprise an operator
    // who set the peer manually after the discovery added it.
    if (backend_.is_operator_configured(peer.first, peer.second)) return;

    backend_.remove_peer(peer.first, peer.second);
    notify_peers_changed();
}

void AunDiscoverySubscriber::notify_peers_changed() {
    std::function<void()> cb;
    {
        std::lock_guard lock(callback_mutex_);
        cb = on_peers_changed_;
    }
    if (cb) cb();
}

}  // namespace beebium
