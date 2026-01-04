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

#include "DiscImage.hpp"
#include "DiscUrl.hpp"
#include "FileDiscImage.hpp"

#include <memory>
#include <string>

namespace beebium {

// Result of attempting to load a disc image from a URL.
// Check success() before accessing the image.
struct DiscLoadResult {
    std::unique_ptr<DiscImage> image;  // nullptr on error
    std::string error;                  // Empty on success

    bool success() const { return image != nullptr; }
    explicit operator bool() const { return success(); }
};

// Load a disc image from a URL.
//
// Currently supports file: URLs for local filesystem access.
// Returns a DiscLoadResult containing either the loaded image or an error message.
//
// Usage:
//   auto result = load_disc_from_url("file:///path/to/disc.ssd");
//   if (result) {
//       drive.insert(std::move(result.image));
//   } else {
//       std::cerr << "Error: " << result.error << "\n";
//   }
inline DiscLoadResult load_disc_from_url(const std::string& url) {
    // Parse the URL
    auto parsed = DiscUrl::parse(url);
    if (!parsed) {
        return {nullptr, "Invalid or unsupported URL: " + url};
    }

    // Dispatch based on scheme
    switch (parsed->scheme()) {
        case DiscUrlScheme::File: {
            auto filepath = parsed->to_filepath();
            try {
                auto image = FileDiscImage::load(filepath);
                return {std::move(image), ""};
            } catch (const std::exception& e) {
                return {nullptr, e.what()};
            }
        }

        case DiscUrlScheme::Unknown:
        default:
            return {nullptr, "Unsupported URL scheme: " + url};
    }
}

// Load a disc image from a URL or bare filepath.
// If the input doesn't look like a URL (no :// found), treats it as a local filepath
// and converts to a file: URL internally.
//
// This provides backward compatibility with code that uses bare filepaths.
inline DiscLoadResult load_disc_from_url_or_filepath(const std::string& url_or_filepath) {
    // If it looks like a URL (contains ://), parse as URL
    if (url_or_filepath.find("://") != std::string::npos) {
        return load_disc_from_url(url_or_filepath);
    }

    // Otherwise, treat as bare filepath and convert to file: URL
    std::filesystem::path filepath(url_or_filepath);
    auto disc_url = DiscUrl::from_filepath(filepath);
    return load_disc_from_url(disc_url.url());
}

} // namespace beebium
