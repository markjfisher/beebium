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

#ifndef BEEBIUM_SERVICE_DEVICE_INSPECTION_SERVICE_HPP
#define BEEBIUM_SERVICE_DEVICE_INSPECTION_SERVICE_HPP

#include "debugger.grpc.pb.h"
#include <beebium/tube/TubeSocket.hpp>
#include <beebium/tube/TubeUla.hpp>
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <string>

namespace beebium::service {

// Helper function to fill ViaState from a Via6522 instance
template<typename ViaType>
void fill_via_state(ViaType& via, ViaState* response) {
    const auto& state = via.state();

    // Port state
    response->set_ora(state.port_a.or_);
    response->set_orb(state.port_b.or_);
    response->set_ddra(state.port_a.ddr);
    response->set_ddrb(state.port_b.ddr);

    // Computed input values (port pins masked by DDR for inputs)
    response->set_ira(state.port_a.p & ~state.port_a.ddr);
    response->set_irb(state.port_b.p & ~state.port_b.ddr);

    // Timer 1 - use effective value for accurate counter
    response->set_t1c(via.effective_t1());
    response->set_t1l((static_cast<uint16_t>(state.t1lh) << 8) | state.t1ll);

    // Timer 2 - use effective value for accurate counter
    response->set_t2c(via.effective_t2());
    response->set_t2l(state.t2ll);  // Only low byte is latched

    // Control registers
    response->set_acr(state.acr.value);
    response->set_pcr(state.pcr.value);
    response->set_sr(state.sr);

    // Interrupt registers
    response->set_ifr(state.ifr.value);
    response->set_ier(state.ier.value);

    // Internal state
    response->set_t1_pending(state.t1_pending);
    response->set_t2_pending(state.t2_pending);
    response->set_t1_pb7(state.t1_pb7);

    // Control lines
    response->set_ca1(state.port_a.c1 != 0);
    response->set_ca2(state.port_a.c2 != 0);
    response->set_cb1(state.port_b.c1 != 0);
    response->set_cb2(state.port_b.c2 != 0);
}

// Helper to fill TubeState from a TubeUla (in-process mode).
inline void fill_tube_state_from_ula(const TubeUla& ula, TubeState* response) {
    uint8_t flags = ula.control_flags();

    // Control flags.
    auto* cf = response->mutable_control_flags();
    cf->set_q((flags & TubeUla::FLAG_Q) != 0);
    cf->set_i((flags & TubeUla::FLAG_I) != 0);
    cf->set_j((flags & TubeUla::FLAG_J) != 0);
    cf->set_m((flags & TubeUla::FLAG_M) != 0);
    cf->set_v((flags & TubeUla::FLAG_V) != 0);
    cf->set_p((flags & TubeUla::FLAG_P) != 0);

    uint8_t threshold = (flags & TubeUla::FLAG_V) ? 2 : 1;

    // R1 H-to-P latch: peek data register (offset 1 from parasite perspective).
    auto* r1_h2p = response->mutable_r1_h2p();
    r1_h2p->set_value(ula.parasite_peek(1));
    // Data available = parasite can read from H-to-P
    r1_h2p->set_data_available((ula.parasite_peek(0) & TubeUla::DATA_AVAILABLE) != 0);

    // R1 P-to-H FIFO.
    auto* r1_p2h = response->mutable_r1_p2h();
    // Status from host side tells us about P-to-H
    uint8_t r1_host_status = ula.host_peek(0);
    r1_p2h->set_data_available((r1_host_status & TubeUla::DATA_AVAILABLE) != 0);
    // We can peek the head of the FIFO, but to get all data we need to read
    // from the TubeUla's internal state. Since host_peek(1) only shows the head,
    // we set count from status and show what's visible.
    // For the in-process model, count isn't directly exposed through peek,
    // but we can infer from data_available.
    // For a proper implementation, we should expose a snapshot method.
    // For now, show the head byte via peek.
    uint8_t head_byte = ula.host_peek(1);
    r1_p2h->set_count(r1_p2h->data_available() ? 1 : 0);  // Minimum visible count
    if (r1_p2h->data_available()) {
        r1_p2h->set_data(std::string(1, static_cast<char>(head_byte)));
    }

    // R2 H-to-P latch.
    auto* r2_h2p = response->mutable_r2_h2p();
    r2_h2p->set_value(ula.parasite_peek(3));
    r2_h2p->set_data_available((ula.parasite_peek(2) & TubeUla::DATA_AVAILABLE) != 0);

    // R2 P-to-H latch.
    auto* r2_p2h = response->mutable_r2_p2h();
    r2_p2h->set_value(ula.host_peek(3));
    r2_p2h->set_data_available((ula.host_peek(2) & TubeUla::DATA_AVAILABLE) != 0);

    // R3 H-to-P register.
    auto* r3_h2p = response->mutable_r3_h2p();
    r3_h2p->set_threshold(threshold);
    uint8_t parasite_r3_status = ula.parasite_peek(4);
    bool h2p_has_data = (parasite_r3_status & TubeUla::DATA_AVAILABLE) != 0;
    r3_h2p->set_pending(h2p_has_data);
    uint8_t h2p_head = ula.parasite_peek(5);
    r3_h2p->set_count(h2p_has_data ? 1 : 0);  // Minimum visible count
    if (h2p_has_data) {
        r3_h2p->set_data(std::string(1, static_cast<char>(h2p_head)));
    }

    // R3 P-to-H register.
    auto* r3_p2h = response->mutable_r3_p2h();
    r3_p2h->set_threshold(threshold);
    uint8_t host_r3_status = ula.host_peek(4);
    bool p2h_has_data = (host_r3_status & TubeUla::DATA_AVAILABLE) != 0;
    r3_p2h->set_pending(p2h_has_data);
    uint8_t p2h_head = ula.host_peek(5);
    r3_p2h->set_count(p2h_has_data ? 1 : 0);  // Minimum visible count
    if (p2h_has_data) {
        r3_p2h->set_data(std::string(1, static_cast<char>(p2h_head)));
    }

    // R4 H-to-P latch.
    auto* r4_h2p = response->mutable_r4_h2p();
    r4_h2p->set_value(ula.parasite_peek(7));
    r4_h2p->set_data_available((ula.parasite_peek(6) & TubeUla::DATA_AVAILABLE) != 0);

    // R4 P-to-H latch.
    auto* r4_p2h = response->mutable_r4_p2h();
    r4_p2h->set_value(ula.host_peek(7));
    r4_p2h->set_data_available((ula.host_peek(6) & TubeUla::DATA_AVAILABLE) != 0);

    // Host status registers.
    auto* hs = response->mutable_host_status();
    hs->set_r1_status(ula.host_peek(0));
    hs->set_r2_status(ula.host_peek(2));
    hs->set_r3_status(ula.host_peek(4));
    hs->set_r4_status(ula.host_peek(6));

    // Parasite status registers.
    auto* ps = response->mutable_parasite_status();
    ps->set_r1_status(ula.parasite_peek(0));
    ps->set_r2_status(ula.parasite_peek(2));
    ps->set_r3_status(ula.parasite_peek(4));
    ps->set_r4_status(ula.parasite_peek(6));

    // Interrupts.
    auto* irq = response->mutable_interrupts();
    irq->set_hirq(ula.hirq());
    irq->set_pirq(ula.pirq());
    irq->set_pnmi_level(ula.pnmi());
    irq->set_pnmi_edge(ula.pnmi());

    // Bus stretching.
    response->set_host_stretched(ula.stretched());
    response->set_enabled(true);
}

// Helper to fill TubeState from shared memory (TubeHostPort mode).
// Only host-side state is available; parasite status requires a separate
// connection to the parasite's gRPC server.
inline void fill_tube_state_from_shared(const TubeHostPort& host_port, TubeState* response) {
    const TubeShared* shared = host_port.shared();
    uint8_t flags = shared->control_flags.load(std::memory_order_acquire);

    // Control flags.
    auto* cf = response->mutable_control_flags();
    cf->set_q((flags & TubeUla::FLAG_Q) != 0);
    cf->set_i((flags & TubeUla::FLAG_I) != 0);
    cf->set_j((flags & TubeUla::FLAG_J) != 0);
    cf->set_m((flags & TubeUla::FLAG_M) != 0);
    cf->set_v((flags & TubeUla::FLAG_V) != 0);
    cf->set_p((flags & TubeUla::FLAG_P) != 0);

    uint8_t threshold = (flags & TubeUla::FLAG_V) ? 2 : 1;

    // R1 H-to-P latch.
    auto* r1_h2p = response->mutable_r1_h2p();
    r1_h2p->set_value(shared->r1_h2p.value.load(std::memory_order_acquire));
    r1_h2p->set_data_available(shared->r1_h2p.ready.load(std::memory_order_acquire) != 0);

    // R1 P-to-H FIFO.
    auto* r1_p2h = response->mutable_r1_p2h();
    uint8_t r1_count = shared->r1_p2h.count.load(std::memory_order_acquire);
    r1_p2h->set_count(r1_count);
    r1_p2h->set_data_available(r1_count > 0);
    if (r1_count > 0) {
        std::string fifo_data;
        fifo_data.reserve(r1_count);
        uint8_t head = shared->r1_p2h.head.load(std::memory_order_relaxed);
        for (uint8_t i = 0; i < r1_count; ++i) {
            fifo_data.push_back(static_cast<char>(
                shared->r1_p2h.data[(head + i) % 24].load(std::memory_order_relaxed)));
        }
        r1_p2h->set_data(std::move(fifo_data));
    }

    // R2 H-to-P latch.
    auto* r2_h2p = response->mutable_r2_h2p();
    r2_h2p->set_value(shared->r2_h2p.value.load(std::memory_order_acquire));
    r2_h2p->set_data_available(shared->r2_h2p.ready.load(std::memory_order_acquire) != 0);

    // R2 P-to-H latch.
    auto* r2_p2h = response->mutable_r2_p2h();
    r2_p2h->set_value(shared->r2_p2h.value.load(std::memory_order_acquire));
    r2_p2h->set_data_available(shared->r2_p2h.ready.load(std::memory_order_acquire) != 0);

    // R3 H-to-P register.
    auto* r3_h2p = response->mutable_r3_h2p();
    uint8_t r3_h2p_count = shared->r3_h2p.count.load(std::memory_order_acquire);
    r3_h2p->set_count(r3_h2p_count);
    r3_h2p->set_pending(r3_h2p_count >= threshold);
    r3_h2p->set_threshold(threshold);
    if (r3_h2p_count > 0) {
        std::string data;
        uint8_t head = shared->r3_h2p.head.load(std::memory_order_relaxed);
        for (uint8_t i = 0; i < r3_h2p_count && i < 2; ++i) {
            data.push_back(static_cast<char>(
                shared->r3_h2p.data[(head + i) & 1].load(std::memory_order_relaxed)));
        }
        r3_h2p->set_data(std::move(data));
    }

    // R3 P-to-H register.
    auto* r3_p2h = response->mutable_r3_p2h();
    uint8_t r3_p2h_count = shared->r3_p2h.count.load(std::memory_order_acquire);
    r3_p2h->set_count(r3_p2h_count);
    r3_p2h->set_pending(r3_p2h_count >= threshold);
    r3_p2h->set_threshold(threshold);
    if (r3_p2h_count > 0) {
        std::string data;
        uint8_t head = shared->r3_p2h.head.load(std::memory_order_relaxed);
        for (uint8_t i = 0; i < r3_p2h_count && i < 2; ++i) {
            data.push_back(static_cast<char>(
                shared->r3_p2h.data[(head + i) & 1].load(std::memory_order_relaxed)));
        }
        r3_p2h->set_data(std::move(data));
    }

    // R4 H-to-P latch.
    auto* r4_h2p = response->mutable_r4_h2p();
    r4_h2p->set_value(shared->r4_h2p.value.load(std::memory_order_acquire));
    r4_h2p->set_data_available(shared->r4_h2p.ready.load(std::memory_order_acquire) != 0);

    // R4 P-to-H latch.
    auto* r4_p2h = response->mutable_r4_p2h();
    r4_p2h->set_value(shared->r4_p2h.value.load(std::memory_order_acquire));
    r4_p2h->set_data_available(shared->r4_p2h.ready.load(std::memory_order_acquire) != 0);

    // Host status registers (computed from peek).
    auto* hs = response->mutable_host_status();
    hs->set_r1_status(host_port.host_peek(0));
    hs->set_r2_status(host_port.host_peek(2));
    hs->set_r3_status(host_port.host_peek(4));
    hs->set_r4_status(host_port.host_peek(6));

    // Parasite status registers (computed from shared state, same as parasite_peek).
    auto* ps = response->mutable_parasite_status();
    {
        uint8_t s = flags & 0x3F;
        if (shared->r1_h2p.ready.load(std::memory_order_acquire) != 0)
            s |= TubeUla::DATA_AVAILABLE;
        if (shared->r1_p2h.count.load(std::memory_order_acquire) < 24)
            s |= TubeUla::SPACE_AVAILABLE;
        ps->set_r1_status(s);
    }
    {
        uint8_t s = 0x3F;
        if (shared->r2_h2p.ready.load(std::memory_order_acquire) != 0)
            s |= TubeUla::DATA_AVAILABLE;
        if (shared->r2_p2h.ready.load(std::memory_order_acquire) == 0)
            s |= TubeUla::SPACE_AVAILABLE;
        ps->set_r2_status(s);
    }
    {
        uint8_t s = 0x1F;
        bool h2p_data = shared->r3_h2p.count.load(std::memory_order_acquire) >= threshold;
        bool p2h_space = shared->r3_p2h.count.load(std::memory_order_acquire) < threshold;
        if (h2p_data) s |= TubeUla::DATA_AVAILABLE;
        if (p2h_space) s |= TubeUla::SPACE_AVAILABLE;
        if (h2p_data || p2h_space) s |= 0x20;
        ps->set_r3_status(s);
    }
    {
        uint8_t s = 0x3F;
        if (shared->r4_h2p.ready.load(std::memory_order_acquire) != 0)
            s |= TubeUla::DATA_AVAILABLE;
        if (shared->r4_p2h.ready.load(std::memory_order_acquire) == 0)
            s |= TubeUla::SPACE_AVAILABLE;
        ps->set_r4_status(s);
    }

    // Interrupts.
    auto* irq = response->mutable_interrupts();
    irq->set_hirq(host_port.hirq());
    // PIRQ computation (from shared state).
    bool pirq = ((flags & TubeUla::FLAG_I)
                 && shared->r1_h2p.ready.load(std::memory_order_acquire) != 0)
             || ((flags & TubeUla::FLAG_J)
                 && shared->r4_h2p.ready.load(std::memory_order_acquire) != 0);
    irq->set_pirq(pirq);
    // PNMI level (from shared state).
    bool pnmi_level = false;
    if (flags & TubeUla::FLAG_M) {
        bool h2p_data = shared->r3_h2p.count.load(std::memory_order_acquire) >= threshold;
        bool p2h_space = shared->r3_p2h.count.load(std::memory_order_acquire) < threshold;
        pnmi_level = h2p_data || p2h_space;
    }
    irq->set_pnmi_level(pnmi_level);
    // Edge state is parasite-local and not available from the host side.
    irq->set_pnmi_edge(false);

    // No bus stretching in shared memory mode (uses spin-waits instead).
    response->set_host_stretched(false);
    response->set_enabled(true);
}

// gRPC service implementation for DeviceInspection.
// Provides access to BBC Micro device state (VIAs, CRTC, Video ULA, etc.).
// Only meaningful on the host; parasite has no BBC Micro devices.
template<typename MachineType>
class DeviceInspectionServiceImpl final : public DeviceInspection::Service {
public:
    explicit DeviceInspectionServiceImpl(MachineType& machine)
        : machine_(machine) {}

    ~DeviceInspectionServiceImpl() override = default;

    DeviceInspectionServiceImpl(const DeviceInspectionServiceImpl&) = delete;
    DeviceInspectionServiceImpl& operator=(const DeviceInspectionServiceImpl&) = delete;

    grpc::Status GetSystemViaState(
        grpc::ServerContext* /*context*/,
        const GetSystemViaStateRequest* /*request*/,
        ViaState* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fill_via_state(machine_.memory().system_via, response);
        return grpc::Status::OK;
    }

    grpc::Status GetUserViaState(
        grpc::ServerContext* /*context*/,
        const GetUserViaStateRequest* /*request*/,
        ViaState* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fill_via_state(machine_.memory().user_via, response);
        return grpc::Status::OK;
    }

    grpc::Status GetCrtcState(
        grpc::ServerContext* /*context*/,
        const GetCrtcStateRequest* /*request*/,
        CrtcState* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto& crtc = machine_.memory().crtc;

        // All 18 registers (R0-R17)
        for (int i = 0; i < 18; ++i) {
            response->add_registers(crtc.reg(static_cast<uint8_t>(i)));
        }

        // Current timing state
        response->set_address_register(crtc.address_register());
        response->set_column(crtc.column());
        response->set_row(crtc.row());
        response->set_raster(crtc.raster());
        response->set_char_addr(crtc.address());

        // Computed values
        response->set_screen_start(crtc.screen_start());
        response->set_cursor_position(crtc.cursor_position());

        // Sync and display state
        response->set_in_hsync(crtc.in_hsync());
        response->set_in_vsync(crtc.in_vsync());
        response->set_display_enabled(crtc.display_enabled());

        return grpc::Status::OK;
    }

    grpc::Status GetVideoUlaState(
        grpc::ServerContext* /*context*/,
        const GetVideoUlaStateRequest* /*request*/,
        VideoUlaState* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto& ula = machine_.memory().video_ula;

        // Control register
        response->set_control(ula.control());

        // Palette (16 entries, logical -> physical)
        for (int i = 0; i < 16; ++i) {
            response->add_palette(ula.palette(static_cast<uint8_t>(i)));
        }

        return grpc::Status::OK;
    }

    grpc::Status GetAddressableLatchState(
        grpc::ServerContext* /*context*/,
        const GetAddressableLatchStateRequest* /*request*/,
        AddressableLatchState* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto& latch = machine_.memory().addressable_latch;

        // Raw 8-bit value
        response->set_value(latch.value);

        // Decoded fields
        response->set_screen_base(latch.screen_base());
        response->set_sound_write_enable(latch.sound_write_enabled());
        response->set_speech_read((latch.value & 0x02) == 0);  // Bit 1, active low
        response->set_speech_write((latch.value & 0x04) == 0);  // Bit 2, active low
        response->set_keyboard_write(latch.keyboard_enabled());
        response->set_caps_lock_led(latch.caps_lock_led());
        response->set_shift_lock_led(latch.shift_lock_led());

        return grpc::Status::OK;
    }

    grpc::Status GetSoundGeneratorState(
        grpc::ServerContext* /*context*/,
        const GetSoundGeneratorStateRequest* /*request*/,
        SoundGeneratorState* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto& chip = machine_.memory().sound_chip;

        // Always return chip-native ordering: Tone0, Tone1, Tone2, Noise
        // MOS channel mapping is a client-side concern

        // Tone channels (indices 0, 1, 2)
        for (int i = 0; i < 3; ++i) {
            auto tone = chip.get_tone_channel_state(static_cast<size_t>(i));
            auto* channel = response->add_channels();

            channel->set_channel_id(static_cast<uint32_t>(i));
            channel->set_channel_name("Tone" + std::to_string(i));
            channel->set_frequency_divider(tone.frequency);
            channel->set_counter(tone.counter);
            channel->set_output_bit(tone.output_bit);
            channel->set_volume(tone.volume);
            channel->set_frequency_hz(tone.frequency_hz);
        }

        // Noise channel (index 3)
        auto noise = chip.get_noise_channel_state();
        auto* noise_channel = response->add_channels();

        noise_channel->set_channel_id(3);
        noise_channel->set_channel_name("Noise");
        noise_channel->set_noise_rate(noise.rate_select);
        noise_channel->set_white_noise(noise.white_mode);
        noise_channel->set_lfsr_state(noise.lfsr);
        noise_channel->set_volume(noise.volume);
        noise_channel->set_frequency_hz(noise.rate_hz);

        // Latched register
        response->set_latched_register(chip.latched_register());

        return grpc::Status::OK;
    }

    grpc::Status GetTubeState(
        grpc::ServerContext* /*context*/,
        const GetTubeStateRequest* /*request*/,
        TubeState* response) override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto& tube = machine_.memory().tube_socket;

        if (!tube.enabled()) {
            response->set_enabled(false);
            return grpc::Status::OK;
        }

        // In-process mode: full state available from TubeUla.
        if (const auto* ula = tube.tube_ula()) {
            fill_tube_state_from_ula(*ula, response);
            return grpc::Status::OK;
        }

        // Shared memory mode: full state available from TubeHostPort.
        if (const auto* host_port = tube.tube_host_port()) {
            fill_tube_state_from_shared(*host_port, response);
            return grpc::Status::OK;
        }

        // Unknown backend: should not happen, but return empty enabled state.
        response->set_enabled(true);
        return grpc::Status::OK;
    }

private:
    MachineType& machine_;
    std::mutex mutex_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_DEVICE_INSPECTION_SERVICE_HPP
