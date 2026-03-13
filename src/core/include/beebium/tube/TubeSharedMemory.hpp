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

#include "TubeShared.hpp"

#include <cstddef>
#include <string>

namespace beebium {

// Role of a process in the shared memory lifecycle.
enum class TubeSharedMemoryRole {
    Creator,    // Host process: creates, initialises, and unlinks on destruction
    Joiner      // Parasite process: opens existing region, validates header
};

// RAII manager for a POSIX shared memory region containing TubeShared.
//
// The Creator (host) allocates and initialises the region; the Joiner
// (parasite) opens an existing region by name and validates its header.
// Both sides unmap on destruction; the Creator also unlinks the name.
//
// The shared memory name is constructed as "/beebium_tube_<suffix>" where
// the suffix is provided by the caller (typically a UUID or PID).
//
// Usage (host):
//   TubeSharedMemory shm("abc123", TubeSharedMemoryRole::Creator);
//   TubeSocket socket;
//   socket.enable(shm.get());
//
// Usage (parasite):
//   TubeSharedMemory shm("abc123", TubeSharedMemoryRole::Joiner);
//   ParasiteRunner runner(shm.get(), rom);

class TubeSharedMemory {
public:
    // Create or open a shared memory region.
    // Throws std::runtime_error on failure.
    TubeSharedMemory(const std::string& suffix, TubeSharedMemoryRole role);

    ~TubeSharedMemory();

    // Non-copyable, non-movable (unique OS resource ownership).
    TubeSharedMemory(const TubeSharedMemory&) = delete;
    TubeSharedMemory& operator=(const TubeSharedMemory&) = delete;
    TubeSharedMemory(TubeSharedMemory&&) = delete;
    TubeSharedMemory& operator=(TubeSharedMemory&&) = delete;

    // Access the mapped TubeShared region.
    TubeShared* get() const { return mapped_; }
    TubeShared* operator->() const { return mapped_; }
    TubeShared& operator*() const { return *mapped_; }

    // The full POSIX shared memory name (e.g. "/beebium_tube_abc123").
    const std::string& name() const { return shm_name_; }

    // The role of this process.
    TubeSharedMemoryRole role() const { return role_; }

    // The mapping size (>= sizeof(TubeShared), rounded up to page boundary).
    size_t size() const { return mapping_size_; }

private:
    std::string shm_name_;
    TubeSharedMemoryRole role_;
    TubeShared* mapped_ = nullptr;
    size_t mapping_size_ = 0;

#ifdef _WIN32
    void* file_mapping_handle_ = nullptr;
#else
    int shm_fd_ = -1;
#endif
};

}  // namespace beebium
