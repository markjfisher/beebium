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

#ifndef BEEBIUM_SERVER_SUBPROCESS_MANAGER_HPP
#define BEEBIUM_SERVER_SUBPROCESS_MANAGER_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace beebium::server {

// Lightweight handle to a child process.
//
// On POSIX: wraps a pid_t.
// On Windows: wraps a HANDLE to the process.
// Non-copyable, movable. Destructor does NOT terminate the child.

class Subprocess {
public:
#ifdef _WIN32
    explicit Subprocess(HANDLE handle) : handle_(handle) {}
    Subprocess() : handle_(INVALID_HANDLE_VALUE) {}

    ~Subprocess() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    Subprocess(Subprocess&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }
    Subprocess& operator=(Subprocess&& other) noexcept {
        if (this != &other) {
            if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }

    bool is_alive() const {
        if (!valid()) return false;
        DWORD exit_code;
        if (GetExitCodeProcess(handle_, &exit_code)) {
            return exit_code == STILL_ACTIVE;
        }
        return false;
    }

    void terminate() {
        if (valid()) {
            TerminateProcess(handle_, 1);
        }
    }

    bool wait(int timeout_ms = -1) {
        if (!valid()) return true;
        DWORD result = WaitForSingleObject(handle_,
            timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms));
        return result == WAIT_OBJECT_0;
    }

private:
    HANDLE handle_;
#else
    explicit Subprocess(pid_t pid) : pid_(pid) {}
    Subprocess() : pid_(-1) {}

    ~Subprocess() = default;

    Subprocess(Subprocess&& other) noexcept : pid_(other.pid_) {
        other.pid_ = -1;
    }
    Subprocess& operator=(Subprocess&& other) noexcept {
        pid_ = other.pid_;
        other.pid_ = -1;
        return *this;
    }

    bool valid() const { return pid_ > 0; }

    bool is_alive() const {
        if (!valid()) return false;
        int status;
        pid_t result = waitpid(pid_, &status, WNOHANG);
        return result == 0;  // 0 = still running
    }

    void terminate() {
        if (valid()) {
            kill(pid_, SIGTERM);
        }
    }

    bool wait(int timeout_ms = -1) {
        if (!valid()) return true;
        if (timeout_ms < 0) {
            int status;
            waitpid(pid_, &status, 0);
            return true;
        }
        // Poll with short sleeps
        int elapsed = 0;
        while (elapsed < timeout_ms) {
            int status;
            pid_t result = waitpid(pid_, &status, WNOHANG);
            if (result != 0) return true;  // exited or error
            usleep(10000);  // 10ms
            elapsed += 10;
        }
        return false;
    }

    pid_t pid() const { return pid_; }

private:
    pid_t pid_;
#endif

    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;
};

// Find an executable by name, searching:
// 1. The same directory as the host executable (sibling lookup)
// 2. PATH
//
// Returns the full path if found, nullopt otherwise.
inline std::optional<std::filesystem::path> find_sibling_executable(
    const std::string& executable_name,
    const std::filesystem::path& host_executable_filepath)
{
    namespace fs = std::filesystem;

    // Try sibling directory first
    auto sibling = host_executable_filepath.parent_path() / executable_name;
    if (fs::exists(sibling) && fs::is_regular_file(sibling)) {
        return sibling;
    }

    // Fall back to PATH search
    const char* path_env = std::getenv("PATH");
    if (!path_env) return std::nullopt;

    std::string path_str(path_env);
#ifdef _WIN32
    char delimiter = ';';
#else
    char delimiter = ':';
#endif

    size_t start = 0;
    while (start < path_str.size()) {
        size_t end = path_str.find(delimiter, start);
        if (end == std::string::npos) end = path_str.size();

        auto dirpath = fs::path(path_str.substr(start, end - start));
        auto candidate = dirpath / executable_name;
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate;
        }

        start = end + 1;
    }

    return std::nullopt;
}

// Spawn a child process with the given arguments.
// argv[0] should be the executable path.
// Returns the Subprocess handle on success, or nullopt on failure.
inline std::optional<Subprocess> spawn_subprocess(
    const std::filesystem::path& executable_filepath,
    const std::vector<std::string>& args)
{
#ifdef _WIN32
    // Build command line string
    std::string cmd_line;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmd_line += ' ';
        // Quote arguments that contain spaces
        if (args[i].find(' ') != std::string::npos) {
            cmd_line += '"' + args[i] + '"';
        } else {
            cmd_line += args[i];
        }
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(
            executable_filepath.string().c_str(),
            cmd_line.data(),
            nullptr, nullptr,
            FALSE,
            0,
            nullptr, nullptr,
            &si, &pi)) {
        return std::nullopt;
    }

    CloseHandle(pi.hThread);
    return Subprocess(pi.hProcess);
#else
    pid_t pid = fork();
    if (pid < 0) {
        return std::nullopt;
    }

    if (pid == 0) {
        // Child process
        std::vector<const char*> c_args;
        c_args.reserve(args.size() + 1);
        for (const auto& arg : args) {
            c_args.push_back(arg.c_str());
        }
        c_args.push_back(nullptr);

        execv(executable_filepath.string().c_str(),
              const_cast<char* const*>(c_args.data()));

        // execv only returns on error
        _exit(127);
    }

    // Parent process
    return Subprocess(pid);
#endif
}

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_SUBPROCESS_MANAGER_HPP
