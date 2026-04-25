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

// Schema-level round-trip tests for extension_ui.proto. These exercise
// only the protobuf wire format: hand-build a View / DispatchRequest,
// serialize, parse back, and assert structural equality. The framework
// behaviour (subscribe streams, dispatch validation) is covered by
// test_grpc_extension_ui_service.cpp in stage 2.

#include <catch2/catch_test_macros.hpp>

#include "extension_ui.pb.h"

#include <cstdint>
#include <limits>
#include <string>

namespace {

// Build a moderately deep View: Group(Group(Toggle, Button), Label).
// Picks one of the structural primitives (nested Group) plus a couple of
// leaves so we exercise the recursive Control oneof through one round
// trip.
beebium::View make_nested_view() {
    beebium::View view;
    view.set_extension_name("test");
    view.set_view_revision(7);

    auto* root = view.mutable_root();
    root->set_id("root");
    auto* root_group = root->mutable_group();
    root_group->set_label("Outer");

    auto* inner = root_group->add_controls();
    inner->set_id("inner");
    auto* inner_group = inner->mutable_group();
    // Deliberately omit inner_group->label so we test the absent-label
    // case for the optional field.

    auto* toggle = inner_group->add_controls();
    toggle->set_id("toggle1");
    auto* toggle_msg = toggle->mutable_toggle();
    toggle_msg->set_label("Enabled");
    toggle_msg->set_value(true);

    auto* button = inner_group->add_controls();
    button->set_id("button1");
    auto* button_msg = button->mutable_button();
    button_msg->set_label("Restart");
    button_msg->set_enabled(true);

    auto* label = root_group->add_controls();
    label->set_id("label1");
    label->mutable_label()->set_text("Hello");

    return view;
}

}  // namespace

TEST_CASE("View with nested Group(Group(Toggle, Button), Label) round-trips",
          "[extension_ui][proto]") {
    auto original = make_nested_view();

    std::string wire;
    REQUIRE(original.SerializeToString(&wire));

    beebium::View parsed;
    REQUIRE(parsed.ParseFromString(wire));

    REQUIRE(parsed.extension_name() == "test");
    REQUIRE(parsed.view_revision() == 7u);

    const auto& root = parsed.root();
    REQUIRE(root.id() == "root");
    REQUIRE(root.control_case() == beebium::Control::kGroup);
    REQUIRE(root.group().label() == "Outer");
    REQUIRE(root.group().has_label());
    REQUIRE(root.group().controls_size() == 2);

    const auto& inner = root.group().controls(0);
    REQUIRE(inner.id() == "inner");
    REQUIRE(inner.control_case() == beebium::Control::kGroup);
    REQUIRE_FALSE(inner.group().has_label());
    REQUIRE(inner.group().controls_size() == 2);

    const auto& toggle = inner.group().controls(0);
    REQUIRE(toggle.id() == "toggle1");
    REQUIRE(toggle.control_case() == beebium::Control::kToggle);
    REQUIRE(toggle.toggle().label() == "Enabled");
    REQUIRE(toggle.toggle().value() == true);

    const auto& button = inner.group().controls(1);
    REQUIRE(button.id() == "button1");
    REQUIRE(button.control_case() == beebium::Control::kButton);
    REQUIRE(button.button().label() == "Restart");
    REQUIRE(button.button().enabled() == true);

    const auto& label = root.group().controls(1);
    REQUIRE(label.id() == "label1");
    REQUIRE(label.control_case() == beebium::Control::kLabel);
    REQUIRE(label.label().text() == "Hello");
}

TEST_CASE("Indicator round-trips with non-default state", "[extension_ui][proto]") {
    beebium::Control control;
    control.set_id("ind");
    auto* indicator = control.mutable_indicator();
    indicator->set_state(beebium::Indicator_State_ERROR);
    indicator->set_text("Adapter offline");

    std::string wire;
    REQUIRE(control.SerializeToString(&wire));

    beebium::Control parsed;
    REQUIRE(parsed.ParseFromString(wire));

    REQUIRE(parsed.id() == "ind");
    REQUIRE(parsed.control_case() == beebium::Control::kIndicator);
    REQUIRE(parsed.indicator().state() == beebium::Indicator_State_ERROR);
    REQUIRE(parsed.indicator().text() == "Adapter offline");
}

TEST_CASE("Choice and TextInput round-trip with their fields populated",
          "[extension_ui][proto]") {
    beebium::Control choice;
    choice.set_id("c");
    auto* choice_msg = choice.mutable_choice();
    choice_msg->set_label("Mode");
    choice_msg->add_options("Stop");
    choice_msg->add_options("Listen");
    choice_msg->add_options("Monitor");
    choice_msg->set_selected_index(1);

    std::string wire;
    REQUIRE(choice.SerializeToString(&wire));

    beebium::Control parsed;
    REQUIRE(parsed.ParseFromString(wire));
    REQUIRE(parsed.control_case() == beebium::Control::kChoice);
    REQUIRE(parsed.choice().label() == "Mode");
    REQUIRE(parsed.choice().options_size() == 3);
    REQUIRE(parsed.choice().options(0) == "Stop");
    REQUIRE(parsed.choice().options(1) == "Listen");
    REQUIRE(parsed.choice().options(2) == "Monitor");
    REQUIRE(parsed.choice().selected_index() == 1u);

    beebium::Control text_input;
    text_input.set_id("t");
    auto* ti = text_input.mutable_text_input();
    ti->set_label("Host");
    ti->set_value("127.0.0.1");
    ti->set_placeholder("ip");

    std::string ti_wire;
    REQUIRE(text_input.SerializeToString(&ti_wire));

    beebium::Control ti_parsed;
    REQUIRE(ti_parsed.ParseFromString(ti_wire));
    REQUIRE(ti_parsed.control_case() == beebium::Control::kTextInput);
    REQUIRE(ti_parsed.text_input().label() == "Host");
    REQUIRE(ti_parsed.text_input().value() == "127.0.0.1");
    REQUIRE(ti_parsed.text_input().placeholder() == "ip");
}

TEST_CASE("DispatchRequest round-trips each payload variant",
          "[extension_ui][proto]") {
    SECTION("bool_value (Toggle)") {
        beebium::DispatchRequest req;
        req.set_extension_name("piconet");
        req.set_control_id("mode_toggle");
        req.set_view_revision(42);
        req.set_bool_value(true);

        std::string wire;
        REQUIRE(req.SerializeToString(&wire));

        beebium::DispatchRequest parsed;
        REQUIRE(parsed.ParseFromString(wire));
        REQUIRE(parsed.extension_name() == "piconet");
        REQUIRE(parsed.control_id() == "mode_toggle");
        REQUIRE(parsed.view_revision() == 42u);
        REQUIRE(parsed.payload_case() == beebium::DispatchRequest::kBoolValue);
        REQUIRE(parsed.bool_value() == true);
    }

    SECTION("string_value (TextInput)") {
        beebium::DispatchRequest req;
        req.set_extension_name("aun");
        req.set_control_id("peer_host");
        req.set_view_revision(1);
        req.set_string_value("127.0.0.1");

        std::string wire;
        REQUIRE(req.SerializeToString(&wire));

        beebium::DispatchRequest parsed;
        REQUIRE(parsed.ParseFromString(wire));
        REQUIRE(parsed.payload_case() == beebium::DispatchRequest::kStringValue);
        REQUIRE(parsed.string_value() == "127.0.0.1");
    }

    SECTION("index_value (Choice)") {
        beebium::DispatchRequest req;
        req.set_extension_name("piconet");
        req.set_control_id("mode_select");
        req.set_view_revision(99);
        req.set_index_value(2);

        std::string wire;
        REQUIRE(req.SerializeToString(&wire));

        beebium::DispatchRequest parsed;
        REQUIRE(parsed.ParseFromString(wire));
        REQUIRE(parsed.payload_case() == beebium::DispatchRequest::kIndexValue);
        REQUIRE(parsed.index_value() == 2u);
    }

    SECTION("no payload (Button)") {
        beebium::DispatchRequest req;
        req.set_extension_name("aun");
        req.set_control_id("add_peer");
        req.set_view_revision(5);
        // payload oneof intentionally not set

        std::string wire;
        REQUIRE(req.SerializeToString(&wire));

        beebium::DispatchRequest parsed;
        REQUIRE(parsed.ParseFromString(wire));
        REQUIRE(parsed.payload_case() == beebium::DispatchRequest::PAYLOAD_NOT_SET);
    }
}

TEST_CASE("View with view_revision = UINT64_MAX serializes correctly (boundary)",
          "[extension_ui][proto]") {
    beebium::View view;
    view.set_extension_name("boundary");
    view.set_view_revision(std::numeric_limits<std::uint64_t>::max());
    view.mutable_root()->set_id("r");
    view.mutable_root()->mutable_label()->set_text("ok");

    std::string wire;
    REQUIRE(view.SerializeToString(&wire));

    beebium::View parsed;
    REQUIRE(parsed.ParseFromString(wire));
    REQUIRE(parsed.view_revision() == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("ModalEditor round-trips with anchor, editor, and flags",
          "[extension_ui][proto][modal_editor]") {
    beebium::Control control;
    control.set_id("device_path");
    auto* modal = control.mutable_modal_editor();
    modal->set_editable(true);
    modal->set_commit_role(beebium::ModalEditor_CommitRole_SAVE);
    modal->set_show_cancel(true);

    auto* anchor = modal->mutable_anchor();
    anchor->set_id("device_path");
    anchor->mutable_label()->set_text("Device: /dev/tty.usbmodem101");

    // Editor is a single EditableChoice with enumerated options.
    auto* editor = modal->mutable_editor();
    editor->set_id("device_path_value");
    auto* ec = editor->mutable_editable_choice();
    ec->set_label("Serial port");
    ec->set_value("/dev/tty.usbmodem101");
    *ec->add_options() = "/dev/tty.usbmodem101";
    *ec->add_options() = "/dev/tty.usbmodem14101";

    std::string wire;
    REQUIRE(control.SerializeToString(&wire));

    beebium::Control parsed;
    REQUIRE(parsed.ParseFromString(wire));
    REQUIRE(parsed.control_case() == beebium::Control::kModalEditor);
    const auto& pm = parsed.modal_editor();
    REQUIRE(pm.editable() == true);
    REQUIRE(pm.commit_role() == beebium::ModalEditor_CommitRole_SAVE);
    REQUIRE(pm.show_cancel() == true);
    REQUIRE(pm.anchor().control_case() == beebium::Control::kLabel);
    REQUIRE(pm.anchor().label().text() == "Device: /dev/tty.usbmodem101");
    REQUIRE(pm.editor().control_case() == beebium::Control::kEditableChoice);
    REQUIRE(pm.editor().id() == "device_path_value");
    REQUIRE(pm.editor().editable_choice().value() == "/dev/tty.usbmodem101");
    REQUIRE(pm.editor().editable_choice().options_size() == 2);
    REQUIRE(pm.editor().editable_choice().options(0) == "/dev/tty.usbmodem101");
    REQUIRE(pm.editor().editable_choice().options(1) == "/dev/tty.usbmodem14101");
}

TEST_CASE("EditableChoice round-trips with options and value",
          "[extension_ui][proto][editable_choice]") {
    beebium::Control control;
    control.set_id("device_path_value");
    auto* ec = control.mutable_editable_choice();
    ec->set_label("Serial port");
    ec->set_value("/dev/tty.usbmodem101");
    ec->set_placeholder("/dev/tty.usbmodem...");
    *ec->add_options() = "/dev/tty.usbmodem101";
    *ec->add_options() = "/dev/tty.usbmodem14101";

    std::string wire;
    REQUIRE(control.SerializeToString(&wire));

    beebium::Control parsed;
    REQUIRE(parsed.ParseFromString(wire));
    REQUIRE(parsed.control_case() == beebium::Control::kEditableChoice);
    const auto& pec = parsed.editable_choice();
    REQUIRE(pec.label() == "Serial port");
    REQUIRE(pec.value() == "/dev/tty.usbmodem101");
    REQUIRE(pec.options_size() == 2);
    REQUIRE(pec.options(0) == "/dev/tty.usbmodem101");
    REQUIRE(pec.options(1) == "/dev/tty.usbmodem14101");
}

TEST_CASE("DispatchRequest round-trips an EditorCommit with mixed field types",
          "[extension_ui][proto][modal_editor]") {
    beebium::DispatchRequest req;
    req.set_extension_name("piconet");
    req.set_control_id("device_path");
    req.set_view_revision(7);
    auto* commit = req.mutable_editor_commit();

    {
        auto* f = commit->add_fields();
        f->set_field_id("port_choice");
        f->set_index_value(1);
    }
    {
        auto* f = commit->add_fields();
        f->set_field_id("custom_path");
        f->set_string_value("/dev/tty.usbserial-A1");
    }
    {
        auto* f = commit->add_fields();
        f->set_field_id("include_unlisted");
        f->set_bool_value(true);
    }

    std::string wire;
    REQUIRE(req.SerializeToString(&wire));

    beebium::DispatchRequest parsed;
    REQUIRE(parsed.ParseFromString(wire));
    REQUIRE(parsed.payload_case() == beebium::DispatchRequest::kEditorCommit);
    REQUIRE(parsed.editor_commit().fields_size() == 3);

    const auto& f0 = parsed.editor_commit().fields(0);
    REQUIRE(f0.field_id() == "port_choice");
    REQUIRE(f0.value_case() == beebium::EditorFieldValue::kIndexValue);
    REQUIRE(f0.index_value() == 1u);

    const auto& f1 = parsed.editor_commit().fields(1);
    REQUIRE(f1.field_id() == "custom_path");
    REQUIRE(f1.value_case() == beebium::EditorFieldValue::kStringValue);
    REQUIRE(f1.string_value() == "/dev/tty.usbserial-A1");

    const auto& f2 = parsed.editor_commit().fields(2);
    REQUIRE(f2.field_id() == "include_unlisted");
    REQUIRE(f2.value_case() == beebium::EditorFieldValue::kBoolValue);
    REQUIRE(f2.bool_value() == true);
}

TEST_CASE("DispatchResponse round-trips both accepted and error cases",
          "[extension_ui][proto]") {
    beebium::DispatchResponse ok;
    ok.set_accepted(true);
    std::string wire;
    REQUIRE(ok.SerializeToString(&wire));

    beebium::DispatchResponse parsed;
    REQUIRE(parsed.ParseFromString(wire));
    REQUIRE(parsed.accepted() == true);
    REQUIRE(parsed.error().empty());

    beebium::DispatchResponse rejected;
    rejected.set_accepted(false);
    rejected.set_error("stale event: client revision 3, current 7");

    std::string err_wire;
    REQUIRE(rejected.SerializeToString(&err_wire));

    beebium::DispatchResponse err_parsed;
    REQUIRE(err_parsed.ParseFromString(err_wire));
    REQUIRE(err_parsed.accepted() == false);
    REQUIRE(err_parsed.error() == "stale event: client revision 3, current 7");
}
