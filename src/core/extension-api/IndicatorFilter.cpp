// Copyright (c) 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Vtable anchors for the non-template IndicatorFilter classes.
//
// Defining the virtual destructors out-of-line in beebium_extension_api
// (which is a SHARED library loaded for the lifetime of the process)
// pins each vtable to a single shared object. Without this anchor every
// translation unit that instantiates a filter -- including plugin DLLs
// like scsi-hard-disc.dylib -- emits its own weak vtable. When the host
// machine's Indicators registry holds a unique_ptr<IndicatorFilter>
// constructed inside a plugin, dlclose() of that plugin at server
// shutdown unmaps the plugin's vtable; destroying the unique_ptr
// afterwards then dispatches a virtual destructor through unmapped
// memory and the process crashes (issue #42).
//
// Template filters (e.g. QuantizedDutyCycleFilter<N>) are not anchored
// here because they have per-instantiation vtables. They're currently
// only instantiated inside the host machine (SystemViaPeripheral.hpp),
// not by plugins, so the issue does not apply. If a future plugin
// constructs one, add an explicit instantiation in this file.

#include "beebium/indicators/IndicatorFilter.hpp"

namespace beebium {

IndicatorFilter::~IndicatorFilter() = default;
PassthroughFilter::~PassthroughFilter() = default;
DebounceFilter::~DebounceFilter() = default;
DutyCycleFilter::~DutyCycleFilter() = default;
RetriggerableMonostableFilter::~RetriggerableMonostableFilter() = default;

}  // namespace beebium
