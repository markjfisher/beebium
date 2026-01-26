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

#ifdef BEEBIUM_HAS_BONJOUR

#include <beebium/discovery/Advertiser.hpp>

#include <dns_sd.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace beebium::discovery {

namespace {

constexpr const char* SERVICE_TYPE = "_beebium._tcp";

}  // namespace

/// macOS Bonjour implementation of Advertiser using dns_sd.h.
///
/// Uses DNSServiceRegister() to advertise the service via mDNS.
/// The event loop runs in a background thread to process DNS-SD callbacks.
class BonjourAdvertiser final : public Advertiser {
public:
    BonjourAdvertiser() = default;

    ~BonjourAdvertiser() override { stop(); }

    bool start(const ServiceInfo& info) override {
        // Stop any existing advertisement
        stop();

        // Store info for state reporting
        {
            std::lock_guard lock(mutex_);
            info_ = info;
        }

        // Build TXT record
        TXTRecordRef txt_ref;
        TXTRecordCreate(&txt_ref, 0, nullptr);

        for (const auto& [key, value] : info.txt_records) {
            TXTRecordSetValue(&txt_ref, key.c_str(),
                              static_cast<uint8_t>(value.size()),
                              value.c_str());
        }

        // Register service
        DNSServiceErrorType err = DNSServiceRegister(
            &service_ref_,
            0,  // flags
            kDNSServiceInterfaceIndexAny,
            info.instance_name.c_str(),
            SERVICE_TYPE,
            nullptr,  // domain (default = .local)
            nullptr,  // host (default = this machine)
            htons(info.port),
            TXTRecordGetLength(&txt_ref),
            TXTRecordGetBytesPtr(&txt_ref),
            register_callback,
            this);

        TXTRecordDeallocate(&txt_ref);

        if (err != kDNSServiceErr_NoError) {
            service_ref_ = nullptr;
            return false;
        }

        // Start event loop thread
        running_ = true;
        event_thread_ = std::thread([this] { event_loop(); });

        return true;
    }

    void stop() override {
        // Signal event loop to stop
        running_ = false;

        // Deallocate service (causes DNSServiceProcessResult to return)
        if (service_ref_) {
            DNSServiceRefDeallocate(service_ref_);
            service_ref_ = nullptr;
        }

        // Wait for event thread to finish
        if (event_thread_.joinable()) {
            event_thread_.join();
        }

        // Reset state
        registered_ = false;
        {
            std::lock_guard lock(mutex_);
            actual_name_.clear();
        }
    }

    AdvertiserState state() const override {
        std::lock_guard lock(mutex_);
        return AdvertiserState{
            .available = true,  // Bonjour is always available on macOS
            .advertising = registered_.load(),
            .actual_name = actual_name_,
        };
    }

private:
    DNSServiceRef service_ref_ = nullptr;
    std::atomic<bool> registered_{false};
    std::atomic<bool> running_{false};
    std::thread event_thread_;

    mutable std::mutex mutex_;
    ServiceInfo info_;
    std::string actual_name_;

    void event_loop() {
        while (running_ && service_ref_) {
            // DNSServiceProcessResult blocks until an event is ready
            // or the service ref is deallocated
            DNSServiceErrorType err = DNSServiceProcessResult(service_ref_);
            if (err != kDNSServiceErr_NoError) {
                // Service was deallocated or error occurred
                break;
            }
        }
    }

    static void register_callback(DNSServiceRef /*sdRef*/,
                                  DNSServiceFlags /*flags*/,
                                  DNSServiceErrorType errorCode,
                                  const char* name, const char* /*regtype*/,
                                  const char* /*domain*/, void* context) {
        auto* self = static_cast<BonjourAdvertiser*>(context);

        if (errorCode == kDNSServiceErr_NoError) {
            self->registered_ = true;
            // The name may differ from what we requested if there was a
            // collision (DNS-SD appends " (2)", " (3)", etc.)
            std::lock_guard lock(self->mutex_);
            self->actual_name_ = name;
        } else {
            self->registered_ = false;
        }
    }
};

std::unique_ptr<Advertiser> create_advertiser() {
    return std::make_unique<BonjourAdvertiser>();
}

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_BONJOUR
