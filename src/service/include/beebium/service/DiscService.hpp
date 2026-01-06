// Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVICE_DISC_SERVICE_HPP
#define BEEBIUM_SERVICE_DISC_SERVICE_HPP

#include "disc.grpc.pb.h"
#include "beebium/disc/DiscDrive.hpp"
#include "beebium/disc/DiscLoader.hpp"

#include <grpcpp/grpcpp.h>
#include <concepts>
#include <mutex>

namespace beebium::service {

// Concept to detect if a hardware type has disc drives
template<typename T>
concept HasDiscDrives = requires(T& hw) {
    { hw.disc_drive_0 } -> std::same_as<DiscDrive&>;
    { hw.disc_drive_1 } -> std::same_as<DiscDrive&>;
};

// gRPC service implementation for DiscService
template<typename MachineType>
class DiscServiceImpl final : public DiscService::Service {
public:
    explicit DiscServiceImpl(MachineType& machine)
        : machine_(machine) {}

    ~DiscServiceImpl() override = default;

    // Non-copyable
    DiscServiceImpl(const DiscServiceImpl&) = delete;
    DiscServiceImpl& operator=(const DiscServiceImpl&) = delete;

    grpc::Status InsertDisc(
        grpc::ServerContext* context,
        const InsertDiscRequest* request,
        InsertDiscResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            response->set_success(false);
            response->set_error("Machine has no disc controller");
            return grpc::Status::OK;
        } else {
            uint32_t drive_num = request->drive();
            if (drive_num > 1) {
                response->set_success(false);
                response->set_error("Invalid drive number (must be 0 or 1)");
                return grpc::Status::OK;
            }

            // Load disc from URL
            auto result = load_disc_from_url_or_filepath(request->url());
            if (!result) {
                response->set_success(false);
                response->set_error(result.error);
                return grpc::Status::OK;
            }

            // Apply write protection override if requested
            if (request->write_protect_override()) {
                result.image->set_write_protected(true);
            }

            // Get the target drive
            DiscDrive& drive = (drive_num == 0)
                ? machine_.state().memory.disc_drive_0
                : machine_.state().memory.disc_drive_1;

            // Eject any existing disc first
            if (drive.state() != DriveState::Empty) {
                drive.eject_immediate();
            }

            // Fill metadata before inserting (since insert moves the image)
            fill_disc_metadata(response->mutable_disc(), result.image.get());

            // Insert the new disc
            drive.insert(std::move(result.image), request->url());

            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status EjectDisc(
        grpc::ServerContext* context,
        const EjectDiscRequest* request,
        EjectDiscResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            response->set_accepted(false);
            response->set_error("Machine has no disc controller");
            return grpc::Status::OK;
        } else {
            uint32_t drive_num = request->drive();
            if (drive_num > 1) {
                response->set_accepted(false);
                response->set_error("Invalid drive number (must be 0 or 1)");
                return grpc::Status::OK;
            }

            DiscDrive& drive = (drive_num == 0)
                ? machine_.state().memory.disc_drive_0
                : machine_.state().memory.disc_drive_1;

            if (drive.state() == DriveState::Empty) {
                response->set_accepted(false);
                response->set_error("Drive is empty");
                return grpc::Status::OK;
            }

            if (request->immediate()) {
                // Immediate eject
                drive.eject_immediate();
                response->set_accepted(true);
            } else {
                // Safe eject with quiescence
                EjectOptions opts;
                if (request->quiescence_ms() > 0) {
                    opts.quiescence_duration = std::chrono::milliseconds(request->quiescence_ms());
                }
                if (request->force_after_ms() > 0) {
                    opts.force_after = std::chrono::milliseconds(request->force_after_ms());
                }

                bool accepted = drive.request_eject(opts);
                response->set_accepted(accepted);
                if (!accepted) {
                    response->set_error("Eject already in progress");
                }
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status GetDriveStatus(
        grpc::ServerContext* context,
        const GetDriveStatusRequest* request,
        GetDriveStatusResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            response->set_has_disc_controller(false);
            response->set_controller_type("");
            return grpc::Status::OK;
        } else {
            response->set_has_disc_controller(true);
            response->set_controller_type("WD1770");

            // Drive 0
            fill_drive_status(response->add_drives(), 0,
                machine_.state().memory.disc_drive_0);

            // Drive 1
            fill_drive_status(response->add_drives(), 1,
                machine_.state().memory.disc_drive_1);

            return grpc::Status::OK;
        }
    }

    grpc::Status SubscribeDiscEvents(
        grpc::ServerContext* context,
        const SubscribeDiscEventsRequest* request,
        grpc::ServerWriter<DiscEvent>* writer) override
    {
        (void)request;

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            // No disc controller - just return immediately
            return grpc::Status::OK;
        } else {
            // Track previous state for change detection
            DriveState prev_state_0 = DriveState::Empty;
            DriveState prev_state_1 = DriveState::Empty;
            bool prev_motor_0 = false;
            bool prev_motor_1 = false;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                prev_state_0 = machine_.state().memory.disc_drive_0.state();
                prev_state_1 = machine_.state().memory.disc_drive_1.state();
                prev_motor_0 = machine_.state().memory.disc_drive_0.motor_on();
                prev_motor_1 = machine_.state().memory.disc_drive_1.motor_on();
            }

            while (!context->IsCancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                std::lock_guard<std::mutex> lock(mutex_);

                // Tick safe eject for both drives (checks quiescence, completes ejection)
                machine_.state().memory.disc_drive_0.tick_eject();
                machine_.state().memory.disc_drive_1.tick_eject();

                // Check drive 0
                check_and_send_events(writer, 0,
                    machine_.state().memory.disc_drive_0,
                    prev_state_0, prev_motor_0);

                // Check drive 1
                check_and_send_events(writer, 1,
                    machine_.state().memory.disc_drive_1,
                    prev_state_1, prev_motor_1);
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status SetSpinUpDelay(
        grpc::ServerContext* context,
        const SetSpinUpDelayRequest* request,
        SetSpinUpDelayResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            response->set_success(false);
            response->set_error("Machine has no disc controller");
            return grpc::Status::OK;
        } else {
            machine_.state().memory.disc_controller.set_spin_up_delay_enabled(request->enabled());
            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status GetSpinUpDelay(
        grpc::ServerContext* context,
        const GetSpinUpDelayRequest* request,
        GetSpinUpDelayResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            // No disc controller - return false (no delay possible)
            response->set_enabled(false);
            return grpc::Status::OK;
        } else {
            response->set_enabled(machine_.state().memory.disc_controller.spin_up_delay_enabled());
            return grpc::Status::OK;
        }
    }

private:
    void fill_disc_metadata(DiscMetadata* metadata, const DiscImage* image) {
        if (!image) return;

        metadata->set_name(image->name());
        metadata->set_sides(image->sides());
        metadata->set_tracks_per_side(image->tracks_per_side());
        metadata->set_sectors_per_track(image->sectors_per_track());
        metadata->set_sector_size(image->sector_size());
        metadata->set_write_protected(image->is_write_protected());
    }

    void fill_drive_status(beebium::DriveStatus* status, uint32_t drive_num,
                           const DiscDrive& drive) {
        status->set_drive(drive_num);

        switch (drive.state()) {
            case DriveState::Empty:
                status->set_state(DISC_DRIVE_STATE_EMPTY);
                break;
            case DriveState::Loaded:
                status->set_state(DISC_DRIVE_STATE_LOADED);
                break;
            case DriveState::Ejecting:
                status->set_state(DISC_DRIVE_STATE_EJECTING);
                break;
        }

        if (drive.disc()) {
            status->set_disc_name(drive.disc()->name());
            fill_disc_metadata(status->mutable_disc(), drive.disc());
        }

        status->set_disc_url(drive.source_url());
        status->set_motor_on(drive.motor_on());
        status->set_current_track(drive.current_track());
        status->set_write_protected(drive.is_write_protected());
    }

    void check_and_send_events(grpc::ServerWriter<DiscEvent>* writer,
                               uint32_t drive_num,
                               DiscDrive& drive,
                               DriveState& prev_state,
                               bool& prev_motor) {
        DriveState curr_state = drive.state();
        bool curr_motor = drive.motor_on();
        uint64_t cycle = machine_.cycle_count();

        // State change events
        if (curr_state != prev_state) {
            DiscEvent event;
            event.set_drive(drive_num);
            event.set_timestamp_cycles(cycle);

            if (prev_state == DriveState::Empty && curr_state == DriveState::Loaded) {
                event.set_type(DISC_EVENT_INSERTED);
                fill_disc_metadata(event.mutable_disc(), drive.disc());
            } else if (prev_state == DriveState::Loaded && curr_state == DriveState::Ejecting) {
                event.set_type(DISC_EVENT_EJECT_REQUESTED);
            } else if (prev_state == DriveState::Ejecting && curr_state == DriveState::Empty) {
                if (drive.was_forced_eject()) {
                    event.set_type(DISC_EVENT_FORCE_EJECTED);
                } else {
                    event.set_type(DISC_EVENT_EJECTED);
                }
            } else if (prev_state == DriveState::Ejecting && curr_state == DriveState::Loaded) {
                event.set_type(DISC_EVENT_EJECT_CANCELLED);
            } else if (curr_state == DriveState::Empty) {
                // Any other transition to empty (e.g., immediate eject from Loaded)
                event.set_type(DISC_EVENT_EJECTED);
            }

            writer->Write(event);
            prev_state = curr_state;
        }

        // Motor change events
        if (curr_motor != prev_motor) {
            DiscEvent event;
            event.set_drive(drive_num);
            event.set_timestamp_cycles(cycle);
            event.set_type(curr_motor ? DISC_EVENT_MOTOR_ON : DISC_EVENT_MOTOR_OFF);
            writer->Write(event);
            prev_motor = curr_motor;
        }
    }

    MachineType& machine_;
    std::mutex mutex_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_DISC_SERVICE_HPP
