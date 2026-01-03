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

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace beebium {

// Abstract interface for disc image storage.
//
// This interface abstracts the storage backend for disc images,
// allowing different implementations:
// - FileDiscImage: Local filesystem storage
// - UrlDiscImage: HTTP/HTTPS remote storage (read-only)
// - GrpcDiscImage: Client-provided storage via gRPC (future)
//
// All implementations must provide sector-level read/write access
// with geometry information.
class DiscImage {
public:
    virtual ~DiscImage() = default;

    // Metadata
    virtual std::string name() const = 0;
    virtual bool is_write_protected() const = 0;
    virtual void set_write_protected(bool write_protected) = 0;

    // Geometry
    virtual uint8_t sides() const = 0;              // 1 for SSD, 2 for DSD
    virtual uint8_t tracks_per_side() const = 0;    // 40 or 80
    virtual uint8_t sectors_per_track() const = 0;  // 10 for DFS
    virtual uint16_t sector_size() const = 0;       // 256 for DFS

    // Sector I/O
    // Returns true on success, false if parameters are invalid or write-protected.
    virtual bool read_sector(uint8_t side, uint8_t track, uint8_t sector,
                            std::span<uint8_t> buffer) = 0;
    virtual bool write_sector(uint8_t side, uint8_t track, uint8_t sector,
                             std::span<const uint8_t> buffer) = 0;

    // Flush any pending changes to backing store.
    // For file-based images, this writes to disk.
    // For remote images, this may be a no-op.
    virtual void flush() = 0;
};

} // namespace beebium
