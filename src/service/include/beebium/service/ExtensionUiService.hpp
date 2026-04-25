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

// Walk the View's Control tree, populating (id -> Control*) so the
// Dispatch validator can look up an addressed control by id and check
// both its type and its underlying message.
//
// Groups descend into their children. ModalEditors deliberately do NOT
// descend into their anchor or editor sub-trees: the ModalEditor itself
// is the only dispatch target at the top level, and its editor sub-
// controls are addressed only via EditorCommit.fields.field_id (which
// uses a separate per-ModalEditor lookup; see collect_editor_fields).
// Keeping editor sub-controls out of the top-level map also prevents
// id collisions between two ModalEditors' sub-trees from shadowing
// each other.
inline void collect_control_ids(
    const ::beebium::Control& control,
    std::unordered_map<std::string, const ::beebium::Control*>& out)
{
    out[control.id()] = &control;
    if (control.control_case() == ::beebium::Control::kGroup) {
        for (const auto& child : control.group().controls()) {
            collect_control_ids(child, out);
        }
    }
}

// Walk a ModalEditor's editor sub-tree, populating (field_id -> Control*)
// for EditorCommit field validation. Descends into Groups; does not
// descend into nested ModalEditors (those are their own atomic units).
inline void collect_editor_fields(
    const ::beebium::Control& editor_root,
    std::unordered_map<std::string, const ::beebium::Control*>& out)
{
    out[editor_root.id()] = &editor_root;
    if (editor_root.control_case() == ::beebium::Control::kGroup) {
        for (const auto& child : editor_root.group().controls()) {
            collect_editor_fields(child, out);
        }
    }
}

inline const char* control_type_name(::beebium::Control::ControlCase c) noexcept {
    switch (c) {
        case ::beebium::Control::kLabel:          return "Label";
        case ::beebium::Control::kIndicator:      return "Indicator";
        case ::beebium::Control::kToggle:         return "Toggle";
        case ::beebium::Control::kButton:         return "Button";
        case ::beebium::Control::kChoice:         return "Choice";
        case ::beebium::Control::kTextInput:      return "TextInput";
        case ::beebium::Control::kGroup:          return "Group";
        case ::beebium::Control::kModalEditor:    return "ModalEditor";
        case ::beebium::Control::kEditableChoice: return "EditableChoice";
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
        case CC::kToggle:         expected = PC::kBoolValue;     break;
        case CC::kTextInput:      expected = PC::kStringValue;   break;
        case CC::kEditableChoice: expected = PC::kStringValue;   break;
        case CC::kChoice:         expected = PC::kIndexValue;    break;
        case CC::kButton:         expected = PC::PAYLOAD_NOT_SET; break;
        case CC::kModalEditor:    expected = PC::kEditorCommit;  break;
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

// Verify a single EditorFieldValue: its value variant must match the
// addressed sub-control's type. Only input types (Toggle, TextInput,
// Choice) are valid commit targets inside an editor tree.
inline bool editor_field_matches(
    const ::beebium::EditorFieldValue& field,
    ::beebium::Control::ControlCase ctrl_case,
    std::string& error)
{
    using FV = ::beebium::EditorFieldValue::ValueCase;
    using CC = ::beebium::Control::ControlCase;

    FV actual = field.value_case();
    FV expected;
    switch (ctrl_case) {
        case CC::kToggle:         expected = FV::kBoolValue;   break;
        case CC::kTextInput:      expected = FV::kStringValue; break;
        case CC::kEditableChoice: expected = FV::kStringValue; break;
        case CC::kChoice:         expected = FV::kIndexValue;  break;
        default:
            error = "editor field '" + field.field_id() +
                    "' addresses a non-input control (" +
                    control_type_name(ctrl_case) + ")";
            return false;
    }
    if (actual != expected) {
        error = "editor field '" + field.field_id() +
                "' value variant mismatches sub-control type " +
                control_type_name(ctrl_case);
        return false;
    }
    return true;
}

// Additional validation for a DispatchRequest whose target is a
// ModalEditor and whose payload is an EditorCommit. Checks (a) the
// ModalEditor is editable, (b) every EditorCommit.field_id names a
// sub-control in the editor tree, (c) each field's value variant
// matches the sub-control's type. All-or-nothing: any failure rejects
// the whole commit.
inline bool validate_editor_commit(
    const ::beebium::DispatchRequest& req,
    const ::beebium::ModalEditor& modal,
    std::string& error)
{
    if (!modal.editable()) {
        error = "ModalEditor '" + req.control_id() + "' is not editable";
        return false;
    }
    std::unordered_map<std::string, const ::beebium::Control*> sub_ids;
    collect_editor_fields(modal.editor(), sub_ids);
    for (const auto& field : req.editor_commit().fields()) {
        auto it = sub_ids.find(field.field_id());
        if (it == sub_ids.end()) {
            error = "unknown editor field id: " + field.field_id();
            return false;
        }
        if (!editor_field_matches(field, it->second->control_case(), error)) {
            return false;
        }
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

        std::unordered_map<std::string, const ::beebium::Control*> ids;
        detail::collect_control_ids(view.root(), ids);
        auto it = ids.find(request->control_id());
        if (it == ids.end()) {
            response->set_accepted(false);
            response->set_error(
                "unknown control id: " + request->control_id());
            return grpc::Status::OK;
        }

        const ::beebium::Control* target = it->second;
        std::string error;
        if (!detail::payload_matches_control(
                *request, target->control_case(), error)) {
            response->set_accepted(false);
            response->set_error(std::move(error));
            return grpc::Status::OK;
        }

        // ModalEditor carries additional structure that Dispatch must
        // validate before running handle_event: editable gate, every
        // EditorCommit field_id resolves to a sub-control, each value
        // variant matches its sub-control's type. All-or-nothing.
        if (target->control_case() == ::beebium::Control::kModalEditor) {
            if (!detail::validate_editor_commit(
                    *request, target->modal_editor(), error)) {
                response->set_accepted(false);
                response->set_error(std::move(error));
                return grpc::Status::OK;
            }
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
