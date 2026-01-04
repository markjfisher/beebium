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

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace beebium {

// Supported URL schemes for disc images
enum class DiscUrlScheme {
    File,       // file:// - local filesystem
    Unknown     // Unsupported or unrecognized scheme
};

// Represents a parsed disc image URL.
//
// Currently supports file: URLs for local filesystem access.
// Future: may support http:, https:, or custom schemes.
//
// Usage:
//   auto url = DiscUrl::parse("file:///path/to/disc.ssd");
//   if (url) {
//       auto path = url->to_filepath();
//   }
class DiscUrl {
public:
    // Parse a URL string.
    // Returns std::nullopt if URL is malformed or uses an unsupported scheme.
    //
    // Supported formats:
    //   file:///absolute/path    (Unix)
    //   file:///C:/path          (Windows)
    //   file://localhost/path    (explicit localhost)
    static std::optional<DiscUrl> parse(std::string_view url);

    // Create a DiscUrl from a filesystem path.
    // Convenience for converting bare paths to file: URLs.
    static DiscUrl from_filepath(const std::filesystem::path& filepath);

    // Get the URL scheme
    DiscUrlScheme scheme() const { return scheme_; }

    // Get the original URL string
    const std::string& url() const { return url_; }

    // For file: URLs, return the local filesystem path.
    // Throws std::logic_error if scheme is not File.
    std::filesystem::path to_filepath() const;

    // Check if this is a local file URL
    bool is_local() const { return scheme_ == DiscUrlScheme::File; }

private:
    DiscUrl(DiscUrlScheme scheme, std::string url, std::filesystem::path filepath)
        : scheme_(scheme), url_(std::move(url)), filepath_(std::move(filepath)) {}

    DiscUrlScheme scheme_;
    std::string url_;
    std::filesystem::path filepath_;  // Only valid for file: URLs
};

// Implementation

inline std::optional<DiscUrl> DiscUrl::parse(std::string_view url) {
    // Check for file: scheme
    constexpr std::string_view file_prefix = "file://";

    if (url.substr(0, file_prefix.size()) == file_prefix) {
        std::string_view remainder = url.substr(file_prefix.size());

        if (remainder.empty()) {
            return std::nullopt;  // Malformed: nothing after file://
        }

        // Handle file:///path (empty authority = localhost)
        if (remainder[0] == '/') {
            // On Unix: file:///path -> /path
            // On Windows: file:///C:/path -> C:/path (skip leading /)
#ifdef _WIN32
            // Skip leading / for Windows paths like file:///C:/...
            if (remainder.size() >= 3 && remainder[2] == ':') {
                remainder = remainder.substr(1);
            }
#endif
            std::filesystem::path path(remainder);
            return DiscUrl(DiscUrlScheme::File, std::string(url), path);
        }

        // Handle file://localhost/path or file://host/path
        auto slash_pos = remainder.find('/');
        if (slash_pos != std::string_view::npos) {
            std::string_view authority = remainder.substr(0, slash_pos);
            std::string_view path_part = remainder.substr(slash_pos);

            // Only accept localhost or empty authority
            if (authority == "localhost" || authority.empty()) {
                std::filesystem::path path(path_part);
                return DiscUrl(DiscUrlScheme::File, std::string(url), path);
            }
            // Non-localhost authority not supported
            return std::nullopt;
        }

        // No path component
        return std::nullopt;
    }

    // Unknown scheme
    return std::nullopt;
}

inline DiscUrl DiscUrl::from_filepath(const std::filesystem::path& filepath) {
    std::filesystem::path abs_path = std::filesystem::absolute(filepath);
    std::string url = "file://" + abs_path.string();
    return DiscUrl(DiscUrlScheme::File, std::move(url), abs_path);
}

inline std::filesystem::path DiscUrl::to_filepath() const {
    if (scheme_ != DiscUrlScheme::File) {
        throw std::logic_error("Cannot convert non-file URL to filepath: " + url_);
    }
    return filepath_;
}

} // namespace beebium
