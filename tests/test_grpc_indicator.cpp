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

// Test gRPC IndicatorService
//
// These tests verify the IndicatorService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify indicator operations work correctly.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "indicator.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <fstream>
#include <thread>
#include <vector>

namespace {

// Helper to load ROM file
std::vector<uint8_t> load_rom(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open ROM: " + filepath);
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Test fixture for Model B
class IndicatorTestFixtureModelB {
public:
    IndicatorTestFixtureModelB() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        std::copy(basic.begin(), basic.end(), machine_.state().memory.basic_rom.data());
#endif
        machine_.reset();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start();

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::IndicatorService::NewStub(channel_);
    }

    ~IndicatorTestFixtureModelB() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::IndicatorService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::IndicatorService::Stub> stub_;
};

// Test fixture for Model B+ (has disc drive activity LEDs)
class IndicatorTestFixtureBPlus {
public:
    IndicatorTestFixtureBPlus() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_2_0.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        std::copy(basic.begin(), basic.end(), machine_.state().memory.basic_rom.data());
#endif
        machine_.reset();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelBPlus>>(
            machine_, "127.0.0.1", 0);
        server_->start();

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::IndicatorService::NewStub(channel_);
    }

    ~IndicatorTestFixtureBPlus() {
        server_->stop();
    }

    beebium::ModelBPlus& machine() { return machine_; }
    beebium::IndicatorService::Stub& stub() { return *stub_; }

private:
    beebium::ModelBPlus machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelBPlus>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::IndicatorService::Stub> stub_;
};

} // anonymous namespace

// =============================================================================
// ListIndicators tests
// =============================================================================

TEST_CASE("IndicatorService ListIndicators returns registered indicators for Model B", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::ListIndicatorsRequest request;
    beebium::ListIndicatorsResponse response;

    auto status = fixture.stub().ListIndicators(&context, request, &response);

    REQUIRE(status.ok());
    // Model B has caps-lock-led and shift-lock-led
    REQUIRE(response.indicators_size() >= 2);

    // Find caps-lock-led
    bool found_caps = false;
    bool found_shift = false;
    for (const auto& indicator : response.indicators()) {
        if (indicator.name() == "caps-lock-led") {
            found_caps = true;
            CHECK(indicator.metadata().count("label") > 0);
            CHECK(indicator.metadata().at("label") == "CAPS LOCK");
        }
        if (indicator.name() == "shift-lock-led") {
            found_shift = true;
            CHECK(indicator.metadata().count("label") > 0);
            CHECK(indicator.metadata().at("label") == "SHIFT LOCK");
        }
    }
    CHECK(found_caps);
    CHECK(found_shift);
}

TEST_CASE("IndicatorService ListIndicators returns disc indicators for Model B+", "[grpc][indicator]") {
    IndicatorTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::ListIndicatorsRequest request;
    beebium::ListIndicatorsResponse response;

    auto status = fixture.stub().ListIndicators(&context, request, &response);

    REQUIRE(status.ok());
    // Model B+ has caps, shift, and two disc activity LEDs
    REQUIRE(response.indicators_size() >= 4);

    bool found_floppy_0 = false;
    bool found_floppy_1 = false;
    for (const auto& indicator : response.indicators()) {
        if (indicator.name() == "floppy-0-activity-led") {
            found_floppy_0 = true;
            CHECK(indicator.metadata().count("label") > 0);
            CHECK(indicator.metadata().at("label") == "Drive 0");
            CHECK(indicator.metadata().count("color") > 0);
            CHECK(indicator.metadata().at("color") == "590nm");
        }
        if (indicator.name() == "floppy-1-activity-led") {
            found_floppy_1 = true;
            CHECK(indicator.metadata().at("label") == "Drive 1");
        }
    }
    CHECK(found_floppy_0);
    CHECK(found_floppy_1);
}

TEST_CASE("IndicatorService ListIndicators includes metadata", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::ListIndicatorsRequest request;
    beebium::ListIndicatorsResponse response;

    auto status = fixture.stub().ListIndicators(&context, request, &response);

    REQUIRE(status.ok());

    // Find caps-lock-led and check all metadata fields
    for (const auto& indicator : response.indicators()) {
        if (indicator.name() == "caps-lock-led") {
            CHECK(indicator.metadata().at("color") == "470nm");
            CHECK(indicator.metadata().at("shape") == "domed");
            return;
        }
    }
    FAIL("caps-lock-led not found");
}

// =============================================================================
// GetIndicators tests
// =============================================================================

TEST_CASE("IndicatorService GetIndicators returns current values", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.changed());
    // Sequence starts at 0, no need to check > 0
    CHECK(response.values().count("caps-lock-led") > 0);
    CHECK(response.values().count("shift-lock-led") > 0);
}

TEST_CASE("IndicatorService GetIndicators conditional fetch returns unchanged when sequence matches", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // First, trigger a change to make sequence > 0
    fixture.machine().state().memory.indicators.set("caps-lock-led", 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Get the current sequence after the change
    uint64_t current_seq;
    {
        grpc::ClientContext context;
        beebium::GetIndicatorsRequest request;
        beebium::GetIndicatorsResponse response;
        fixture.stub().GetIndicators(&context, request, &response);
        current_seq = response.sequence();
        REQUIRE(current_seq > 0);  // Must have a valid sequence
    }

    // Second fetch with if_changed_since - should return unchanged
    {
        grpc::ClientContext context;
        beebium::GetIndicatorsRequest request;
        request.set_if_changed_since(current_seq);
        beebium::GetIndicatorsResponse response;

        auto status = fixture.stub().GetIndicators(&context, request, &response);

        REQUIRE(status.ok());
        CHECK_FALSE(response.changed());
        CHECK(response.sequence() == current_seq);
        CHECK(response.values().empty());
    }
}

TEST_CASE("IndicatorService GetIndicators conditional fetch returns data when sequence changed", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // First fetch
    uint64_t old_seq;
    {
        grpc::ClientContext context;
        beebium::GetIndicatorsRequest request;
        beebium::GetIndicatorsResponse response;
        fixture.stub().GetIndicators(&context, request, &response);
        old_seq = response.sequence();
    }

    // Trigger an indicator change by manipulating the addressable latch
    // The caps lock LED is controlled by bit 6 of the latch
    fixture.machine().state().memory.addressable_latch.write(6, true);  // Caps Lock LED on
    fixture.machine().state().memory.indicators.set("caps-lock-led", 255);

    // Wait for consumer thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Second fetch with old sequence
    {
        grpc::ClientContext context;
        beebium::GetIndicatorsRequest request;
        request.set_if_changed_since(old_seq);
        beebium::GetIndicatorsResponse response;

        auto status = fixture.stub().GetIndicators(&context, request, &response);

        REQUIRE(status.ok());
        CHECK(response.changed());
        CHECK(response.sequence() > old_seq);
        CHECK_FALSE(response.values().empty());
    }
}

TEST_CASE("IndicatorService GetIndicators reflects LED state changes", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // Turn on caps lock LED via indicator system
    fixture.machine().state().memory.indicators.set("caps-lock-led", 255);

    // Wait for consumer thread
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    // Value should be non-zero (duty cycle filter may not show full 255)
    CHECK(response.values().at("caps-lock-led") > 0);
}

// =============================================================================
// Subscribe tests
// =============================================================================

TEST_CASE("IndicatorService Subscribe streams updates", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // We need to run the subscriber in a separate thread since Read() blocks
    std::atomic<int> update_count{0};
    std::atomic<bool> stop_reading{false};

    grpc::ClientContext context;
    beebium::SubscribeIndicatorsRequest request;
    request.set_min_interval_ms(10);  // Fast updates for testing

    auto reader = fixture.stub().Subscribe(&context, request);

    // Start a thread to read updates
    std::thread reader_thread([&]() {
        beebium::IndicatorUpdate update;
        while (!stop_reading && reader->Read(&update)) {
            update_count++;
        }
    });

    // Give reader time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Trigger some indicator changes
    for (int i = 0; i < 5; ++i) {
        fixture.machine().state().memory.indicators.set("caps-lock-led", i % 2 == 0 ? 255 : 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    // Give time for updates to propagate
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop reading and cancel
    stop_reading = true;
    context.TryCancel();

    reader_thread.join();

    // Should have received at least one update
    CHECK(update_count.load() >= 1);
}

TEST_CASE("IndicatorService Subscribe filters by name", "[grpc][indicator]") {
    IndicatorTestFixtureBPlus fixture;

    grpc::ClientContext context;
    beebium::SubscribeIndicatorsRequest request;
    request.add_names("caps-lock-led");  // Only subscribe to caps lock
    request.set_min_interval_ms(10);

    auto reader = fixture.stub().Subscribe(&context, request);

    // Change both caps lock and floppy LED
    fixture.machine().state().memory.indicators.set("caps-lock-led", 255);
    fixture.machine().state().memory.indicators.set("floppy-0-activity-led", 255);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    context.TryCancel();

    // Read updates
    beebium::IndicatorUpdate update;
    while (reader->Read(&update)) {
        // Should only contain caps-lock-led, not floppy
        CHECK(update.values().count("floppy-0-activity-led") == 0);
    }
}

// =============================================================================
// Model B+ specific tests
// =============================================================================

TEST_CASE("IndicatorService Model B+ disc motor indicator updates", "[grpc][indicator]") {
    IndicatorTestFixtureBPlus fixture;

    // Turn on disc motor for drive 0
    fixture.machine().state().memory.disc_drive_0.set_motor(true);

    // Wait for consumer thread
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.values().at("floppy-0-activity-led") > 0);
    // Drive 1 should be off
    CHECK(response.values().at("floppy-1-activity-led") == 0);
}

TEST_CASE("IndicatorService Model B+ both drives can be active", "[grpc][indicator]") {
    IndicatorTestFixtureBPlus fixture;

    // Turn on both disc motors
    fixture.machine().state().memory.disc_drive_0.set_motor(true);
    fixture.machine().state().memory.disc_drive_1.set_motor(true);

    // Wait for consumer thread
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.values().at("floppy-0-activity-led") > 0);
    CHECK(response.values().at("floppy-1-activity-led") > 0);
}

// =============================================================================
// Edge cases
// =============================================================================

TEST_CASE("IndicatorService GetIndicators with zero sequence returns all", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    request.set_if_changed_since(0);  // Explicitly set to 0
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.changed());
    CHECK_FALSE(response.values().empty());
}

TEST_CASE("IndicatorService returns correct sequence after rapid updates", "[grpc][indicator]") {
    IndicatorTestFixtureModelB fixture;

    // Rapid updates with enough time between for consumer to notice
    for (int i = 0; i < 5; ++i) {
        fixture.machine().state().memory.indicators.set("caps-lock-led", i % 2 == 0 ? 255 : 0);
        // Small delay between updates so consumer can see distinct changes
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    // Wait for consumer thread to process (runs at 50Hz = 20ms intervals)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    beebium::GetIndicatorsResponse response;

    auto status = fixture.stub().GetIndicators(&context, request, &response);

    REQUIRE(status.ok());
    // Sequence should have advanced
    CHECK(response.sequence() >= 1);
}
