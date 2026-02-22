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

// test_econet_fileserver.cpp
//
// Integration tests for Econet communication with an external file server.
// These tests are discretionary: they SKIP unless the BEEBIUM_FILESERVER
// environment variable is set to point at a running AUN file server.
//
// Example:
//   BEEBIUM_FILESERVER=0.254:127.0.0.1:32768 BEEBIUM_LOCAL_PORT=10101 \
//     ./tests/test_econet_fileserver
//
// The file server can be:
//   - BeebEm running an Acorn Level 3 file server (AUNMODE 1)
//   - PiEconetBridge
//   - Real Econet hardware with an AUN bridge
//
// Station number defaults to 101 but can be overridden with BEEBIUM_STATION.
// Local AUN port must match the station's entry in the file server's Econet.cfg
// (e.g. port 10101 for station 101). Set via BEEBIUM_LOCAL_PORT.
// No disc controller is installed, so NFS is the active filing system.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/econet/AunBackend.hpp>
#include <beebium/econet/EconetSocket.hpp>
#include <beebium/econet/FourWayHandshake.hpp>
#include <beebium/econet/Mc6854.hpp>

#include <arpa/inet.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "test_keyboard_helpers.hpp"

using namespace beebium;
using namespace beebium::test;

namespace {

// --- Diagnostic backend decorator ---
// Wraps an AunBackend to count and log all packets sent/received.
class LoggingBackend : public NetworkBackend {
public:
    explicit LoggingBackend(std::unique_ptr<AunBackend> inner)
        : inner_(std::move(inner)) {}

    void send_frame(const NetworkFrame& frame) override {
        ++send_count;
        char buf[256];
        snprintf(buf, sizeof(buf),
            "  TX: type=%d port=%02X ctrl=%02X dest=%d.%d src=%d.%d data=%zu bytes",
            static_cast<int>(frame.type), frame.port, frame.control_byte,
            frame.dest_net, frame.dest_stn, frame.src_net, frame.src_stn,
            frame.data.size());
        log += buf;
        log += hex_dump(frame.data);
        log += "\n";
        inner_->send_frame(frame);
    }

    std::optional<NetworkFrame> receive_frame() override {
        auto result = inner_->receive_frame();
        if (result) {
            ++recv_count;
            char buf[256];
            snprintf(buf, sizeof(buf),
                "  RX: type=%d port=%02X ctrl=%02X dest=%d.%d src=%d.%d data=%zu bytes",
                static_cast<int>(result->type), result->port, result->control_byte,
                result->dest_net, result->dest_stn, result->src_net, result->src_stn,
                result->data.size());
            log += buf;
            log += hex_dump(result->data);
            log += "\n";
        }
        return result;
    }

    static std::string hex_dump(const std::vector<uint8_t>& data) {
        if (data.empty()) return "";
        std::string result = " [";
        char hex[4];
        for (size_t i = 0; i < data.size() && i < 64; ++i) {
            if (i > 0) result += " ";
            snprintf(hex, sizeof(hex), "%02X", data[i]);
            result += hex;
        }
        if (data.size() > 64) result += " ...";
        result += "]";
        return result;
    }

    bool is_connected() const override { return inner_->is_connected(); }

    uint16_t local_port() const { return inner_->local_port(); }
    void add_peer(uint8_t net, uint8_t stn, uint32_t ip, uint16_t port) {
        inner_->add_peer(net, stn, ip, port);
    }

    int send_count = 0;
    int recv_count = 0;
    std::string log;

private:
    std::unique_ptr<AunBackend> inner_;
};

// --- ROM loading ---

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open ROM: " + filepath.string());
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool base_roms_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom");
}

bool nfs_rom_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-nfs_3_34.rom");
}

// --- File server configuration from environment ---

struct FileserverConfig {
    uint8_t net = 0;
    uint8_t stn = 254;
    uint32_t ip_addr = 0;      // network byte order
    uint16_t port = 32768;     // host byte order
    uint8_t station = 101;     // our station number
    uint16_t local_port = 0;   // our AUN port (0 = ephemeral)
    bool valid = false;
};

// Parse BEEBIUM_FILESERVER=net.stn:ip:port (e.g. "0.254:127.0.0.1:32768")
FileserverConfig parse_fileserver_env() {
    FileserverConfig config;

    const char* env = std::getenv("BEEBIUM_FILESERVER");
    if (!env) return config;

    std::string str(env);

    // Parse net.stn
    auto colon1 = str.find(':');
    if (colon1 == std::string::npos) return config;
    std::string addr_part = str.substr(0, colon1);
    auto dot = addr_part.find('.');
    if (dot == std::string::npos) return config;
    config.net = static_cast<uint8_t>(std::stoi(addr_part.substr(0, dot)));
    config.stn = static_cast<uint8_t>(std::stoi(addr_part.substr(dot + 1)));

    // Parse ip:port — IP may contain dots, so find the last colon
    std::string rest = str.substr(colon1 + 1);
    auto last_colon = rest.rfind(':');
    std::string ip_str;
    if (last_colon != std::string::npos) {
        ip_str = rest.substr(0, last_colon);
        config.port = static_cast<uint16_t>(std::stoi(rest.substr(last_colon + 1)));
    } else {
        ip_str = rest;
    }

    if (::inet_pton(AF_INET, ip_str.c_str(), &config.ip_addr) != 1) return config;

    // Optional station number override
    const char* stn_env = std::getenv("BEEBIUM_STATION");
    if (stn_env) {
        config.station = static_cast<uint8_t>(std::stoi(stn_env));
    }

    // Local AUN port — must match the station's entry in the file server's
    // Econet.cfg so that FindHost() can identify our packets.
    const char* port_env = std::getenv("BEEBIUM_LOCAL_PORT");
    if (port_env) {
        config.local_port = static_cast<uint16_t>(std::stoi(port_env));
    }

    config.valid = true;
    return config;
}

// --- Screen memory helpers ---

bool screen_contains(ModelB& machine, const std::string& text) {
    for (uint16_t addr = 0x7C00; addr <= 0x7FFF - text.size(); ++addr) {
        bool match = true;
        for (size_t i = 0; i < text.size(); ++i) {
            if (machine.state().memory.read(addr + static_cast<uint16_t>(i)) !=
                static_cast<uint8_t>(text[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

std::string dump_screen(ModelB& machine, int rows = 25) {
    std::string result;
    for (int row = 0; row < rows; ++row) {
        result += "Row " + std::to_string(row) + ": [";
        for (int col = 0; col < 40; ++col) {
            uint8_t ch = machine.state().memory.read(
                0x7C00 + static_cast<uint16_t>(row * 40 + col));
            if (ch >= 0x20 && ch < 0x7F) {
                result += static_cast<char>(ch);
            } else {
                result += '.';
            }
        }
        result += "]\n";
    }
    return result;
}

// --- ADLC / handshake state dump for debugging ---

const char* stage_name(FourWayHandshake::Stage stage) {
    switch (stage) {
        case FourWayHandshake::Stage::Idle:              return "Idle";
        case FourWayHandshake::Stage::ScoutSent:         return "ScoutSent";
        case FourWayHandshake::Stage::ScoutAckReceived:  return "ScoutAckReceived";
        case FourWayHandshake::Stage::DataSent:          return "DataSent";
        case FourWayHandshake::Stage::ScoutReceived:     return "ScoutReceived";
        case FourWayHandshake::Stage::ScoutAckSent:      return "ScoutAckSent";
        case FourWayHandshake::Stage::DataReceived:      return "DataReceived";
        case FourWayHandshake::Stage::ImmediateSent:     return "ImmediateSent";
        case FourWayHandshake::Stage::ImmediateReceived: return "ImmediateReceived";
        case FourWayHandshake::Stage::WaitForIdle:       return "WaitForIdle";
        default:                                          return "Unknown";
    }
}

std::string dump_adlc_state(ModelB& machine) {
    auto& socket = machine.state().memory.econet_socket;
    auto* adlc = socket.adlc();
    if (!adlc) return "(no ADLC)\n";

    std::string result;
    char buf[256];

    snprintf(buf, sizeof(buf), "CR1=%02X CR2=%02X CR3=%02X CR4=%02X\n",
             adlc->cr1(), adlc->cr2(), adlc->cr3(), adlc->cr4());
    result += buf;

    snprintf(buf, sizeof(buf), "SR1=%02X SR2=%02X\n", adlc->sr1(), adlc->sr2());
    result += buf;

    snprintf(buf, sizeof(buf), "  SR1: RDA=%d S2RQ=%d FD=%d CTS=%d TXU=%d TDRA=%d IRQ=%d\n",
             (adlc->sr1() & Mc6854::SR1_RDA) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_S2RQ) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_FD) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_CTS) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_TXU) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_TDRA) ? 1 : 0,
             (adlc->sr1() & 0x80) ? 1 : 0);
    result += buf;

    snprintf(buf, sizeof(buf), "  SR2: AP=%d FV=%d INACTIVE=%d ABT=%d ERR=%d DCD=%d OVRN=%d RDA=%d\n",
             (adlc->sr2() & Mc6854::SR2_AP) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_FV) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_INACTIVE) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_ABT) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_ERR) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_DCD) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_OVRN) ? 1 : 0,
             (adlc->sr2() & 0x80) ? 1 : 0);
    result += buf;

    snprintf(buf, sizeof(buf), "TX FIFO: empty=%d full=%d  RX FIFO: empty=%d full=%d\n",
             adlc->tx_fifo_empty() ? 1 : 0, adlc->tx_fifo_full() ? 1 : 0,
             adlc->rx_fifo_empty() ? 1 : 0, adlc->rx_fifo_full() ? 1 : 0);
    result += buf;

    snprintf(buf, sizeof(buf), "IRQ output: %d  NMI enable: %d  NMI pending: %d\n",
             adlc->irq_output() ? 1 : 0,
             socket.nmi_enable_ff() ? 1 : 0,
             socket.nmi_pending() ? 1 : 0);
    result += buf;

    auto* hs = socket.handshake();
    if (hs) {
        snprintf(buf, sizeof(buf), "Handshake: stage=%s flag_fill=%d\n",
                 stage_name(hs->stage()), hs->flag_fill_active() ? 1 : 0);
        result += buf;
    }

    return result;
}

} // namespace

// =============================================================================
// File Server Integration Tests (discretionary — need BEEBIUM_FILESERVER)
// =============================================================================

TEST_CASE("Econet *. command to file server", "[econet][fileserver]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    auto fs_config = parse_fileserver_env();
    if (!fs_config.valid) SKIP("BEEBIUM_FILESERVER not set "
        "(e.g. BEEBIUM_FILESERVER=0.254:127.0.0.1:32768 BEEBIUM_LOCAL_PORT=10101)");

    // Set up Model B with MOS + BASIC + NFS ROM, no disc controller.
    // Without DFS, NFS is the active filing system and *. goes over the network.
    ModelB machine;
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    auto nfs = load_rom(rom_dirpath / "acorn-nfs_3_34.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().load_sideways_rom(10, nfs.data(), nfs.size());
    machine.memory().enable_video_output();

    // Create AUN backend wrapped in logging decorator for diagnostics.
    // The local port must match the station's entry in the file server's
    // Econet.cfg so that BeebEm's FindHost() can identify our packets.
    auto aun = std::make_unique<AunBackend>(
        0, fs_config.station, fs_config.local_port);
    REQUIRE(aun->is_connected());
    auto logging = std::make_unique<LoggingBackend>(std::move(aun));
    auto* log_ptr = logging.get();
    INFO("Bound to UDP port " << log_ptr->local_port());

    // Add file server as peer
    log_ptr->add_peer(fs_config.net, fs_config.stn, fs_config.ip_addr, fs_config.port);

    // Enable Econet with AUN mode (FourWayHandshake decorator)
    machine.state().memory.econet_socket.enable(
        fs_config.station, std::move(logging), true);

    // Set up video rendering (required for keyboard typing helpers)
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot the machine
    machine.reset();
    for (uint64_t i = 0; i < 4'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Verify boot completed
    INFO("Screen after boot:\n" << dump_screen(machine, 8));
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "Econet Station"));

    INFO("ADLC state after boot:\n" << dump_adlc_state(machine));

    // Select NFS as the active filing system. On a cold boot with MOS 1.20,
    // the default filing system is the Cassette Filing System, even when NFS
    // is installed. Typing *NET selects the Network Filing System.
    type_string_with_shift(machine, renderer, "*NET\r", 100000);

    // Give time for *NET to complete
    for (uint64_t i = 0; i < 2'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    INFO("Screen after *NET:\n" << dump_screen(machine, 12));

    // Log out any stale session from a previous test run. The file server
    // remembers station numbers, so without *BYE we may still be logged in.
    type_string_with_shift(machine, renderer, "*BYE\r", 100000);
    for (uint64_t i = 0; i < 10'000'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }

    // Type *. (without RETURN yet) using normal helpers
    type_string_with_shift(machine, renderer, "*.", 100000);

    // Now set up tracking BEFORE pressing RETURN, so we capture all NFS
    // activity from the moment the command is submitted.
    auto* adlc = machine.state().memory.econet_socket.adlc();
    auto* hs_ptr = machine.state().memory.econet_socket.handshake();
    uint8_t prev_cr1 = adlc ? adlc->cr1() : 0;
    uint8_t prev_cr2 = adlc ? adlc->cr2() : 0;
    uint8_t prev_sr1 = adlc ? adlc->sr1() : 0;
    uint8_t prev_sr2 = adlc ? adlc->sr2() : 0;
    FourWayHandshake::Stage prev_hs_stage = hs_ptr ? hs_ptr->stage()
                                                    : FourWayHandshake::Stage::Idle;
    int change_count = 0;
    bool entered_nfs = false;
    uint16_t nfs_pc_min = 0xFFFF;
    uint16_t nfs_pc_max = 0x0000;
    bool entered_tx_routine = false;
    uint64_t nfs_cycle_count = 0;

    // Accumulate trace into a string so it survives assertion consumption.
    // UNSCOPED_INFO messages are consumed by the next assertion (pass or fail)
    // and only displayed on failure. Using a scoped string with INFO ensures
    // the trace is visible on ALL subsequent assertion failures.
    std::string trace_log;

    // Dump NFS workspace memory at key moments for debugging.
    // $0D4A = workspace flags: bit0 = data frame expected, bit6 = completion
    // $0D3D-$0D46 = scout buffer (populated during scout reception)
    // $0D20-$0D27 = TX buffer area (source/dest addresses for verification)
    // $00A0-$00A9 = page zero workspace (RXCB pointer, buffer pointers)
    auto dump_workspace = [&](uint64_t cycle_offset, const char* label) {
        char buf[1024];
        auto& mem = machine.state().memory;

        uint8_t flags_0d4a = mem.read(0x0D4A);
        snprintf(buf, sizeof(buf),
            "  === WORKSPACE DUMP [%s] cycle=%llu ===\n"
            "  $0D4A (flags): %02X (bit0=%d bit1=%d bit6=%d bit7=%d)\n",
            label, cycle_offset, flags_0d4a,
            flags_0d4a & 1, (flags_0d4a >> 1) & 1,
            (flags_0d4a >> 6) & 1, (flags_0d4a >> 7) & 1);
        trace_log += buf;

        // Scout buffer at $0D3D (filled during scout RX)
        trace_log += "  $0D3D scout:";
        for (int i = 0; i < 10; ++i) {
            snprintf(buf, sizeof(buf), " %02X", mem.read(0x0D3D + i));
            trace_log += buf;
        }
        trace_log += "\n";

        // TX buffer at $0D20 (addresses for frame matching)
        trace_log += "  $0D20 txbuf:";
        for (int i = 0; i < 10; ++i) {
            snprintf(buf, sizeof(buf), " %02X", mem.read(0x0D20 + i));
            trace_log += buf;
        }
        trace_log += "\n";

        // NMI jump target at $0D0C-$0D0D
        snprintf(buf, sizeof(buf),
            "  NMI target: $%02X%02X\n",
            mem.read(0x0D0D), mem.read(0x0D0C));
        trace_log += buf;

        // Page zero workspace $A0-$A9 (RXCB pointer, buffer pointers)
        trace_log += "  $00A0 pagez:";
        for (int i = 0; i < 10; ++i) {
            snprintf(buf, sizeof(buf), " %02X", mem.read(0x00A0 + i));
            trace_log += buf;
        }
        trace_log += "\n";

        // Workspace at $0D38-$0D4F (VAR area)
        trace_log += "  $0D38 var:";
        for (int i = 0; i < 24; ++i) {
            snprintf(buf, sizeof(buf), " %02X", mem.read(0x0D38 + i));
            trace_log += buf;
        }
        trace_log += "\n";

        // $0D52 and $0D66 — additional status
        snprintf(buf, sizeof(buf),
            "  $0D52=%02X $0D66=%02X\n",
            mem.read(0x0D52), mem.read(0x0D66));
        trace_log += buf;
    };

    // Lambda for the instrumented step loop
    auto tracked_step = [&](uint64_t cycle_offset) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }

        uint16_t pc = machine.state().cpu.pc.w;
        uint8_t romsel = machine.state().memory.read(0xF4);

        if (pc >= 0x8000 && pc < 0xC000 && romsel == 10) {
            entered_nfs = true;
            if (pc < nfs_pc_min) nfs_pc_min = pc;
            if (pc > nfs_pc_max) nfs_pc_max = pc;
            ++nfs_cycle_count;
            if (pc >= 0x9C40 && pc <= 0x9CC0) {
                entered_tx_routine = true;
            }
        }

        if (adlc && change_count < 2000) {
            uint8_t cr1 = adlc->cr1();
            uint8_t cr2 = adlc->cr2();
            uint8_t sr1 = adlc->sr1();
            uint8_t sr2 = adlc->sr2();
            auto hs_stage = hs_ptr ? hs_ptr->stage() : FourWayHandshake::Stage::Idle;

            if (cr1 != prev_cr1 || cr2 != prev_cr2 || sr1 != prev_sr1
                || sr2 != prev_sr2 || hs_stage != prev_hs_stage) {
                char buf[512];
                snprintf(buf, sizeof(buf),
                    "  cycle=%llu PC=%04X CR1=%02X CR2=%02X SR1=%02X SR2=%02X "
                    "IRQ=%d NMI_en=%d HS=%s rxbuf=%zu/%zu fv=%d ovrn=%d pse=%d\n",
                    cycle_offset, pc, cr1, cr2, sr1, sr2,
                    adlc->irq_output() ? 1 : 0,
                    machine.state().memory.econet_socket.nmi_enable_ff() ? 1 : 0,
                    hs_ptr ? stage_name(hs_ptr->stage()) : "n/a",
                    adlc->rx_buffer_index(), adlc->rx_frame_buffer_size(),
                    adlc->fv_stored() ? 1 : 0, adlc->ovrn_stored() ? 1 : 0,
                    adlc->pse_level());
                trace_log += buf;

                // Dump workspace at handshake transitions
                if (hs_stage != prev_hs_stage) {
                    dump_workspace(cycle_offset, stage_name(hs_stage));
                }

                prev_cr1 = cr1; prev_cr2 = cr2;
                prev_sr1 = sr1; prev_sr2 = sr2;
                prev_hs_stage = hs_stage;
                ++change_count;
            }
        }
    };

    // Press RETURN with tracking active
    press_key(machine, '\r');
    for (uint64_t i = 0; i < 50000; ++i) tracked_step(i);
    release_key(machine, '\r');
    for (uint64_t i = 50000; i < 100000; ++i) tracked_step(i);

    // Run for enough cycles to allow the network operation to complete or timeout.
    // 40M cycles is ~20 seconds of BBC time.
    for (uint64_t i = 100000; i < 40'100'000; ++i) {
        tracked_step(i);
    }

    if (change_count >= 2000) {
        trace_log += "(state changes truncated at 2000)\n";
    }

    {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "Total state changes: %d\n"
            "NFS ROM: entered=%d pc_range=$%04X-$%04X cycles=%llu tx_routine=%d\n",
            change_count,
            entered_nfs ? 1 : 0, nfs_pc_min, nfs_pc_max,
            nfs_cycle_count, entered_tx_routine ? 1 : 0);
        trace_log += buf;
    }

    // Write trace to a temp file so it survives Catch2 output truncation
    {
        std::ofstream trace_file("/tmp/beebium_econet_trace.log");
        trace_file << "ADLC state change trace:\n" << trace_log;
        trace_file << "\nScreen after *. command:\n" << dump_screen(machine);
        trace_file << "\nADLC state after *. command:\n" << dump_adlc_state(machine);
        trace_file << "\nAUN packet log (send=" << log_ptr->send_count
                   << " recv=" << log_ptr->recv_count << "):\n" << log_ptr->log;
    }

    // Use INFO (scoped) so the trace survives all subsequent assertions
    INFO("ADLC state change trace:\n" << trace_log);
    INFO("Screen after *. command:\n" << dump_screen(machine));
    INFO("ADLC state after *. command:\n" << dump_adlc_state(machine));
    INFO("AUN packet log (send=" << log_ptr->send_count
         << " recv=" << log_ptr->recv_count << "):\n" << log_ptr->log);
    INFO("Full trace written to /tmp/beebium_econet_trace.log");

    bool line_jammed   = screen_contains(machine, "Line Jammed");
    bool who_are_you   = screen_contains(machine, "Who are you");
    bool no_reply      = screen_contains(machine, "No reply");
    bool not_listening = screen_contains(machine, "Not listening");
    bool net_error     = screen_contains(machine, "Net Error");

    INFO("Outcomes: LineJammed=" << line_jammed
         << " WhoAreYou=" << who_are_you
         << " NoReply=" << no_reply
         << " NotListening=" << not_listening
         << " NetError=" << net_error);

    CHECK_FALSE(line_jammed);
    // Temporarily require who_are_you to force diagnostic INFO output
    CHECK(who_are_you);
}

TEST_CASE("Econet *I AM SYST then *. lists directory", "[econet][fileserver]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    auto fs_config = parse_fileserver_env();
    if (!fs_config.valid) SKIP("BEEBIUM_FILESERVER not set "
        "(e.g. BEEBIUM_FILESERVER=0.254:127.0.0.1:32768 BEEBIUM_LOCAL_PORT=10101)");

    // Set up Model B with MOS + BASIC + NFS ROM, no disc controller.
    ModelB machine;
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    auto nfs = load_rom(rom_dirpath / "acorn-nfs_3_34.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().load_sideways_rom(10, nfs.data(), nfs.size());
    machine.memory().enable_video_output();

    // Create AUN backend wrapped in logging decorator
    auto aun = std::make_unique<AunBackend>(
        0, fs_config.station, fs_config.local_port);
    REQUIRE(aun->is_connected());
    auto logging = std::make_unique<LoggingBackend>(std::move(aun));
    auto* log_ptr = logging.get();
    INFO("Bound to UDP port " << log_ptr->local_port());

    log_ptr->add_peer(fs_config.net, fs_config.stn, fs_config.ip_addr, fs_config.port);

    machine.state().memory.econet_socket.enable(
        fs_config.station, std::move(logging), true);

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Helper: run N cycles, processing video output
    auto run_cycles = [&](uint64_t count) {
        for (uint64_t i = 0; i < count; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    };

    // Boot the machine
    machine.reset();
    run_cycles(4'000'000);

    INFO("Screen after boot:\n" << dump_screen(machine, 8));
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "Econet Station"));

    // *NET — select the Network Filing System
    type_string_with_shift(machine, renderer, "*NET\r", 100000);
    run_cycles(2'000'000);
    INFO("Screen after *NET:\n" << dump_screen(machine, 12));

    // Log out any stale session from a previous test run.
    type_string_with_shift(machine, renderer, "*BYE\r", 100000);
    run_cycles(10'000'000);

    // *I AM SYST — log in as the SYST user
    type_string_with_shift(machine, renderer, "*I AM SYST\r", 100000);
    run_cycles(10'000'000);
    INFO("Screen after *I AM SYST:\n" << dump_screen(machine, 16));

    // *. — catalogue the current directory
    type_string_with_shift(machine, renderer, "*.\r", 100000);
    run_cycles(20'000'000);

    INFO("Screen after *.:\n" << dump_screen(machine));
    INFO("ADLC state:\n" << dump_adlc_state(machine));
    INFO("AUN packet log (send=" << log_ptr->send_count
         << " recv=" << log_ptr->recv_count << "):\n" << log_ptr->log);

    bool line_jammed   = screen_contains(machine, "Line Jammed");
    bool who_are_you   = screen_contains(machine, "Who are you");
    bool no_reply      = screen_contains(machine, "No reply");
    bool not_listening = screen_contains(machine, "Not listening");
    bool net_error     = screen_contains(machine, "Net Error");
    bool library        = screen_contains(machine, "Library");

    INFO("Outcomes: LineJammed=" << line_jammed
         << " WhoAreYou=" << who_are_you
         << " NoReply=" << no_reply
         << " NotListening=" << not_listening
         << " NetError=" << net_error
         << " Library=" << library);

    CHECK_FALSE(line_jammed);
    CHECK_FALSE(who_are_you);
    CHECK_FALSE(no_reply);
    CHECK_FALSE(not_listening);
    CHECK_FALSE(net_error);
    CHECK(library);

    // Log out so the file server forgets this station.
    type_string_with_shift(machine, renderer, "*BYE\r", 100000);
    run_cycles(10'000'000);
}

TEST_CASE("Econet *DATE runs library program from file server", "[econet][fileserver]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    auto fs_config = parse_fileserver_env();
    if (!fs_config.valid) SKIP("BEEBIUM_FILESERVER not set "
        "(e.g. BEEBIUM_FILESERVER=0.254:127.0.0.1:32768 BEEBIUM_LOCAL_PORT=10101)");

    // Set up Model B with MOS + BASIC + NFS ROM, no disc controller.
    ModelB machine;
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    auto nfs = load_rom(rom_dirpath / "acorn-nfs_3_34.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().load_sideways_rom(10, nfs.data(), nfs.size());
    machine.memory().enable_video_output();

    // Create AUN backend wrapped in logging decorator
    auto aun = std::make_unique<AunBackend>(
        0, fs_config.station, fs_config.local_port);
    REQUIRE(aun->is_connected());
    auto logging = std::make_unique<LoggingBackend>(std::move(aun));
    auto* log_ptr = logging.get();
    INFO("Bound to UDP port " << log_ptr->local_port());

    log_ptr->add_peer(fs_config.net, fs_config.stn, fs_config.ip_addr, fs_config.port);

    machine.state().memory.econet_socket.enable(
        fs_config.station, std::move(logging), true);

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Helper: run N cycles, processing video output
    auto run_cycles = [&](uint64_t count) {
        for (uint64_t i = 0; i < count; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    };

    // Boot the machine
    machine.reset();
    run_cycles(4'000'000);

    INFO("Screen after boot:\n" << dump_screen(machine, 8));
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "Econet Station"));

    // *NET — select the Network Filing System
    type_string_with_shift(machine, renderer, "*NET\r", 100000);
    run_cycles(2'000'000);
    INFO("Screen after *NET:\n" << dump_screen(machine, 12));

    // Log out any stale session from a previous test run.
    type_string_with_shift(machine, renderer, "*BYE\r", 100000);
    run_cycles(10'000'000);

    // *I AM SYST — log in as the privileged user
    type_string_with_shift(machine, renderer, "*I AM SYST\r", 100000);
    run_cycles(10'000'000);
    INFO("Screen after *I AM SYST:\n" << dump_screen(machine, 16));

    // *DATE — run the DATE utility from the Library directory.
    // The file server loads and executes the program, which prints
    // "Today is ..." with the server's date.
    type_string_with_shift(machine, renderer, "*DATE\r", 100000);
    run_cycles(20'000'000);

    INFO("Screen after *DATE:\n" << dump_screen(machine));
    INFO("ADLC state:\n" << dump_adlc_state(machine));
    INFO("AUN packet log (send=" << log_ptr->send_count
         << " recv=" << log_ptr->recv_count << "):\n" << log_ptr->log);

    bool line_jammed   = screen_contains(machine, "Line Jammed");
    bool no_reply      = screen_contains(machine, "No reply");
    bool not_listening = screen_contains(machine, "Not listening");
    bool net_error     = screen_contains(machine, "Net Error");
    bool bad_command   = screen_contains(machine, "Bad command");
    bool today         = screen_contains(machine, "Today");

    INFO("Outcomes: LineJammed=" << line_jammed
         << " NoReply=" << no_reply
         << " NotListening=" << not_listening
         << " NetError=" << net_error
         << " BadCommand=" << bad_command
         << " Today=" << today);

    CHECK_FALSE(line_jammed);
    CHECK_FALSE(no_reply);
    CHECK_FALSE(not_listening);
    CHECK_FALSE(net_error);
    CHECK_FALSE(bad_command);
    CHECK(today);

    // Log out so the file server forgets this station.
    type_string_with_shift(machine, renderer, "*BYE\r", 100000);
    run_cycles(10'000'000);
}
