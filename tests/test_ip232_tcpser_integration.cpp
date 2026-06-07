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

// Opt-in interop test against a REAL tcpser virtual modem -- the authoritative
// IP232 peer. It SKIPs cleanly when tcpser is not available, so the normal suite
// stays green without it; run it where tcpser is installed (set BEEBIUM_TCPSER
// to the binary, or put tcpser on PATH). It is POSIX-only (tcpser is a Unix
// tool); on Windows it skips.
//
// What it proves: the endpoint connects to a real tcpser over TCP and exchanges
// bytes through the IP232 framing (a bare AT command elicits a modem response).
// Exact wire-byte correctness lives in the codec golden-vector test; this is the
// end-to-end interop smoke.

#include "Ip232SerialEndpoint.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace beebium;
using namespace std::chrono_literals;

namespace {

template <typename Predicate>
bool wait_until(Predicate pred, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return pred();
}

#ifndef _WIN32

// Locate tcpser: $BEEBIUM_TCPSER if set and executable, else "tcpser" on PATH.
std::optional<std::string> find_tcpser() {
    if (const char* env = std::getenv("BEEBIUM_TCPSER")) {
        if (env[0] != '\0' && ::access(env, X_OK) == 0) return std::string(env);
    }
    const char* path = std::getenv("PATH");
    if (path == nullptr) return std::nullopt;
    std::string dirs(path);
    std::size_t start = 0;
    while (start <= dirs.size()) {
        std::size_t colon = dirs.find(':', start);
        std::string dir = dirs.substr(
            start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!dir.empty()) {
            std::string candidate = dir + "/tcpser";
            if (::access(candidate.c_str(), X_OK) == 0) return candidate;
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return std::nullopt;
}

// An ephemeral port that is free at the moment of the call (bind, read, close).
std::uint16_t free_port() {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    std::uint16_t port = ntohs(bound.sin_port);
    ::close(s);
    return port;
}

// Launch tcpser listening on `port`. Returns its pid, or -1 on fork failure.
pid_t launch_tcpser(const std::string& bin, std::uint16_t port) {
    pid_t pid = ::fork();
    if (pid == 0) {
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
        }
        const std::string port_str = std::to_string(port);
        ::execlp(bin.c_str(), "tcpser", "-v", port_str.c_str(), "-s", "19200",
                 static_cast<char*>(nullptr));
        _exit(127);  // exec failed
    }
    return pid;
}

#endif  // !_WIN32

}  // namespace

TEST_CASE("ip232-serial interoperates with a real tcpser", "[ip232][tcpser]") {
#ifdef _WIN32
    SKIP("tcpser integration is POSIX-only");
#else
    std::optional<std::string> bin = find_tcpser();
    if (!bin) {
        SKIP("tcpser not found (set BEEBIUM_TCPSER or put tcpser on PATH)");
    }

    const std::uint16_t port = free_port();
    pid_t pid = launch_tcpser(*bin, port);
    REQUIRE(pid > 0);
    // Tear tcpser down however the test exits.
    struct Reaper {
        pid_t pid;
        ~Reaper() {
            ::kill(pid, SIGTERM);
            ::waitpid(pid, nullptr, 0);
        }
    } reaper{pid};

    ip232::Ip232SerialEndpoint::Options options;
    options.host = "127.0.0.1";
    options.port = port;
    options.raw = false;  // ip232 mode
    ip232::Ip232SerialEndpoint endpoint(std::move(options));

    // tcpser needs a moment to bind; the endpoint retries with backoff.
    REQUIRE(wait_until([&] { return endpoint.connected(); }, 8s));

    // A bare AT command should make the virtual modem respond.
    for (char c : std::string("AT\r")) {
        endpoint.add_byte(static_cast<std::uint8_t>(c));
    }

    std::string response;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline &&
           response.find("OK") == std::string::npos) {
        if (endpoint.has_data()) {
            response.push_back(static_cast<char>(endpoint.next_byte()));
        } else {
            std::this_thread::sleep_for(5ms);
        }
    }

    INFO("tcpser response: " << response);
    CHECK_FALSE(response.empty());  // the modem replied through the IP232 framing
#endif
}
