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

#ifndef BEEBIUM_EXTENSION_EXTENSION_UI_HPP
#define BEEBIUM_EXTENSION_EXTENSION_UI_HPP

#include "Export.hpp"

#include <atomic>
#include <cstdint>

namespace beebium {

// Forward declarations for proto types defined in extension_ui.pb.h.
// Including the proto header here would force beebium_extension_api to
// link against the generated proto target; concrete extension UI
// implementations include extension_ui.pb.h in their own .cpp files.
class View;
class DispatchRequest;

// Server-driven UI hook for an extension. Implementors describe the
// extension's control panel as a typed View tree and respond to events
// the framework dispatches from clients.
//
// The framework owns view-revision plumbing, dispatch validation, and
// the streaming gRPC service. Implementors only need to:
//
//   * Build the current View when asked (a pure function of state).
//   * Handle events the framework has already validated.
//   * Call mark_dirty() when state changes for reasons other than an
//     incoming event (e.g. asynchronous status from a downstream
//     device).
class BEEBIUM_EXT_TYPE_VISIBLE ExtensionUi {
public:
    // BEEBIUM_EXT_TYPE_VISIBLE on the class makes the vtable +
    // typeinfo reachable from consumer shared libraries on POSIX
    // (it's a no-op on Windows). The destructor's BEEBIUM_EXT_API
    // is what anchors the vtable in the exporting DLL on Windows.
    // We deliberately don't use class-level dllimport on MSVC because
    // that makes inline-in-class methods emit as out-of-line imports,
    // which fail to link in consumer DLLs that don't link
    // beebium_extension_api (e.g. beebium_extension_ui_proto compiling
    // ExtensionUiServiceImpl). See
    // docs/discussion/grpc-windows-streaming-race.md.
    BEEBIUM_EXT_API virtual ~ExtensionUi();

    // Populate `out` with the extension's current control tree. The
    // implementation should set Control ids, oneof payloads, and any
    // text/state fields it owns. The framework stamps view_revision
    // and extension_id itself; implementations should leave those
    // fields alone.
    virtual void build_view(View* out) const = 0;

    // Handle a user action. Called only after the framework has
    // validated the request (extension exists, control id is known for
    // the current view, payload variant matches the control type, view
    // revision is current). May mutate state synchronously; the next
    // poll iteration will rebuild and push the resulting View.
    //
    // Implementations may call mark_dirty() inside handle_event, but
    // are not required to -- the framework bumps the revision after
    // handle_event returns regardless, so the next poll picks up the
    // change either way.
    virtual void handle_event(const DispatchRequest& request) = 0;

    // Read the current revision (used by the framework's poll loop
    // and Dispatch validation gauntlet). Initial value is 1, so the
    // very first push from a fresh subscription has revision 1.
    std::uint64_t current_revision() const noexcept {
        return revision_.load(std::memory_order_acquire);
    }

    // Notify the framework that the view has changed. Bumps the
    // revision counter; the next poll iteration will rebuild and push.
    // Safe to call from any thread.
    void mark_dirty() noexcept {
        revision_.fetch_add(1, std::memory_order_acq_rel);
    }

private:
    std::atomic<std::uint64_t> revision_{1};
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_EXTENSION_UI_HPP
