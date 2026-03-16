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
#include "beebium/Types.hpp"
#include "beebium/tube/TubeShared.hpp"
#include <moodycamel/readerwriterqueue.h>
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <vector>
#include <atomic>
#include <condition_variable>
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

/// Internal breakpoint record (service-layer, holds parsed condition)
struct BreakpointRecord {
    uint32_t id;
    uint32_t address;
    bool stop_counterpart = false;
    std::optional<beebium::CompiledExpression> condition;
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

    // Watchpoints
    grpc::Status AddWatchpoint(
        grpc::ServerContext* context,
        const AddWatchpointRequest* request,
        AddWatchpointResponse* response) override;

    grpc::Status RemoveWatchpoint(
        grpc::ServerContext* context,
        const RemoveWatchpointRequest* request,
        RemoveWatchpointResponse* response) override;

    grpc::Status ListWatchpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ListWatchpointsResponse* response) override;

    grpc::Status ClearWatchpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ClearWatchpointsResponse* response) override;

    // CPU state
    grpc::Status Get6502State(
        grpc::ServerContext* context,
        const Get6502StateRequest* request,
        Cpu6502State* response) override;

    grpc::Status Set6502State(
        grpc::ServerContext* context,
        const Set6502StateRequest* request,
        Set6502StateResponse* response) override;

    // Event streaming
    grpc::Status WatchExecutionState(
        grpc::ServerContext* context,
        const WatchExecutionStateRequest* request,
        grpc::ServerWriter<ExecutionStateEvent>* writer) override;

private:
    void fill_execution_state(ExecutionState* state);
    void update_breakpoint_entries();
    void update_watchpoint_entries();
    void enqueue_event(StopReason reason);
    void enqueue_event(StopReason reason, const WatchpointHitInfo& watchpoint_hit);
    void signal_counterpart_stop();

    MachineType& machine_;
    std::mutex mutex_;
    std::vector<BreakpointRecord> breakpoints_;
    std::atomic<uint32_t> next_breakpoint_id_{1};
    std::string halt_reason_;

    // Watchpoint state
    struct WatchpointRecord {
        uint32_t id;
        uint32_t start_address;
        uint32_t end_address;
        beebium::WatchType type;
        bool stop_counterpart = false;
        std::optional<beebium::CompiledExpression> condition;
    };
    std::vector<WatchpointRecord> watchpoints_;
    std::atomic<uint32_t> next_watchpoint_id_{1};

    // Event queue for execution state watchers.
    // Each state transition pushes an event; the WatchExecutionState loop
    // drains the queue and sends each event individually. This prevents
    // rapid transitions from being coalesced into a single notification.
    struct ExecutionEvent {
        StopReason reason = STOP_REASON_UNKNOWN;
        bool is_running = false;
        uint64_t cycle_count = 0;
        uint64_t sequence = 0;
        std::string halt_reason;
        WatchpointHitInfo watchpoint_hit;
        bool has_watchpoint_hit = false;
    };
    moodycamel::ReaderWriterQueue<ExecutionEvent> event_queue_{32};
    mutable std::mutex event_queue_mutex_;
    std::condition_variable event_queue_cv_;
};

//////////////////////////////////////////////////////////////////////////////
// DebuggerControlServiceImpl template implementation
//////////////////////////////////////////////////////////////////////////////

template<typename MachineType>
DebuggerControlServiceImpl<MachineType>::DebuggerControlServiceImpl(MachineType& machine)
    : machine_(machine) {
    machine_.set_breakpoint_hit_callback([this](const beebium::BreakpointEntry& bp, uint16_t pc) {
        // Increment hit counter
        auto& mutable_bp = const_cast<beebium::BreakpointEntry&>(bp);
        ++mutable_bp.hit_count;

        // Evaluate condition (if present; absent = unconditional = stop)
        bool should_stop = true;
        if (bp.condition) {
            beebium::ExprCpuState cpu_state{
                machine_.a(), machine_.x(), machine_.y(),
                machine_.sp(), machine_.p(),
                machine_.cpu().opcode_pc.w, machine_.cycle_count(),
                bp.hit_count
            };
            auto peek_fn = [](void* ctx, uint16_t a) -> uint8_t {
                return static_cast<MachineType*>(ctx)->peek(a);
            };
            should_stop = beebium::evaluate(
                *bp.condition, cpu_state, peek_fn, &machine_) != 0;
        }

        if (!should_stop) return;

        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "breakpoint at $" << std::hex << std::uppercase
            << std::setw(4) << std::setfill('0') << pc;
        halt_reason_ = oss.str();

        if (bp.stop_counterpart) {
            signal_counterpart_stop();
        }

        machine_.pause();
        enqueue_event(STOP_REASON_BREAKPOINT);
    });

    machine_.set_watchpoint_hit_callback(
        [this](const beebium::WatchpointEntry& wp, uint16_t addr, uint8_t value, bool is_write) {
            // Increment hit counter (available as `hits` in condition expression)
            auto& mutable_wp = const_cast<beebium::WatchpointEntry&>(wp);
            ++mutable_wp.hit_count;

            // Evaluate condition (if present; absent = unconditional = stop)
            bool should_stop = true;
            if (wp.condition) {
                beebium::ExprCpuState cpu_state{
                    machine_.a(), machine_.x(), machine_.y(),
                    machine_.sp(), machine_.p(),
                    machine_.cpu().opcode_pc.w, machine_.cycle_count(),
                    wp.hit_count
                };
                auto peek_fn = [](void* ctx, uint16_t a) -> uint8_t {
                    return static_cast<MachineType*>(ctx)->peek(a);
                };
                should_stop = beebium::evaluate(
                    *wp.condition, cpu_state, peek_fn, &machine_) != 0;
            }

            if (!should_stop) return;

            std::lock_guard<std::mutex> lock(mutex_);
            std::ostringstream oss;
            oss << (is_write ? "write" : "read") << " watchpoint at $"
                << std::hex << std::uppercase
                << std::setw(4) << std::setfill('0') << addr
                << " = $" << std::setw(2) << static_cast<unsigned>(value);
            halt_reason_ = oss.str();

            if (wp.stop_counterpart) {
                signal_counterpart_stop();
            }

            machine_.pause();

            WatchpointHitInfo hit_info;
            hit_info.set_watchpoint_id(wp.id);
            hit_info.set_address(addr);
            hit_info.set_value(value);
            hit_info.set_is_write(is_write);
            enqueue_event(STOP_REASON_WATCHPOINT, hit_info);
        });
}

template<typename MachineType>
void DebuggerControlServiceImpl<MachineType>::fill_execution_state(ExecutionState* state) {
    state->set_is_running(!machine_.is_paused());
    state->set_cycle_count(machine_.cycle_count());
    state->set_halt_reason(halt_reason_);
    state->set_sequence(machine_.sequence());
}

template<typename MachineType>
void DebuggerControlServiceImpl<MachineType>::enqueue_event(StopReason reason) {
    ExecutionEvent evt;
    evt.reason = reason;
    evt.is_running = !machine_.is_paused();
    evt.cycle_count = machine_.cycle_count();
    evt.sequence = machine_.sequence();
    evt.halt_reason = halt_reason_;
    event_queue_.enqueue(std::move(evt));
    event_queue_cv_.notify_all();
}

template<typename MachineType>
void DebuggerControlServiceImpl<MachineType>::update_breakpoint_entries() {
    std::vector<beebium::BreakpointEntry> entries;
    entries.reserve(breakpoints_.size());
    for (const auto& bp : breakpoints_) {
        entries.push_back({bp.id,
                          static_cast<uint16_t>(bp.address),
                          bp.stop_counterpart,
                          bp.condition,
                          0});  // reset hit_count
    }
    machine_.set_breakpoint_entries(std::move(entries));
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
    enqueue_event(STOP_REASON_UNKNOWN);
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
    enqueue_event(STOP_REASON_MANUAL);
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

    machine_.prepare_for_step();

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

    machine_.prepare_for_step();

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

    // Parse optional condition expression
    std::optional<beebium::CompiledExpression> condition;
    if (!request->condition().empty()) {
        auto result = beebium::compile(request->condition());
        if (std::holds_alternative<std::string>(result)) {
            response->set_success(false);
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                std::get<std::string>(result));
        }
        condition = std::get<beebium::CompiledExpression>(std::move(result));
    }

    uint32_t id = next_breakpoint_id_++;
    breakpoints_.push_back({id, address, request->stop_counterpart(), std::move(condition)});
    update_breakpoint_entries();

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
        [id](const BreakpointRecord& bp) { return bp.id == id; });

    if (it != breakpoints_.end()) {
        breakpoints_.erase(it);
        update_breakpoint_entries();
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
    update_breakpoint_entries();

    response->set_count_removed(count);
    return grpc::Status::OK;
}

//////////////////////////////////////////////////////////////////////////////
// Watchpoint RPCs - DebuggerControlServiceImpl
//////////////////////////////////////////////////////////////////////////////

template<typename MachineType>
void DebuggerControlServiceImpl<MachineType>::update_watchpoint_entries() {
    std::vector<beebium::WatchpointEntry> entries;
    entries.reserve(watchpoints_.size());
    for (const auto& wp : watchpoints_) {
        entries.push_back({wp.id,
                          static_cast<uint16_t>(wp.start_address),
                          static_cast<uint16_t>(wp.end_address),
                          wp.type,
                          wp.stop_counterpart,
                          wp.condition,
                          0});  // reset hit_count
    }
    machine_.set_watchpoint_entries(std::move(entries));
}

template<typename MachineType>
void DebuggerControlServiceImpl<MachineType>::enqueue_event(
    StopReason reason, const WatchpointHitInfo& watchpoint_hit) {
    ExecutionEvent evt;
    evt.reason = reason;
    evt.is_running = !machine_.is_paused();
    evt.cycle_count = machine_.cycle_count();
    evt.sequence = machine_.sequence();
    evt.halt_reason = halt_reason_;
    evt.watchpoint_hit = watchpoint_hit;
    evt.has_watchpoint_hit = true;
    event_queue_.enqueue(std::move(evt));
    event_queue_cv_.notify_all();
}

template<typename MachineType>
void DebuggerControlServiceImpl<MachineType>::signal_counterpart_stop() {
    auto* shared = machine_.tube_shared();
    if (shared) {
        shared->debugger_stop_signal.store(true, std::memory_order_release);
        shared->bus_stretch_cancel.store(true, std::memory_order_release);
    }
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::AddWatchpoint(
    grpc::ServerContext* /*context*/,
    const AddWatchpointRequest* request,
    AddWatchpointResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t start = request->start_address();
    uint32_t end = request->end_address();
    if (start > 0xFFFF || end > 0x10000 || start >= end) {
        response->set_success(false);
        return grpc::Status::OK;
    }

    beebium::WatchType type;
    switch (request->type()) {
        case WATCHPOINT_READ:  type = beebium::WATCH_READ; break;
        case WATCHPOINT_WRITE: type = beebium::WATCH_WRITE; break;
        default:               type = beebium::WATCH_BOTH; break;
    }

    // Parse optional condition expression
    std::optional<beebium::CompiledExpression> condition;
    if (!request->condition().empty()) {
        auto result = beebium::compile(request->condition());
        if (std::holds_alternative<std::string>(result)) {
            response->set_success(false);
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                std::get<std::string>(result));
        }
        condition = std::get<beebium::CompiledExpression>(std::move(result));
    }

    uint32_t id = next_watchpoint_id_++;
    watchpoints_.push_back({id, start, end, type, request->stop_counterpart(),
                           std::move(condition)});
    update_watchpoint_entries();

    response->set_success(true);
    response->set_id(id);
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::RemoveWatchpoint(
    grpc::ServerContext* /*context*/,
    const RemoveWatchpointRequest* request,
    RemoveWatchpointResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t id = request->id();
    auto it = std::find_if(watchpoints_.begin(), watchpoints_.end(),
        [id](const WatchpointRecord& wp) { return wp.id == id; });

    if (it != watchpoints_.end()) {
        watchpoints_.erase(it);
        update_watchpoint_entries();
        response->set_success(true);
    } else {
        response->set_success(false);
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::ListWatchpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ListWatchpointsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& wp : watchpoints_) {
        auto* pb_wp = response->add_watchpoints();
        pb_wp->set_id(wp.id);
        pb_wp->set_start_address(wp.start_address);
        pb_wp->set_end_address(wp.end_address);
        switch (wp.type) {
            case beebium::WATCH_READ:  pb_wp->set_type(WATCHPOINT_READ); break;
            case beebium::WATCH_WRITE: pb_wp->set_type(WATCHPOINT_WRITE); break;
            default:                   pb_wp->set_type(WATCHPOINT_BOTH); break;
        }
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::ClearWatchpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ClearWatchpointsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t count = static_cast<uint32_t>(watchpoints_.size());
    watchpoints_.clear();
    update_watchpoint_entries();

    response->set_count_removed(count);
    return grpc::Status::OK;
}

//////////////////////////////////////////////////////////////////////////////
// Event Streaming - DebuggerControlServiceImpl
//////////////////////////////////////////////////////////////////////////////

template<typename MachineType>
grpc::Status DebuggerControlServiceImpl<MachineType>::WatchExecutionState(
    grpc::ServerContext* context,
    const WatchExecutionStateRequest* /*request*/,
    grpc::ServerWriter<ExecutionStateEvent>* writer) {

    // Send initial state immediately
    {
        ExecutionStateEvent event;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fill_execution_state(event.mutable_state());
        }
        if (!writer->Write(event)) {
            return grpc::Status::OK;
        }
    }

    // Drain the event queue in a loop
    while (!context->IsCancelled()) {
        // Wait for events (bounded to allow cancellation checks)
        {
            std::unique_lock<std::mutex> lock(event_queue_mutex_);
            event_queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this, context] {
                return event_queue_.peek() != nullptr || context->IsCancelled();
            });
        }

        if (context->IsCancelled()) {
            break;
        }

        // Drain all queued events, sending each individually
        ExecutionEvent evt;
        while (event_queue_.try_dequeue(evt)) {
            ExecutionStateEvent proto_event;
            proto_event.set_reason(evt.reason);
            proto_event.mutable_state()->set_is_running(evt.is_running);
            proto_event.mutable_state()->set_cycle_count(evt.cycle_count);
            proto_event.mutable_state()->set_sequence(evt.sequence);
            proto_event.mutable_state()->set_halt_reason(evt.halt_reason);
            proto_event.set_message(evt.halt_reason);
            if (evt.has_watchpoint_hit) {
                *proto_event.mutable_watchpoint_hit() = evt.watchpoint_hit;
            }
            if (!writer->Write(proto_event)) {
                return grpc::Status::OK;
            }
        }
    }

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

    // Interrupt handler tracking
    response->set_in_nmi_handler(machine_.in_nmi_handler());
    response->set_in_irq_handler(machine_.in_irq_handler());

    // M6502 interrupt line state
    response->set_nmi_pending(machine_.cpu().nmi_flags != 0);
    response->set_irq_pending(machine_.cpu().irq_flags != 0
                              && !(machine_.p() & 0x04));
    response->set_device_irq_flags(machine_.cpu().device_irq_flags);
    response->set_device_nmi_flags(machine_.cpu().device_nmi_flags);

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

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_DEBUGGER_SERVICE_HPP
