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

#include <beebium/tube/ParasiteRunner.hpp>

#include <algorithm>
#include <chrono>

namespace beebium {

ParasiteRunner::ParasiteRunner(TubeShared* shared, std::span<const uint8_t, 2048> rom)
    : shared_(shared)
    , tube_port_(shared)
    , memory_(tube_port_, rom)
    , cpu_(memory_, tube_port_)
{
    std::copy(rom.begin(), rom.end(), rom_.begin());
}

void ParasiteRunner::reset() {
    cpu_.reset();
}

void ParasiteRunner::run(uint64_t cycles) {
    uint64_t remaining = cycles;

    while (remaining > 0) {
        // Check pause state
        if (!wait_if_paused())
            return;  // Shutdown during pause

        // Check lifecycle mailbox (may reset cycle_count)
        if (!poll_mailbox())
            return;  // Shutdown or freeze

        // Execute a batch of cycles (up to the next mailbox poll or remaining)
        uint64_t batch = std::min(remaining, mailbox_poll_interval);
        uint64_t batch_end = cpu_.cycle_count() + batch;
        while (cpu_.cycle_count() < batch_end) {
            cpu_.tick();
        }
        remaining -= batch;
    }
}

uint64_t ParasiteRunner::step_instruction() {
    return cpu_.step_instruction();
}

void ParasiteRunner::pause() {
    paused_.store(true, std::memory_order_release);
}

void ParasiteRunner::resume() {
    paused_.store(false, std::memory_order_release);
    pause_cv_.notify_all();
}

void ParasiteRunner::request_shutdown() {
    shutdown_requested_.store(true, std::memory_order_release);
    pause_cv_.notify_all();
}

bool ParasiteRunner::poll_mailbox() {
    auto cmd = static_cast<TubeLifecycleCommand>(
        shared_->host_command.load(std::memory_order_acquire));

    switch (cmd) {
    case TubeLifecycleCommand::None:
        return true;

    case TubeLifecycleCommand::Reset:
        // Clear the command so it doesn't re-trigger after reset.
        shared_->host_command.store(
            static_cast<uint8_t>(TubeLifecycleCommand::None),
            std::memory_order_release);
        reset();
        shared_->parasite_ack.store(
            static_cast<uint8_t>(TubeLifecycleAck::ResetDone),
            std::memory_order_release);
        return true;  // Continue execution after reset

    case TubeLifecycleCommand::Freeze:
        shared_->parasite_ack.store(
            static_cast<uint8_t>(TubeLifecycleAck::Frozen),
            std::memory_order_release);
        return wait_while_frozen();

    case TubeLifecycleCommand::Shutdown:
        shutdown_requested_.store(true, std::memory_order_release);
        shared_->parasite_ack.store(
            static_cast<uint8_t>(TubeLifecycleAck::Exiting),
            std::memory_order_release);
        return false;
    }

    return true;  // Unknown command -- ignore
}

bool ParasiteRunner::wait_if_paused() {
    if (paused_.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(pause_mutex_);
        pause_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return !paused_.load(std::memory_order_acquire)
                || shutdown_requested_.load(std::memory_order_acquire);
        });
    }
    return !shutdown_requested_.load(std::memory_order_acquire);
}

bool ParasiteRunner::wait_while_frozen() {
    // Block until the host changes the command (e.g. to None, Reset, or Shutdown).
    while (!shutdown_requested_.load(std::memory_order_acquire)) {
        auto cmd = static_cast<TubeLifecycleCommand>(
            shared_->host_command.load(std::memory_order_acquire));
        if (cmd != TubeLifecycleCommand::Freeze)
            break;

        std::unique_lock<std::mutex> lock(pause_mutex_);
        pause_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return shutdown_requested_.load(std::memory_order_acquire)
                || static_cast<TubeLifecycleCommand>(
                       shared_->host_command.load(std::memory_order_acquire))
                   != TubeLifecycleCommand::Freeze;
        });
    }
    return !shutdown_requested_.load(std::memory_order_acquire);
}

}  // namespace beebium
