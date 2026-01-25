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

#ifndef BEEBIUM_SERVICE_SHUTDOWN_POLICY_HPP
#define BEEBIUM_SERVICE_SHUTDOWN_POLICY_HPP

#include <string>
#include <utility>

namespace beebium::service {

/// Configuration for shutdown policy.
///
/// This is typically populated from command-line arguments.
struct ShutdownPolicyConfig {
    /// If true, any client can shut down the server regardless of provenance match
    /// or client count. Set via --allow-shutdown flag.
    bool allow_shutdown = false;
};

/// Evaluates whether a shutdown request should be accepted.
///
/// Policy rules (accept if ANY is true):
/// 1. Requester's instance UUID matches the launch provenance UUID
/// 2. Only one client is connected (the requester owns the machine)
/// 3. --allow-shutdown flag was set
///
/// Otherwise, refuse the request.
class ShutdownPolicyEvaluator {
public:
    /// Create an evaluator with the given launch provenance UUID and config.
    ///
    /// @param provenance_instance_uuid The instance UUID from launch provenance.
    /// @param config Policy configuration (e.g., --allow-shutdown flag).
    ShutdownPolicyEvaluator(std::string provenance_instance_uuid,
                            ShutdownPolicyConfig config)
        : provenance_instance_uuid_(std::move(provenance_instance_uuid))
        , config_(config) {
    }

    /// Evaluate whether a shutdown request should be accepted.
    ///
    /// @param requester_instance_uuid The instance UUID sent by the requester
    ///        (via x-beebium-instance-uuid metadata). Empty if not provided.
    /// @param client_count Number of clients currently connected.
    /// @return Pair of (accepted, message). Message explains the decision.
    [[nodiscard]] std::pair<bool, std::string> evaluate(
        const std::string& requester_instance_uuid,
        int client_count) const {

        // Rule 1: Instance UUID matches provenance
        if (!requester_instance_uuid.empty() &&
            requester_instance_uuid == provenance_instance_uuid_) {
            return {true, "Shutdown initiated (launcher authority)"};
        }

        // Rule 2: Single client connected
        if (client_count <= 1) {
            return {true, "Shutdown initiated (sole client)"};
        }

        // Rule 3: --allow-shutdown flag set
        if (config_.allow_shutdown) {
            return {true, "Shutdown initiated (--allow-shutdown)"};
        }

        // Refuse: multiple clients and no authority
        return {false, "Shutdown refused: multiple clients connected and "
                       "requester is not the launcher"};
    }

    /// Get the provenance instance UUID.
    [[nodiscard]] const std::string& provenance_instance_uuid() const {
        return provenance_instance_uuid_;
    }

    /// Get the policy configuration.
    [[nodiscard]] const ShutdownPolicyConfig& config() const {
        return config_;
    }

private:
    std::string provenance_instance_uuid_;
    ShutdownPolicyConfig config_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SHUTDOWN_POLICY_HPP
