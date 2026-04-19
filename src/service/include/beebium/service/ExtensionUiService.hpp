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

#ifndef BEEBIUM_SERVICE_EXTENSION_UI_SERVICE_HPP
#define BEEBIUM_SERVICE_EXTENSION_UI_SERVICE_HPP

// Server-driven Extension UI gRPC service.
//
// Reads from EconetTransportRegistry and ExtensionRegistry (peripheral)
// to discover extensions; routes SubscribeView streams and Dispatch
// events to the matching ExtensionUi. Owns view-revision plumbing and
// the dispatch validation gauntlet (extension exists, control id is
// known, payload type matches the control type, view revision is
// current). Extensions only see validated events.

#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/Extension.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/ExtensionUi.hpp"

#include "extension_ui.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>

namespace beebium::service {

namespace detail {

// Walk the View's Control tree, populating (id -> control_case) so the
// Dispatch validator can look up an addressed control by id and check
// its type.
inline void collect_control_ids(
    const ::beebium::Control& control,
    std::unordered_map<std::string, ::beebium::Control::ControlCase>& out)
{
    out[control.id()] = control.control_case();
    if (control.control_case() == ::beebium::Control::kGroup) {
        for (const auto& child : control.group().controls()) {
            collect_control_ids(child, out);
        }
    }
}

inline const char* control_type_name(::beebium::Control::ControlCase c) noexcept {
    switch (c) {
        case ::beebium::Control::kLabel:     return "Label";
        case ::beebium::Control::kIndicator: return "Indicator";
        case ::beebium::Control::kToggle:    return "Toggle";
        case ::beebium::Control::kButton:    return "Button";
        case ::beebium::Control::kChoice:    return "Choice";
        case ::beebium::Control::kTextInput: return "TextInput";
        case ::beebium::Control::kGroup:     return "Group";
        case ::beebium::Control::CONTROL_NOT_SET: return "(unset)";
    }
    return "(unknown)";
}

// Verify that the Dispatch payload variant matches the addressed
// control's type. Read-only controls (Label, Indicator, Group) are not
// dispatchable. On mismatch, populates `error` and returns false.
inline bool payload_matches_control(
    const ::beebium::DispatchRequest& req,
    ::beebium::Control::ControlCase ctrl_case,
    std::string& error)
{
    using PC = ::beebium::DispatchRequest::PayloadCase;
    using CC = ::beebium::Control::ControlCase;

    PC actual = req.payload_case();
    PC expected = PC::PAYLOAD_NOT_SET;
    bool dispatchable = true;

    switch (ctrl_case) {
        case CC::kToggle:    expected = PC::kBoolValue;   break;
        case CC::kTextInput: expected = PC::kStringValue; break;
        case CC::kChoice:    expected = PC::kIndexValue;  break;
        case CC::kButton:    expected = PC::PAYLOAD_NOT_SET; break;
        case CC::kLabel:
        case CC::kIndicator:
        case CC::kGroup:
        case CC::CONTROL_NOT_SET:
            dispatchable = false;
            break;
    }

    if (!dispatchable) {
        error = "control '" + req.control_id() + "' (" +
                control_type_name(ctrl_case) +
                ") is not dispatchable";
        return false;
    }
    if (actual != expected) {
        error = "payload type mismatch for control '" + req.control_id() +
                "' (" + control_type_name(ctrl_case) + ")";
        return false;
    }
    return true;
}

}  // namespace detail

class ExtensionUiServiceImpl final
    : public ::beebium::ExtensionUiService::Service {
public:
    ExtensionUiServiceImpl(EconetTransportRegistry& transport_registry,
                           ExtensionRegistry& peripheral_registry)
        : transport_registry_(transport_registry),
          peripheral_registry_(peripheral_registry) {}

    grpc::Status SubscribeView(
        grpc::ServerContext* context,
        const ::beebium::SubscribeViewRequest* request,
        grpc::ServerWriter<::beebium::View>* writer) override
    {
        Extension* ext = find_extension(request->extension_name());
        ExtensionUi* ui = ext ? ext->ui() : nullptr;
        if (!ui) {
            return grpc::Status(
                grpc::StatusCode::NOT_FOUND,
                "Extension '" + request->extension_name() +
                "' not found or has no UI");
        }

        // Initial last_pushed = 0 ensures the first iteration always
        // pushes (ExtensionUi::current_revision() starts at 1).
        std::uint64_t last_pushed = 0;
        const auto interval = std::chrono::milliseconds(50);

        while (!context->IsCancelled()) {
            std::uint64_t curr = ui->current_revision();
            if (curr != last_pushed) {
                ::beebium::View view;
                ui->build_view(&view);
                view.set_extension_name(request->extension_name());
                view.set_view_revision(curr);
                if (!writer->Write(view)) {
                    break;  // client disconnected
                }
                last_pushed = curr;
            }
            std::this_thread::sleep_for(interval);
        }
        return grpc::Status::OK;
    }

    grpc::Status Dispatch(
        grpc::ServerContext*,
        const ::beebium::DispatchRequest* request,
        ::beebium::DispatchResponse* response) override
    {
        Extension* ext = find_extension(request->extension_name());
        if (!ext) {
            response->set_accepted(false);
            response->set_error(
                "unknown extension: " + request->extension_name());
            return grpc::Status::OK;
        }
        ExtensionUi* ui = ext->ui();
        if (!ui) {
            response->set_accepted(false);
            response->set_error(
                "extension '" + request->extension_name() + "' has no UI");
            return grpc::Status::OK;
        }

        std::uint64_t curr_rev = ui->current_revision();
        if (request->view_revision() != curr_rev) {
            response->set_accepted(false);
            response->set_error(
                "stale event: client revision " +
                std::to_string(request->view_revision()) +
                ", current " + std::to_string(curr_rev));
            return grpc::Status::OK;
        }

        ::beebium::View view;
        ui->build_view(&view);

        std::unordered_map<std::string, ::beebium::Control::ControlCase> ids;
        detail::collect_control_ids(view.root(), ids);
        auto it = ids.find(request->control_id());
        if (it == ids.end()) {
            response->set_accepted(false);
            response->set_error(
                "unknown control id: " + request->control_id());
            return grpc::Status::OK;
        }

        std::string error;
        if (!detail::payload_matches_control(*request, it->second, error)) {
            response->set_accepted(false);
            response->set_error(std::move(error));
            return grpc::Status::OK;
        }

        ui->handle_event(*request);
        response->set_accepted(true);
        return grpc::Status::OK;
    }

private:
    Extension* find_extension(const std::string& name) const {
        for (const auto& ext : transport_registry_.extensions()) {
            if (ext->name() == name) {
                return ext.get();
            }
        }
        for (auto* ext : peripheral_registry_.extensions()) {
            if (ext->name() == name) {
                return ext;
            }
        }
        return nullptr;
    }

    EconetTransportRegistry& transport_registry_;
    ExtensionRegistry& peripheral_registry_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_EXTENSION_UI_SERVICE_HPP
