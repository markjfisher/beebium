// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#pragma once

#include <beebium/serial/HostSerialPort.hpp>
#include <beebium/serial/SerialDevice.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace beebium::serial {

// Bridges the Serial ULA to a host HostSerialPort (a PTY master or an opened
// device path), exposing the SerialDataSource / SerialDataSink interfaces the
// ULA expects.
//
// Both directions run on dedicated I/O threads so the emulation thread never
// blocks on the OS port:
//   device -> Beeb: a reader thread does the blocking-style reads and pushes
//     received bytes into a mutex-protected queue, which the ULA drains on the
//     emulation thread.
//   Beeb -> device: add_byte() (emulation thread) only enqueues; a writer thread
//     drains the queue to the port. If a peer stops reading, the kernel buffer
//     fills and the writer stalls -- but only the writer thread, never the
//     emulation thread. The transmit queue is bounded: at the back-pressure mark
//     accepts_more() returns false, so the Serial ULA asserts the ACIA's /CTS
//     and the guest's transmit loop busy-waits (it stalls the emulated guest,
//     not the emulator host). A hard cap above that drops bytes only if the
//     guest also ignores /CTS, exactly as a real ACIA would on overrun.
//
// The writer thread is the sole writer, honouring the HostSerialPort
// single-writer threading contract.
class HostSerialEndpoint final : public beebium::SerialPortDevice {
public:
    // Transmit-queue bounds (bytes), mirroring RpcSerialEndpoint.
    static constexpr std::size_t kTxBackPressure = 4096;            // assert /CTS at/above
    static constexpr std::size_t kTxHardCap = kTxBackPressure + 64;  // absolute cap

    explicit HostSerialEndpoint(std::unique_ptr<HostSerialPort> port)
        : port_(std::move(port)) {
        if (port_ && port_->is_open()) {
            reader_ = std::thread([this] { reader_loop(); });
            writer_ = std::thread([this] { writer_loop(); });
        }
    }

    ~HostSerialEndpoint() override {
        stop_.store(true, std::memory_order_release);
        tx_cv_.notify_all();  // wake the writer if it is waiting for data
        if (port_) {
            port_->close();  // unblock a pending read() so the reader can exit
        }
        if (reader_.joinable()) {
            reader_.join();
        }
        if (writer_.joinable()) {
            writer_.join();
        }
    }

    HostSerialEndpoint(const HostSerialEndpoint&) = delete;
    HostSerialEndpoint& operator=(const HostSerialEndpoint&) = delete;

    // --- SerialDataSource (device -> Beeb), called on the emulation thread ---
    bool has_data() override {
        std::lock_guard<std::mutex> lock(rx_mutex_);
        return !rx_.empty();
    }

    uint8_t next_byte() override {
        std::lock_guard<std::mutex> lock(rx_mutex_);
        uint8_t value = rx_.front();
        rx_.pop_front();
        return value;
    }

    // --- SerialDataSink (Beeb -> device), called on the emulation thread ---
    // Enqueue only; the writer thread does the OS write. Never blocks.
    void add_byte(uint8_t value) override {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        if (tx_.size() >= kTxHardCap) {
            // Writer can't keep up AND the guest is ignoring /CTS (TDRE): a real
            // ACIA would lose data here too. Account it; never grow without bound.
            ++tx_dropped_;
            return;
        }
        tx_.push_back(value);
        tx_cv_.notify_one();
    }

    // Flow control (/CTS): not clear to send once the transmit queue reaches the
    // back-pressure mark, so the guest's transmit loop stalls until the writer
    // thread drains it to the port.
    bool accepts_more() override {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        return tx_.size() < kTxBackPressure;
    }

    // True while the underlying port is usable.
    bool is_open() const { return port_ && port_->is_open(); }

    // Diagnostics / tests: bytes queued for transmit, and bytes dropped at the
    // hard cap.
    std::size_t tx_pending() {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        return tx_.size();
    }
    std::size_t tx_dropped() {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        return tx_dropped_;
    }

private:
    void reader_loop() {
        std::array<std::uint8_t, 256> buf{};
        while (!stop_.load(std::memory_order_acquire)) {
            if (!port_->is_open()) {
                break;
            }
            ReadResult r = port_->read(std::span<std::uint8_t>(buf.data(), buf.size()));
            if (r.error) {
                break;
            }
            if (r.bytes > 0) {
                std::lock_guard<std::mutex> lock(rx_mutex_);
                for (std::size_t i = 0; i < r.bytes; ++i) {
                    rx_.push_back(buf[i]);
                }
            }
            // would_block: the read already waited ~100ms, so just loop.
        }
    }

    void writer_loop() {
        std::vector<std::uint8_t> batch;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(tx_mutex_);
                tx_cv_.wait(lock, [this] {
                    return stop_.load(std::memory_order_acquire) || !tx_.empty();
                });
                if (stop_.load(std::memory_order_acquire)) {
                    return;  // drop any pending bytes; the machine is tearing down
                }
                batch.assign(tx_.begin(), tx_.end());
                tx_.clear();
            }
            // Write the batch, retrying the remainder while the kernel buffer is
            // full (a partial write with no error == EAGAIN). New transmitted
            // bytes pile up in tx_ meanwhile, so accepts_more() asserts /CTS.
            std::size_t off = 0;
            while (off < batch.size() && !stop_.load(std::memory_order_acquire)) {
                if (!port_ || !port_->is_open()) {
                    return;
                }
                WriteResult w = port_->write(
                    std::span<const std::uint8_t>(batch.data() + off, batch.size() - off));
                if (w.error) {
                    return;  // port is broken; stop writing
                }
                off += w.bytes;
                if (off < batch.size()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
            batch.clear();
        }
    }

    std::unique_ptr<HostSerialPort> port_;
    std::thread reader_;
    std::thread writer_;
    std::atomic<bool> stop_{false};

    std::mutex rx_mutex_;
    std::deque<std::uint8_t> rx_;  // device -> Beeb

    std::mutex tx_mutex_;
    std::condition_variable tx_cv_;
    std::deque<std::uint8_t> tx_;  // Beeb -> device
    std::size_t tx_dropped_ = 0;
};

}  // namespace beebium::serial
