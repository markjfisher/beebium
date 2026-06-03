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
#include <beebium/serial/SerialBufferLimits.hpp>
#include <beebium/serial/SerialDevice.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
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
//
// Re-pointing (Extension UI): the device path / baud can be changed at runtime.
// request_reopen() (any thread) posts the new target; the emulation thread
// applies it on its next has_data() tick (process_pending_reopen) -- tearing
// down the I/O threads, building a fresh port via the SerialFactory, and
// restarting -- so the emulation thread stays the sole owner of port_. The UI
// reads state only through ui_snapshot() (under ui_mutex_); never the live
// fields directly -- racing the path string across threads corrupted the heap
// on Windows (the Piconet lesson, docs/discussion/grpc-windows-streaming-race.md).
class HostSerialEndpoint final : public beebium::SerialPortDevice {
public:
    // Builds a replacement HostSerialPort for a (path, baud) on reopen. When
    // null, request_reopen() is a no-op (the port handed in at construction is
    // the only one this endpoint will own).
    using SerialFactory =
        std::function<std::unique_ptr<HostSerialPort>(const std::string& path, int baud)>;

    struct Options {
        std::size_t tx_back_pressure = kDefaultTxBackPressure;
        SerialFactory factory;                      // null => reopen disabled
        std::function<void()> on_async_state_change;  // fired on hot-unplug / reopen
        std::string device_path;                    // for the UI snapshot
        int baud = 19200;                           // for the UI snapshot
        std::string mode = "device";                // "pty" | "device", for the UI
    };

    explicit HostSerialEndpoint(std::unique_ptr<HostSerialPort> port)
        : HostSerialEndpoint(std::move(port), Options{}) {}

    HostSerialEndpoint(std::unique_ptr<HostSerialPort> port, Options options)
        : port_(std::move(port)),
          tx_back_pressure_(options.tx_back_pressure),
          tx_hard_cap_(options.tx_back_pressure + kTxHardCapMargin),
          factory_(std::move(options.factory)),
          on_async_state_change_(std::move(options.on_async_state_change)),
          current_path_(std::move(options.device_path)),
          current_baud_(options.baud),
          current_mode_(std::move(options.mode)) {
        start_io_threads();
    }

    ~HostSerialEndpoint() override {
        stop_io_threads();
        delete pending_reopen_.exchange(nullptr, std::memory_order_acq_rel);
    }

    HostSerialEndpoint(const HostSerialEndpoint&) = delete;
    HostSerialEndpoint& operator=(const HostSerialEndpoint&) = delete;

    std::size_t tx_back_pressure() const { return tx_back_pressure_; }
    std::size_t tx_hard_cap() const { return tx_hard_cap_; }

    // --- SerialDataSource (device -> Beeb), called on the emulation thread ---
    bool has_data() override {
        process_pending_reopen();  // emulation-thread hook; cheap when idle
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
        if (tx_.size() >= tx_hard_cap_) {
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
        return tx_.size() < tx_back_pressure_;
    }

    // True while the underlying port is usable. For the emulation thread / tests;
    // the UI uses ui_snapshot() instead (cross-thread safe).
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

    // --- Runtime re-pointing (Extension UI) ---

    // Atomic snapshot of the state the UI renders. Built under ui_mutex_ and
    // returned by value, so the gRPC SubscribeView thread sees a consistent view
    // without holding the lock. The only supported cross-thread read of the racy
    // fields (port_, current_path_, current_baud_, open_error_).
    struct UiSnapshot {
        std::string device_path;
        int baud = 0;
        bool serial_open = false;
        std::string open_error_message;
        std::string mode;  // "pty" | "device"
    };
    UiSnapshot ui_snapshot() const {
        std::lock_guard<std::mutex> lock(ui_mutex_);
        return UiSnapshot{current_path_, current_baud_,
                          port_ && port_->is_open(), open_error_, current_mode_};
    }

    // Request the bridge re-point to a new device path + baud. Safe from any
    // thread; the close/open/restart runs on the emulation thread at the next
    // has_data() tick. Successive calls coalesce. No-op without a SerialFactory.
    void request_reopen(std::string path, int baud) {
        if (!factory_) {
            return;
        }
        auto* req = new ReopenRequest{std::move(path), baud};
        delete pending_reopen_.exchange(req, std::memory_order_acq_rel);
    }

private:
    struct ReopenRequest {
        std::string path;
        int baud;
    };

    void start_io_threads() {
        if (port_ && port_->is_open()) {
            stop_.store(false, std::memory_order_release);
            reader_ = std::thread([this] { reader_loop(); });
            writer_ = std::thread([this] { writer_loop(); });
        }
    }

    void stop_io_threads() {
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

    void notify_state_changed() {
        if (on_async_state_change_) {
            on_async_state_change_();
        }
    }

    // Emulation thread: apply a pending re-point. Tears down the I/O threads
    // (so no thread touches port_), swaps the port under ui_mutex_, restarts.
    void process_pending_reopen() {
        if (pending_reopen_.load(std::memory_order_acquire) == nullptr) {
            return;  // common case: nothing queued
        }
        ReopenRequest* req = pending_reopen_.exchange(nullptr, std::memory_order_acq_rel);
        if (!req) {
            return;
        }
        std::string path = std::move(req->path);
        int baud = req->baud;
        delete req;

        stop_io_threads();  // join reader + writer; close the old port

        std::unique_ptr<HostSerialPort> fresh = factory_ ? factory_(path, baud) : nullptr;
        const bool open = fresh && fresh->is_open();
        std::string error;
        if (!open) {
            error = fresh ? std::string(fresh->open_error())
                          : std::string("reopen unavailable");
        }
        {
            std::lock_guard<std::mutex> lock(ui_mutex_);
            port_ = std::move(fresh);
            current_path_ = std::move(path);
            current_baud_ = baud;
            open_error_ = std::move(error);
            // The factory always opens a real device; re-pointing away from a
            // startup pty therefore lands in device mode.
            current_mode_ = "device";
        }
        start_io_threads();   // resets stop_, starts threads iff the new port opened
        notify_state_changed();
    }

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
        // Exited for a reason other than a teardown/reopen (hot-unplug, read
        // error closing the port) -- tell the UI so it re-renders the offline
        // state. Suppressed during stop_io_threads (the deliberate path).
        if (!stop_.load(std::memory_order_acquire)) {
            notify_state_changed();
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
    const std::size_t tx_back_pressure_;
    const std::size_t tx_hard_cap_;

    // Re-pointing.
    SerialFactory factory_;
    std::function<void()> on_async_state_change_;
    std::atomic<ReopenRequest*> pending_reopen_{nullptr};

    // Guards the cross-thread read path (ui_snapshot on the gRPC thread) against
    // the emulation thread's port_ swap in process_pending_reopen. Held only for
    // the field swap / snapshot; the heavy work (thread teardown, port open,
    // thread restart) runs outside the lock.
    mutable std::mutex ui_mutex_;
    std::string current_path_;
    int current_baud_ = 0;
    std::string open_error_;
    std::string current_mode_;
};

}  // namespace beebium::serial
