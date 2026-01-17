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

#ifndef BEEBIUM_SERVICE_DEBUGGER_SERVICE_HPP
#define BEEBIUM_SERVICE_DEBUGGER_SERVICE_HPP

#include "debugger.grpc.pb.h"
#include "beebium/MemoryRegion.hpp"
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <vector>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <concepts>

namespace beebium::service {

// Concept to detect if a memory type has PC-aware access methods
template<typename T>
concept HasPcAwareMemory = requires(T& m, uint16_t addr, uint16_t pc, uint8_t val) {
    { m.read_with_pc(addr, pc) } -> std::same_as<uint8_t>;
    { m.write_with_pc(addr, val, pc) } -> std::same_as<void>;
};

// Helper to read with PC context when available, otherwise use regular read
template<typename Machine>
uint8_t read_with_optional_pc(Machine& machine, uint16_t addr, bool has_pc, uint16_t pc) {
    if constexpr (HasPcAwareMemory<decltype(machine.memory())>) {
        if (has_pc) {
            return machine.memory().read_with_pc(addr, pc);
        }
    }
    return machine.read(addr);
}

// Helper to peek with PC context when available, otherwise use regular peek
template<typename Machine>
uint8_t peek_with_optional_pc(Machine& machine, uint16_t addr, bool has_pc, uint16_t pc) {
    if constexpr (HasPcAwareMemory<decltype(machine.memory())>) {
        if (has_pc) {
            // For peek with PC, we use the PC-aware read (side-effect-free routing)
            return machine.memory().read_with_pc(addr, pc);
        }
    }
    return machine.peek(addr);
}

// Helper to write with PC context when available, otherwise use regular write
template<typename Machine>
void write_with_optional_pc(Machine& machine, uint16_t addr, uint8_t val, bool has_pc, uint16_t pc) {
    if constexpr (HasPcAwareMemory<decltype(machine.memory())>) {
        if (has_pc) {
            machine.memory().write_with_pc(addr, val, pc);
            return;
        }
    }
    machine.write(addr, val);
}

/// Internal breakpoint representation
struct BreakpointEntry {
    uint32_t id;
    uint32_t address;
};

/// gRPC service implementation for DebuggerControl
template<typename MachineType>
class DebuggerControlServiceImpl final : public DebuggerControl::Service {
public:
    explicit DebuggerControlServiceImpl(MachineType& machine);
    ~DebuggerControlServiceImpl() override = default;

    // Non-copyable
    DebuggerControlServiceImpl(const DebuggerControlServiceImpl&) = delete;
    DebuggerControlServiceImpl& operator=(const DebuggerControlServiceImpl&) = delete;

    // Execution control
    grpc::Status GetState(
        grpc::ServerContext* context,
        const Empty* request,
        ExecutionState* response) override;

    grpc::Status Run(
        grpc::ServerContext* context,
        const Empty* request,
        RunResponse* response) override;

    grpc::Status Stop(
        grpc::ServerContext* context,
        const Empty* request,
        StopResponse* response) override;

    grpc::Status Reset(
        grpc::ServerContext* context,
        const Empty* request,
        ResetResponse* response) override;

    grpc::Status StepInstruction(
        grpc::ServerContext* context,
        const StepRequest* request,
        StepResponse* response) override;

    grpc::Status StepCycle(
        grpc::ServerContext* context,
        const StepRequest* request,
        StepResponse* response) override;

    // Memory access
    grpc::Status ReadMemory(
        grpc::ServerContext* context,
        const ReadMemoryRequest* request,
        ReadMemoryResponse* response) override;

    grpc::Status WriteMemory(
        grpc::ServerContext* context,
        const WriteMemoryRequest* request,
        WriteMemoryResponse* response) override;

    grpc::Status PeekMemory(
        grpc::ServerContext* context,
        const PeekMemoryRequest* request,
        PeekMemoryResponse* response) override;

    // Memory region access
    grpc::Status GetMemoryRegions(
        grpc::ServerContext* context,
        const GetMemoryRegionsRequest* request,
        GetMemoryRegionsResponse* response) override;

    grpc::Status PeekRegion(
        grpc::ServerContext* context,
        const RegionAccessRequest* request,
        RegionAccessResponse* response) override;

    grpc::Status ReadRegion(
        grpc::ServerContext* context,
        const RegionAccessRequest* request,
        RegionAccessResponse* response) override;

    grpc::Status WriteRegion(
        grpc::ServerContext* context,
        const WriteRegionRequest* request,
        WriteRegionResponse* response) override;

    // Breakpoints
    grpc::Status AddBreakpoint(
        grpc::ServerContext* context,
        const AddBreakpointRequest* request,
        AddBreakpointResponse* response) override;

    grpc::Status RemoveBreakpoint(
        grpc::ServerContext* context,
        const RemoveBreakpointRequest* request,
        RemoveBreakpointResponse* response) override;

    grpc::Status ListBreakpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ListBreakpointsResponse* response) override;

    grpc::Status ClearBreakpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ClearBreakpointsResponse* response) override;

    // Device state inspection
    grpc::Status GetSystemViaState(
        grpc::ServerContext* context,
        const GetSystemViaStateRequest* request,
        ViaState* response) override;

    grpc::Status GetUserViaState(
        grpc::ServerContext* context,
        const GetUserViaStateRequest* request,
        ViaState* response) override;

    grpc::Status GetCrtcState(
        grpc::ServerContext* context,
        const GetCrtcStateRequest* request,
        CrtcState* response) override;

    grpc::Status GetVideoUlaState(
        grpc::ServerContext* context,
        const GetVideoUlaStateRequest* request,
        VideoUlaState* response) override;

    grpc::Status GetAddressableLatchState(
        grpc::ServerContext* context,
        const GetAddressableLatchStateRequest* request,
        AddressableLatchState* response) override;

    grpc::Status GetSoundGeneratorState(
        grpc::ServerContext* context,
        const GetSoundGeneratorStateRequest* request,
        SoundGeneratorState* response) override;

    // CPU state
    grpc::Status Get6502State(
        grpc::ServerContext* context,
        const Get6502StateRequest* request,
        Cpu6502State* response) override;

    grpc::Status Set6502State(
        grpc::ServerContext* context,
        const Set6502StateRequest* request,
        Set6502StateResponse* response) override;

private:
    void fill_execution_state(ExecutionState* state);
    void update_breakpoint_callback();

    MachineType& machine_;
    std::mutex mutex_;
    std::vector<BreakpointEntry> breakpoints_;
    std::atomic<uint32_t> next_breakpoint_id_{1};
    std::string halt_reason_;
};

//////////////////////////////////////////////////////////////////////////////
// DebuggerControlServiceImpl template implementation
//////////////////////////////////////////////////////////////////////////////

template<typename MachineType>
DebuggerControlServiceImpl<MachineType>::DebuggerControlServiceImpl(MachineType& machine)
    : machine_(machine) {
}

template<typename MachineType>
void DebuggerControlServiceImpl<MachineType>::fill_execution_state(ExecutionState* state) {
    state->set_is_running(!machine_.is_paused());
    state->set_cycle_count(machine_.cycle_count());
    state->set_halt_reason(halt_reason_);
    state->set_sequence(machine_.sequence());
}

template<typename MachineType>
void DebuggerControlServiceImpl<MachineType>::update_breakpoint_callback() {
    if (breakpoints_.empty()) {
        machine_.set_instruction_callback(nullptr);
    } else {
        machine_.set_instruction_callback(
            [this](uint16_t pc, uint64_t /*cycle*/) -> bool {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& bp : breakpoints_) {
                    if (bp.address == pc) {
                        std::ostringstream oss;
                        oss << "breakpoint at $" << std::hex << std::uppercase
                            << std::setw(4) << std::setfill('0') << pc;
                        halt_reason_ = oss.str();
                        machine_.pause();
                        return false;  // Stop execution
                    }
                }
                return true;  // Continue
            }
        );
    }
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::GetState(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ExecutionState* response) {

    std::lock_guard<std::mutex> lock(mutex_);
    fill_execution_state(response);
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::Run(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    RunResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!machine_.is_paused()) {
        response->set_success(false);
        response->set_error("already running");
        return grpc::Status::OK;
    }

    halt_reason_.clear();
    machine_.resume();
    response->set_success(true);
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::Stop(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    StopResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    machine_.pause();
    halt_reason_ = "stopped by debugger";
    response->set_success(true);
    fill_execution_state(response->mutable_state());
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::Reset(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ResetResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    machine_.reset();

    // Complete the 7-cycle reset sequence so PC contains the actual
    // reset vector value. The 6502 reads the reset vector from $FFFC/$FFFD
    // during cycles 4-6.
    machine_.run(7);

    // Leave machine paused at first instruction for debugger control
    machine_.pause();

    halt_reason_.clear();
    response->set_success(true);
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::StepInstruction(
    grpc::ServerContext* /*context*/,
    const StepRequest* request,
    StepResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!machine_.is_paused()) {
        response->set_success(false);
        response->set_error("machine is running");
        return grpc::Status::OK;
    }

    uint32_t count = request->count();
    if (count == 0) count = 1;

    uint64_t start_cycle = machine_.cycle_count();
    uint32_t instructions = 0;

    for (uint32_t i = 0; i < count; ++i) {
        machine_.step_instruction();
        ++instructions;
    }

    halt_reason_.clear();
    response->set_success(true);
    response->set_instructions_executed(instructions);
    response->set_cycles_executed(machine_.cycle_count() - start_cycle);
    fill_execution_state(response->mutable_state());
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::StepCycle(
    grpc::ServerContext* /*context*/,
    const StepRequest* request,
    StepResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!machine_.is_paused()) {
        response->set_success(false);
        response->set_error("machine is running");
        return grpc::Status::OK;
    }

    uint32_t count = request->count();
    if (count == 0) count = 1;

    uint64_t start_cycle = machine_.cycle_count();

    for (uint32_t i = 0; i < count; ++i) {
        machine_.step();
    }

    halt_reason_.clear();
    response->set_success(true);
    response->set_instructions_executed(0);  // Unknown for cycle stepping
    response->set_cycles_executed(machine_.cycle_count() - start_cycle);
    fill_execution_state(response->mutable_state());
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::ReadMemory(
    grpc::ServerContext* /*context*/,
    const ReadMemoryRequest* request,
    ReadMemoryResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t address = request->address();
    uint32_t length = request->length();
    bool has_pc = request->has_simulated_pc();
    uint16_t pc = has_pc ? static_cast<uint16_t>(request->simulated_pc()) : 0;

    std::string data;
    data.reserve(length);

    for (uint32_t i = 0; i < length && (address + i) <= 0xFFFF; ++i) {
        uint16_t addr = static_cast<uint16_t>(address + i);
        data.push_back(static_cast<char>(
            read_with_optional_pc(machine_, addr, has_pc, pc)));
    }

    response->set_data(std::move(data));
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::WriteMemory(
    grpc::ServerContext* /*context*/,
    const WriteMemoryRequest* request,
    WriteMemoryResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t address = request->address();
    const std::string& data = request->data();
    bool has_pc = request->has_simulated_pc();
    uint16_t pc = has_pc ? static_cast<uint16_t>(request->simulated_pc()) : 0;

    for (size_t i = 0; i < data.size() && (address + i) <= 0xFFFF; ++i) {
        uint16_t addr = static_cast<uint16_t>(address + i);
        write_with_optional_pc(machine_, addr, static_cast<uint8_t>(data[i]), has_pc, pc);
    }

    response->set_success(true);
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::PeekMemory(
    grpc::ServerContext* /*context*/,
    const PeekMemoryRequest* request,
    PeekMemoryResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t address = request->address();
    uint32_t length = request->length();
    bool has_pc = request->has_simulated_pc();
    uint16_t pc = has_pc ? static_cast<uint16_t>(request->simulated_pc()) : 0;

    std::string data;
    data.reserve(length);

    for (uint32_t i = 0; i < length && (address + i) <= 0xFFFF; ++i) {
        uint16_t addr = static_cast<uint16_t>(address + i);
        data.push_back(static_cast<char>(
            peek_with_optional_pc(machine_, addr, has_pc, pc)));
    }

    response->set_data(std::move(data));
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::GetMemoryRegions(
    grpc::ServerContext* /*context*/,
    const GetMemoryRegionsRequest* /*request*/,
    GetMemoryRegionsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Get machine type from hardware
    response->set_machine_type(std::string(machine_.memory().MACHINE_TYPE));

    // Get regions from hardware
    auto regions = machine_.memory().get_memory_regions();
    for (const auto& region : regions) {
        auto* pb_region = response->add_regions();
        pb_region->set_name(std::string(region.name));
        pb_region->set_base_address(region.base_address);
        pb_region->set_size(region.size);
        pb_region->set_readable(beebium::has_flag(region.flags, beebium::RegionFlags::Readable));
        pb_region->set_writable(beebium::has_flag(region.flags, beebium::RegionFlags::Writable));
        pb_region->set_has_side_effects(beebium::has_flag(region.flags, beebium::RegionFlags::HasSideEffects));
        pb_region->set_populated(beebium::has_flag(region.flags, beebium::RegionFlags::Populated));
        pb_region->set_active(beebium::has_flag(region.flags, beebium::RegionFlags::Active));
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::PeekRegion(
    grpc::ServerContext* /*context*/,
    const RegionAccessRequest* request,
    RegionAccessResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    const std::string& region_name = request->region_name();
    uint32_t address = request->address();
    uint32_t length = request->length();

    try {
        std::string data;
        data.reserve(length);

        for (uint32_t i = 0; i < length; ++i) {
            data.push_back(static_cast<char>(
                machine_.memory().peek_region(region_name, address + i)));
        }

        response->set_data(std::move(data));
        return grpc::Status::OK;
    } catch (const std::invalid_argument& e) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
    }
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::ReadRegion(
    grpc::ServerContext* /*context*/,
    const RegionAccessRequest* request,
    RegionAccessResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    const std::string& region_name = request->region_name();
    uint32_t address = request->address();
    uint32_t length = request->length();

    try {
        std::string data;
        data.reserve(length);

        for (uint32_t i = 0; i < length; ++i) {
            data.push_back(static_cast<char>(
                machine_.memory().read_region(region_name, address + i)));
        }

        response->set_data(std::move(data));
        return grpc::Status::OK;
    } catch (const std::invalid_argument& e) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
    }
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::WriteRegion(
    grpc::ServerContext* /*context*/,
    const WriteRegionRequest* request,
    WriteRegionResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    const std::string& region_name = request->region_name();
    uint32_t address = request->address();
    const std::string& data = request->data();

    try {
        for (size_t i = 0; i < data.size(); ++i) {
            machine_.memory().write_region(region_name, address + static_cast<uint32_t>(i),
                static_cast<uint8_t>(data[i]));
        }

        response->set_success(true);
        return grpc::Status::OK;
    } catch (const std::invalid_argument& e) {
        response->set_success(false);
        response->set_error(e.what());
        return grpc::Status::OK;
    }
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::AddBreakpoint(
    grpc::ServerContext* /*context*/,
    const AddBreakpointRequest* request,
    AddBreakpointResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t address = request->address();
    if (address > 0xFFFF) {
        response->set_success(false);
        return grpc::Status::OK;
    }

    uint32_t id = next_breakpoint_id_++;
    breakpoints_.push_back({id, address});
    update_breakpoint_callback();

    response->set_success(true);
    response->set_id(id);
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::RemoveBreakpoint(
    grpc::ServerContext* /*context*/,
    const RemoveBreakpointRequest* request,
    RemoveBreakpointResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t id = request->id();
    auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
        [id](const BreakpointEntry& bp) { return bp.id == id; });

    if (it != breakpoints_.end()) {
        breakpoints_.erase(it);
        update_breakpoint_callback();
        response->set_success(true);
    } else {
        response->set_success(false);
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::ListBreakpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ListBreakpointsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& bp : breakpoints_) {
        auto* pb_bp = response->add_breakpoints();
        pb_bp->set_id(bp.id);
        pb_bp->set_address(bp.address);
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::ClearBreakpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ClearBreakpointsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t count = static_cast<uint32_t>(breakpoints_.size());
    breakpoints_.clear();
    update_breakpoint_callback();

    response->set_count_removed(count);
    return grpc::Status::OK;
}

//////////////////////////////////////////////////////////////////////////////
// CPU State - DebuggerControlServiceImpl
//////////////////////////////////////////////////////////////////////////////

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::Get6502State(
    grpc::ServerContext* /*context*/,
    const Get6502StateRequest* /*request*/,
    Cpu6502State* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    response->set_a(machine_.a());
    response->set_x(machine_.x());
    response->set_y(machine_.y());
    response->set_sp(machine_.sp());
    response->set_pc(machine_.pc());
    response->set_p(machine_.p());

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::Set6502State(
    grpc::ServerContext* /*context*/,
    const Set6502StateRequest* request,
    Set6502StateResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (request->has_a()) {
        machine_.set_a(static_cast<uint8_t>(request->a()));
    }
    if (request->has_x()) {
        machine_.set_x(static_cast<uint8_t>(request->x()));
    }
    if (request->has_y()) {
        machine_.set_y(static_cast<uint8_t>(request->y()));
    }
    if (request->has_sp()) {
        machine_.set_sp(static_cast<uint8_t>(request->sp()));
    }
    if (request->has_pc()) {
        machine_.set_pc(static_cast<uint16_t>(request->pc()));
    }
    if (request->has_p()) {
        machine_.set_p(static_cast<uint8_t>(request->p()));
    }

    response->set_success(true);
    return grpc::Status::OK;
}

//////////////////////////////////////////////////////////////////////////////
// Device State Inspection - DebuggerControlServiceImpl
//////////////////////////////////////////////////////////////////////////////

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

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::GetSystemViaState(
    grpc::ServerContext* /*context*/,
    const GetSystemViaStateRequest* /*request*/,
    ViaState* response) {

    std::lock_guard<std::mutex> lock(mutex_);
    fill_via_state(machine_.memory().system_via, response);
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::GetUserViaState(
    grpc::ServerContext* /*context*/,
    const GetUserViaStateRequest* /*request*/,
    ViaState* response) {

    std::lock_guard<std::mutex> lock(mutex_);
    fill_via_state(machine_.memory().user_via, response);
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::GetCrtcState(
    grpc::ServerContext* /*context*/,
    const GetCrtcStateRequest* /*request*/,
    CrtcState* response) {

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

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::GetVideoUlaState(
    grpc::ServerContext* /*context*/,
    const GetVideoUlaStateRequest* /*request*/,
    VideoUlaState* response) {

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

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::GetAddressableLatchState(
    grpc::ServerContext* /*context*/,
    const GetAddressableLatchStateRequest* /*request*/,
    AddressableLatchState* response) {

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

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::GetSoundGeneratorState(
    grpc::ServerContext* /*context*/,
    const GetSoundGeneratorStateRequest* /*request*/,
    SoundGeneratorState* response) {

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

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_DEBUGGER_SERVICE_HPP
