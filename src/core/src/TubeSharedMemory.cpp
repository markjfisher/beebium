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

#include <beebium/tube/TubeSharedMemory.hpp>

#include <cstring>
#include <stdexcept>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace beebium {

static size_t page_size() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}

static size_t round_to_page(size_t n) {
    size_t ps = page_size();
    return (n + ps - 1) & ~(ps - 1);
}

TubeSharedMemory::TubeSharedMemory(const std::string& suffix, TubeSharedMemoryRole role)
    : shm_name_("beebium_tube_" + suffix)
    , role_(role)
    , mapping_size_(round_to_page(sizeof(TubeShared)))
{
    if (role_ == TubeSharedMemoryRole::Creator) {
        file_mapping_handle_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, static_cast<DWORD>(mapping_size_), shm_name_.c_str());
        if (!file_mapping_handle_)
            throw std::runtime_error("CreateFileMapping failed for " + shm_name_);
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(file_mapping_handle_);
            file_mapping_handle_ = nullptr;
            throw std::runtime_error("Shared memory already exists: " + shm_name_);
        }
    } else {
        file_mapping_handle_ = OpenFileMappingA(
            FILE_MAP_ALL_ACCESS, FALSE, shm_name_.c_str());
        if (!file_mapping_handle_)
            throw std::runtime_error("OpenFileMapping failed for " + shm_name_);
    }

    void* ptr = MapViewOfFile(file_mapping_handle_, FILE_MAP_ALL_ACCESS,
                              0, 0, mapping_size_);
    if (!ptr) {
        CloseHandle(file_mapping_handle_);
        file_mapping_handle_ = nullptr;
        throw std::runtime_error("MapViewOfFile failed for " + shm_name_);
    }

    mapped_ = static_cast<TubeShared*>(ptr);

    if (role_ == TubeSharedMemoryRole::Creator) {
        mapped_->init();
    } else {
        if (!mapped_->valid())
            throw std::runtime_error("Invalid TubeShared header in " + shm_name_);
    }
}

TubeSharedMemory::~TubeSharedMemory() {
    if (mapped_) {
        UnmapViewOfFile(mapped_);
        mapped_ = nullptr;
    }
    if (file_mapping_handle_) {
        CloseHandle(file_mapping_handle_);
        file_mapping_handle_ = nullptr;
    }
}

}  // namespace beebium

#else  // POSIX (macOS, Linux)

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace beebium {

static size_t round_to_page(size_t n) {
    long ps = sysconf(_SC_PAGESIZE);
    size_t page_size = (ps > 0) ? static_cast<size_t>(ps) : 4096;
    return (n + page_size - 1) & ~(page_size - 1);
}

TubeSharedMemory::TubeSharedMemory(const std::string& suffix, TubeSharedMemoryRole role)
    : shm_name_("/" + suffix)
    , role_(role)
    , mapping_size_(round_to_page(sizeof(TubeShared)))
{
    int flags = O_RDWR;
    if (role_ == TubeSharedMemoryRole::Creator)
        flags |= O_CREAT | O_EXCL;

    shm_fd_ = shm_open(shm_name_.c_str(), flags, 0600);
    if (shm_fd_ < 0)
        throw std::runtime_error("shm_open failed for " + shm_name_ + ": "
                                 + std::string(strerror(errno)));

    if (role_ == TubeSharedMemoryRole::Creator) {
        if (ftruncate(shm_fd_, static_cast<off_t>(mapping_size_)) != 0) {
            close(shm_fd_);
            shm_unlink(shm_name_.c_str());
            throw std::runtime_error("ftruncate failed for " + shm_name_);
        }
    }

    void* ptr = mmap(nullptr, mapping_size_, PROT_READ | PROT_WRITE,
                     MAP_SHARED, shm_fd_, 0);
    if (ptr == MAP_FAILED) {
        close(shm_fd_);
        if (role_ == TubeSharedMemoryRole::Creator)
            shm_unlink(shm_name_.c_str());
        throw std::runtime_error("mmap failed for " + shm_name_);
    }

    mapped_ = static_cast<TubeShared*>(ptr);

    if (role_ == TubeSharedMemoryRole::Creator) {
        mapped_->init();
    } else {
        if (!mapped_->valid()) {
            munmap(mapped_, mapping_size_);
            close(shm_fd_);
            mapped_ = nullptr;
            throw std::runtime_error("Invalid TubeShared header in " + shm_name_);
        }
    }
}

TubeSharedMemory::~TubeSharedMemory() {
    if (mapped_) {
        munmap(mapped_, mapping_size_);
        mapped_ = nullptr;
    }
    if (shm_fd_ >= 0) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
    if (role_ == TubeSharedMemoryRole::Creator) {
        shm_unlink(shm_name_.c_str());
    }
}

}  // namespace beebium

#endif
