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

#ifndef BEEBIUM_SERVICE_AUDIO_SERVICE_HPP
#define BEEBIUM_SERVICE_AUDIO_SERVICE_HPP

#include "audio.grpc.pb.h"
#include "beebium/AudioBuffer.hpp"
#include "beebium/devices/Sn76489.hpp"
#include <grpcpp/grpcpp.h>
#include <thread>
#include <chrono>

namespace beebium {
namespace service {

/// gRPC service implementation for audio streaming and introspection
template<typename MachineType>
class AudioServiceImpl final : public AudioService::Service {
public:
    explicit AudioServiceImpl(MachineType& machine);
    ~AudioServiceImpl() override = default;

    // Non-copyable
    AudioServiceImpl(const AudioServiceImpl&) = delete;
    AudioServiceImpl& operator=(const AudioServiceImpl&) = delete;

    grpc::Status SubscribeAudio(
        grpc::ServerContext* context,
        const SubscribeAudioRequest* request,
        grpc::ServerWriter<AudioChunk>* writer) override;

    grpc::Status GetAudioFormat(
        grpc::ServerContext* context,
        const GetAudioFormatRequest* request,
        AudioFormat* response) override;

    grpc::Status GetChannelStates(
        grpc::ServerContext* context,
        const GetChannelStatesRequest* request,
        ChannelStatesResponse* response) override;

private:
    MachineType& machine_;

    static constexpr size_t DEFAULT_CHUNK_SIZE = 1024;
    static constexpr uint32_t SAMPLE_RATE = 48000;
};

// Template implementation

template<typename MachineType>
AudioServiceImpl<MachineType>::AudioServiceImpl(MachineType& machine)
    : machine_(machine) {
}

template<typename MachineType>
grpc::Status AudioServiceImpl<MachineType>::SubscribeAudio(
    grpc::ServerContext* context,
    const SubscribeAudioRequest* request,
    grpc::ServerWriter<AudioChunk>* writer) {

    size_t chunk_size = request->chunk_size() > 0 ? request->chunk_size() : DEFAULT_CHUNK_SIZE;
    std::vector<AudioSample> samples(chunk_size);
    uint64_t sequence = 0;

    while (!context->IsCancelled()) {
        // Check if audio buffer is available
        if (!machine_.state().memory.audio_buffer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto& audio_buffer = machine_.state().memory.audio_buffer.value();

        if (audio_buffer.available() >= chunk_size) {
            size_t count = audio_buffer.read(samples.data(), chunk_size);

            AudioChunk chunk;
            chunk.set_sequence(sequence++);
            chunk.set_sample_count(static_cast<uint32_t>(count));
            chunk.set_cycle_count(0);  // Reserved for future use

            // Pack samples into bytes: each sample is N × 32-bit fields
            // For now, just source 0 (SN76489) is active
            std::string sample_data;
            sample_data.reserve(count * AudioSample::MAX_SOURCES * sizeof(uint32_t));

            for (size_t i = 0; i < count; ++i) {
                for (size_t j = 0; j < AudioSample::MAX_SOURCES; ++j) {
                    uint32_t value = samples[i].sources[j];
                    // Pack as little-endian bytes
                    sample_data.push_back(static_cast<char>(value & 0xFF));
                    sample_data.push_back(static_cast<char>((value >> 8) & 0xFF));
                    sample_data.push_back(static_cast<char>((value >> 16) & 0xFF));
                    sample_data.push_back(static_cast<char>((value >> 24) & 0xFF));
                }
            }

            chunk.set_samples(std::move(sample_data));

            if (!writer->Write(chunk)) {
                // Client disconnected
                break;
            }
        } else {
            // Not enough samples, wait briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status AudioServiceImpl<MachineType>::GetAudioFormat(
    grpc::ServerContext* /*context*/,
    const GetAudioFormatRequest* /*request*/,
    AudioFormat* response) {

    response->set_sample_rate(SAMPLE_RATE);
    response->set_source_count(AudioSample::MAX_SOURCES);

    // Define channel group for internal sound
    auto* internal_group = response->add_groups();
    internal_group->set_group_id(1);
    internal_group->set_group_name("Internal Sound");
    internal_group->set_description("BBC Micro built-in SN76489 sound chip");
    internal_group->set_color("#FF6B35");  // Orange

    // SN76489 source (always present at index 0)
    auto* sn76489 = response->add_sources();
    sn76489->set_source_index(0);
    sn76489->set_source_name("SN76489");
    sn76489->set_encoding(ENCODING_4X8BIT_UNSIGNED);
    sn76489->add_channel_names("Tone0");
    sn76489->add_channel_names("Tone1");
    sn76489->add_channel_names("Tone2");
    sn76489->add_channel_names("Noise");
    sn76489->set_group_id(1);

    // Reserved sources (silent for now)
    for (size_t i = 1; i < AudioSample::MAX_SOURCES; ++i) {
        auto* reserved = response->add_sources();
        reserved->set_source_index(static_cast<uint32_t>(i));
        reserved->set_source_name("Reserved");
        reserved->set_encoding(ENCODING_SILENCE);
        reserved->set_group_id(0);  // Ungrouped
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status AudioServiceImpl<MachineType>::GetChannelStates(
    grpc::ServerContext* /*context*/,
    const GetChannelStatesRequest* /*request*/,
    ChannelStatesResponse* response) {

    auto& sound_chip = machine_.state().memory.sound_chip;

    // Tone channels (0-2)
    static const char* tone_names[] = {"Tone0", "Tone1", "Tone2"};
    for (size_t i = 0; i < 3; ++i) {
        auto state = sound_chip.get_tone_channel_state(i);

        auto* channel = response->add_channels();
        channel->set_channel_id(static_cast<uint32_t>(i));
        channel->set_channel_name(tone_names[i]);
        channel->set_frequency_divider(state.frequency);
        channel->set_counter(state.counter);
        channel->set_output_bit(state.output_bit);
        channel->set_volume(state.volume);
        channel->set_frequency_hz(state.frequency_hz);
        channel->set_amplitude(state.amplitude);
        channel->set_dc_bias_voltage(state.dc_bias_v);
        channel->set_peak_voltage(state.peak_v);
        channel->set_trough_voltage(state.trough_v);
    }

    // Noise channel (3)
    auto noise_state = sound_chip.get_noise_channel_state();

    auto* noise = response->add_channels();
    noise->set_channel_id(3);
    noise->set_channel_name("Noise");
    noise->set_noise_rate(noise_state.rate_select);
    noise->set_white_noise(noise_state.white_mode);
    noise->set_lfsr_state(noise_state.lfsr);
    noise->set_volume(noise_state.volume);
    noise->set_frequency_hz(noise_state.rate_hz);
    noise->set_amplitude(noise_state.amplitude);
    noise->set_dc_bias_voltage(noise_state.dc_bias_v);
    noise->set_peak_voltage(noise_state.peak_v);
    noise->set_trough_voltage(noise_state.trough_v);

    // Latched register
    response->set_latched_register(sound_chip.latched_register());

    return grpc::Status::OK;
}

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_AUDIO_SERVICE_HPP
