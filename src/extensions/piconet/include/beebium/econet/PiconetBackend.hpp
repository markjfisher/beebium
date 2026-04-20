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

#pragma once

#include "beebium/econet/NetworkBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/econet/piconet/PiconetConfig.hpp"
#include "beebium/econet/piconet/SerialPort.hpp"

#include <moodycamel/readerwriterqueue.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace beebium {

// Network backend that bridges Beebium's emulated Econet to a real Econet
// network via a Piconet USB-CDC serial device. Runs in aun_mode -- the
// FourWayHandshake decorator must be active because Piconet's wire-level
// four-way handshake is opaque to the host (frames in/out are atomic).
//
// Threading model: a dedicated reader thread drains the SerialPort,
// accumulates bytes into a line buffer, parses complete event lines, and
// pushes the resulting NetworkFrames onto a lock-free ReaderWriterQueue.
// The emulation thread drains the queue via receive_frame(). All writes
// to the SerialPort happen on the emulation thread (send_frame and
// on_station_id_changed) per the threading contract documented in
// SerialPort.hpp -- no internal mutex.
class PiconetBackend : public NetworkBackend {
public:
    // Constructor opens the SerialPort, sends SET_STATION + SET_MODE LISTEN,
    // and starts the reader thread. Failures during startup mark the
    // backend disconnected (is_connected() returns false) but do not throw,
    // matching AunBackend's behaviour. The destructor signals shutdown,
    // closes the serial port (which unblocks the reader's timed read), and
    // joins the reader thread.
    //
    // on_async_state_change is invoked by the reader thread whenever
    // PiconetBackend's externally-observable state changes for reasons
    // other than a synchronous call from the emulation thread (today:
    // hot-unplug detection, serial read errors that close the port).
    // PiconetEconetTransportExtension wires this to PiconetUi::mark_dirty()
    // so the Extension UI framework knows to push a fresh View. If null,
    // the callback is simply not invoked. Must be thread-safe -- it runs
    // on the reader thread.
    PiconetBackend(piconet::PiconetConfig config,
                   std::unique_ptr<piconet::SerialPort> serial,
                   std::function<void()> on_async_state_change = nullptr);

    ~PiconetBackend() override;

    void send_frame(const NetworkFrame& frame) override;

    // Non-blocking. Returns the next NetworkFrame from the reader thread's
    // queue, or nullopt if empty.
    std::optional<NetworkFrame> receive_frame() override;

    // NetworkBackend interface: "is the BBC actually in two-way comms with
    // the wire?" -- requires both the USB-CDC link to the adapter to be
    // open AND the firmware to be in LISTEN mode. STOP-mode means the
    // Pico is silently dropping all traffic, so functionally we are not
    // connected. Symmetric with AunBackend's "did the user simulate
    // unplugging the cable" semantics; both transports' is_connected()
    // now answers the same user-meaningful question, which lets the
    // Network sidebar's Connection state row be transport-agnostic.
    bool is_connected() const override {
        return is_serial_open() &&
               current_mode_.load(std::memory_order_acquire) == piconet::Mode::Listen;
    }

    // USB-level connection: is the serial port open and the adapter
    // physically reachable? Independent of firmware mode. Used by
    // PiconetService.GetStatus.serial_open (reports physical-layer
    // health) and PiconetUi's Indicator (distinguishes "USB unplugged"
    // from "muted via STOP mode" -- both produce zero traffic but for
    // different reasons the user can act on differently).
    bool is_serial_open() const noexcept {
        return serial_ && serial_->is_open();
    }

    // EconetSocket calls this when the BBC's station latch changes; we
    // re-issue SET_STATION to keep the Piconet device in sync. Per the
    // threading contract this is invoked on the emulation thread, so it
    // shares the SerialPort write path with send_frame().
    void on_station_id_changed(std::uint8_t new_station_id) override;

    // Switch the firmware between SET_MODE STOP / LISTEN / MONITOR.
    // Caches the new mode so mode() can report it without round-tripping
    // the device. Per the SerialPort threading contract this must be
    // called from the emulation thread (or the test driver thread that
    // owns the write path); ExtensionUi::handle_event invocations are
    // serialised by the gRPC server, so PiconetUi calls this from the
    // gRPC handler thread -- safe because the gRPC handler is the only
    // other writer for tests, and in production the emulator never
    // touches set_mode().
    void set_mode(piconet::Mode mode);

    // Last mode set via set_mode() (or the constructor's initial LISTEN).
    // Atomic so PiconetUi::build_view, called on the gRPC server thread,
    // can read it safely while another thread might be calling set_mode.
    piconet::Mode mode() const noexcept {
        return current_mode_.load(std::memory_order_acquire);
    }

    // Accessor for diagnostics / gRPC introspection.
    const piconet::PiconetConfig& config() const { return config_; }

private:
    void reader_loop();

    piconet::PiconetConfig config_;
    std::unique_ptr<piconet::SerialPort> serial_;

    moodycamel::ReaderWriterQueue<NetworkFrame> rx_queue_{64};
    std::atomic<bool> shutdown_{false};
    std::atomic<piconet::Mode> current_mode_{piconet::Mode::Stop};
    std::function<void()> on_async_state_change_;
    std::thread reader_thread_;
};

}  // namespace beebium
