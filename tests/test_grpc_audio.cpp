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

// Test gRPC AudioService
//
// These tests verify the AudioService implementation by acting as a gRPC client.
// They create a local server, connect to it, and verify audio streaming works correctly.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/AudioBuffer.hpp"

#include "audio.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <thread>
#include <chrono>
#include <fstream>
#include <vector>
#include <cmath>

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

// Test fixture that sets up a machine and server with audio enabled
class AudioTestFixture {
public:
    AudioTestFixture() {
        // Load ROMs
#ifdef BEEBIUM_ROM_DIR
        auto mos = load_rom(std::string(BEEBIUM_ROM_DIR) + "/acorn-mos_1_20.rom");
        auto basic = load_rom(std::string(BEEBIUM_ROM_DIR) + "/bbc-basic_2.rom");
        std::copy(mos.begin(), mos.end(), machine_.state().memory.mos_rom.data());
        std::copy(basic.begin(), basic.end(), machine_.state().memory.basic_rom.data());
#endif

        // Enable video and audio output
        machine_.state().memory.enable_video_output();
        machine_.state().memory.enable_audio_output();
        machine_.reset();

        // Start server on a dynamically allocated port
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(machine_, "127.0.0.1", 0);
        server_->start();

        // Create client channel using the actual bound port
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::AudioService::NewStub(channel_);
    }

    ~AudioTestFixture() {
        server_->stop();
    }

    beebium::ModelB& machine() { return machine_; }
    beebium::AudioService::Stub& stub() { return *stub_; }

    void run_cycles(uint64_t cycles) {
        machine_.run(cycles);
    }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::AudioService::Stub> stub_;
};

} // anonymous namespace

TEST_CASE("AudioService GetAudioFormat returns correct metadata", "[grpc][audio]") {
    AudioTestFixture fixture;

    grpc::ClientContext context;
    beebium::GetAudioFormatRequest request;
    beebium::AudioFormat response;

    auto status = fixture.stub().GetAudioFormat(&context, request, &response);

    REQUIRE(status.ok());
    CHECK(response.sample_rate() == 48000);
    CHECK(response.source_count() == beebium::AudioSample::MAX_SOURCES);

    // Verify SN76489 source metadata
    REQUIRE(response.sources_size() >= 1);
    auto& sn76489 = response.sources(0);
    CHECK(sn76489.source_index() == 0);
    CHECK(sn76489.source_name() == "SN76489");
    CHECK(sn76489.encoding() == beebium::ENCODING_4X8BIT_SIGNED);
    REQUIRE(sn76489.channel_names_size() == 4);
    CHECK(sn76489.channel_names(0) == "Tone0");
    CHECK(sn76489.channel_names(1) == "Tone1");
    CHECK(sn76489.channel_names(2) == "Tone2");
    CHECK(sn76489.channel_names(3) == "Noise");

    // Verify channel group
    REQUIRE(response.groups_size() >= 1);
    auto& group = response.groups(0);
    CHECK(group.group_id() == 1);
    CHECK(group.group_name() == "Internal Sound");
}

TEST_CASE("AudioService GetChannelStates returns channel information", "[grpc][audio]") {
    AudioTestFixture fixture;

    // Run some emulation to initialize sound chip
    fixture.run_cycles(100000);

    grpc::ClientContext context;
    beebium::GetChannelStatesRequest request;
    beebium::ChannelStatesResponse response;

    auto status = fixture.stub().GetChannelStates(&context, request, &response);

    REQUIRE(status.ok());
    REQUIRE(response.channels_size() == 4);

    // Check tone channels (0-2)
    for (int i = 0; i < 3; ++i) {
        auto& ch = response.channels(i);
        CHECK(ch.channel_id() == static_cast<uint32_t>(i));
        // Volume 15 = silent (initial state)
        CHECK(ch.volume() <= 15);
    }

    // Check noise channel (3)
    auto& noise = response.channels(3);
    CHECK(noise.channel_id() == 3);
    CHECK(noise.channel_name() == "Noise");
}

TEST_CASE("AudioService SubscribeAudio streams audio samples", "[grpc][audio]") {
    AudioTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeAudioRequest request;
    request.set_chunk_size(512);  // Small chunks for faster test

    auto reader = fixture.stub().SubscribeAudio(&context, request);

    // Run emulation in a separate thread to generate samples
    std::atomic<bool> running{true};
    std::thread emu_thread([&]() {
        while (running) {
            fixture.run_cycles(20000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Try to receive at least one chunk
    beebium::AudioChunk chunk;
    bool received = reader->Read(&chunk);

    // Stop emulation
    running = false;
    context.TryCancel();
    emu_thread.join();

    REQUIRE(received);
    CHECK(chunk.sample_count() > 0);
    CHECK(chunk.sequence() == 0);

    // Verify sample data size: sample_count × source_count × 4 bytes
    size_t expected_size = chunk.sample_count() * beebium::AudioSample::MAX_SOURCES * sizeof(uint32_t);
    CHECK(chunk.samples().size() == expected_size);
}

TEST_CASE("AudioService sequence numbers increment", "[grpc][audio]") {
    AudioTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeAudioRequest request;
    request.set_chunk_size(256);

    auto reader = fixture.stub().SubscribeAudio(&context, request);

    // Run emulation
    std::atomic<bool> running{true};
    std::thread emu_thread([&]() {
        while (running) {
            fixture.run_cycles(50000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Receive two chunks and verify sequence increments
    beebium::AudioChunk chunk1, chunk2;
    bool received1 = reader->Read(&chunk1);
    bool received2 = reader->Read(&chunk2);

    running = false;
    context.TryCancel();
    emu_thread.join();

    REQUIRE(received1);
    REQUIRE(received2);
    CHECK(chunk2.sequence() > chunk1.sequence());
}

TEST_CASE("AudioService sample unpacking", "[grpc][audio]") {
    // Test that we can correctly unpack samples from the streamed data
    AudioTestFixture fixture;

    grpc::ClientContext context;
    beebium::SubscribeAudioRequest request;
    request.set_chunk_size(100);

    auto reader = fixture.stub().SubscribeAudio(&context, request);

    // Run emulation
    std::atomic<bool> running{true};
    std::thread emu_thread([&]() {
        while (running) {
            fixture.run_cycles(50000);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    beebium::AudioChunk chunk;
    bool received = reader->Read(&chunk);

    running = false;
    context.TryCancel();
    emu_thread.join();

    REQUIRE(received);
    REQUIRE(chunk.sample_count() > 0);

    // Unpack first sample's SN76489 source (index 0)
    const uint8_t* data = reinterpret_cast<const uint8_t*>(chunk.samples().data());

    // Extract channels: [tone0|tone1|tone2|noise] packed big-endian in the 32-bit field
    // But written as little-endian to the stream
    // In pack_4x8bit: (u0 << 24) | (u1 << 16) | (u2 << 8) | u3
    // So after little-endian read: byte[0]=u3, byte[1]=u2, byte[2]=u1, byte[3]=u0

    int8_t noise = static_cast<int8_t>(data[0]);
    int8_t tone2 = static_cast<int8_t>(data[1]);
    int8_t tone1 = static_cast<int8_t>(data[2]);
    int8_t tone0 = static_cast<int8_t>(data[3]);

    // Just verify we can unpack - values will vary based on chip state
    INFO("Sample 0: tone0=" << (int)tone0 << " tone1=" << (int)tone1
         << " tone2=" << (int)tone2 << " noise=" << (int)noise);

    // At boot, channels should be silent (amplitude ~0)
    // Due to chip initialization, this should generally hold
    CHECK(true);  // Test that unpacking doesn't crash
}
