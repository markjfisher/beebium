# Shutdown RPC Design

## Overview

The Shutdown RPC allows clients to request that an emulator core terminate gracefully. This distinguishes between "disconnect UI" (client closes connection but machine keeps running) and "power off" (machine actually stops).

Without an explicit shutdown mechanism, clients must rely on:
- Signal-based termination (SIGTERM/SIGINT) — only works for locally-launched cores
- Killing the process — abrupt, no cleanup opportunity

A proper RPC-based shutdown enables:
- **Remote shutdown**: Terminate a core running on another machine
- **Graceful cleanup**: Machine can notify other clients, save state, flush buffers
- **Policy enforcement**: Core can refuse shutdown if inappropriate (multi-client scenario)
- **Audit trail**: Shutdown request is explicit and logged

## Design Principles

1. **Request, not command**: Clients *request* shutdown; the server *decides* whether to accept
2. **Instance-identity-aware**: Shutdown policy matches the specific client instance that launched the core (not just client type)
3. **Client-count-aware**: Multiple connected clients may prevent shutdown by non-launchers
4. **Graceful by default**: Give clients time to disconnect cleanly
5. **Explicit fallback**: Clients can request immediate shutdown if needed

## Shutdown Policy

The server accepts a shutdown request if ANY of:

1. **Provenance identity match**: The requesting client's instance UUID matches the launching provenance's instance UUID (the exact process that launched the server is requesting shutdown)
2. **Single client**: Only one client is connected (the requester owns the machine)
3. **Explicit permission**: Server was launched with `--allow-shutdown` flag

If none apply, the server refuses with an explanation.

### Policy Rationale

| Scenario | Accept? | Reason |
|----------|---------|--------|
| Script A launched, Script A requests shutdown | Yes | Instance UUID matches |
| Script A launched, Script B (same type) requests | No | Different instance, not the launcher |
| macOS GUI launched, Python debugger requests | No | Instance mismatch, might disrupt user |
| Any launcher, only 1 client connected | Yes | Sole user, no one else affected |
| Any launcher, `--allow-shutdown` set | Yes | Administrator explicitly allowed |
| macOS GUI launched, 3 clients, no flag | No | Would disrupt other users |

### The `--allow-shutdown` Flag

By default, shutdown requests are restricted: only the launching client (matching instance UUID) or a sole connected client can shut down the server. The `--allow-shutdown` flag removes this restriction, allowing any client to shut down the server.

When set:

- Instance UUID matching is not required
- Client count doesn't matter
- Any connected client can request shutdown and it will be accepted

**Use cases:**

| Scenario | Why use the flag |
|----------|------------------|
| Headless server | Admin wants to shut down via Python script or CLI tool |
| Development/testing | Convenient to kill servers from any client without tracking which launched what |
| Orchestration systems | A supervisor process needs to manage cores it didn't launch |
| Shared machines | Multiple users access the same core; any should be able to stop it |

**When NOT to use:**

- Multi-user scenarios where one user shouldn't disrupt another
- When the launcher should retain exclusive control

**Example usage:**

```bash
# Server allows any client to shut it down
beebium-server --allow-shutdown --port 48875

# Later, from a different process (local or remote):
beebium-cli shutdown --host 192.168.1.50 --port 48875  # Succeeds
```

### Future: Per-Client Permission

The policy could be extended to allow specific clients to have shutdown authority via tokens or certificates, but this is out of scope for the initial implementation.

## Graceful Subsystem Shutdown

Shutdown should allow in-flight operations to complete cleanly. Different subsystems may have different requirements — the disc controller shouldn't lose data mid-write, audio buffers should drain, etc. Rather than hardcoding these dependencies, subsystems register shutdown conditions with a coordinator.

### Design Goals

1. **Loose coupling**: Shutdown system doesn't know about disc, audio, etc. — they register with it
2. **Bounded waiting**: Each condition has a timeout; overall shutdown has a maximum duration
3. **Observable progress**: Clients can see what's blocking shutdown (for debugging/UI)
4. **Graceful degradation**: If a condition times out, shutdown proceeds anyway (with warning)

### Shutdown Condition Interface

```cpp
// src/service/include/beebium/service/ShutdownCondition.hpp

/// A condition that should be satisfied before shutdown proceeds.
/// Subsystems implement this interface and register with ShutdownCoordinator.
class ShutdownCondition {
public:
    virtual ~ShutdownCondition() = default;

    /// Human-readable name for logging/debugging (e.g., "Disc controller")
    virtual std::string name() const = 0;

    /// Called when shutdown is initiated. Subsystem should begin winding down.
    /// For example, disc controller stops accepting new commands.
    virtual void prepare_for_shutdown() = 0;

    /// Returns true when this subsystem is ready for shutdown.
    /// For example, disc controller returns true when no I/O is in progress.
    virtual bool ready_for_shutdown() const = 0;

    /// Maximum time to wait for this condition (milliseconds).
    /// Coordinator will proceed after this timeout even if not ready.
    virtual std::chrono::milliseconds timeout() const {
        return std::chrono::milliseconds(2000);  // Default 2 seconds
    }
};
```

### Shutdown Coordinator

```cpp
// src/service/include/beebium/service/ShutdownCoordinator.hpp

class ShutdownCoordinator {
public:
    /// Register a shutdown condition. Coordinator does not own the condition.
    void register_condition(ShutdownCondition* condition);

    /// Unregister (e.g., when subsystem is destroyed)
    void unregister_condition(ShutdownCondition* condition);

    /// Initiate graceful shutdown. Blocks until all conditions satisfied or timed out.
    /// Returns list of conditions that timed out (empty = fully graceful).
    std::vector<std::string> coordinate_shutdown();

    /// Query current state (for UI/debugging)
    struct ConditionStatus {
        std::string name;
        bool ready;
        std::chrono::milliseconds elapsed;
        std::chrono::milliseconds timeout;
    };
    std::vector<ConditionStatus> get_condition_status() const;

private:
    std::vector<ShutdownCondition*> conditions_;
    mutable std::mutex mutex_;
};
```

### Coordinator Implementation

```cpp
std::vector<std::string> ShutdownCoordinator::coordinate_shutdown() {
    std::vector<std::string> timed_out;

    // Phase 1: Notify all subsystems to prepare
    {
        std::lock_guard lock(mutex_);
        for (auto* cond : conditions_) {
            cond->prepare_for_shutdown();
        }
    }

    // Phase 2: Wait for each condition (with individual timeouts)
    auto start = std::chrono::steady_clock::now();
    constexpr auto poll_interval = std::chrono::milliseconds(50);
    constexpr auto max_total = std::chrono::milliseconds(10000);  // Hard cap

    while (true) {
        bool all_ready = true;
        auto now = std::chrono::steady_clock::now();
        auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start);

        // Hard cap on total shutdown time
        if (total_elapsed >= max_total) {
            std::lock_guard lock(mutex_);
            for (auto* cond : conditions_) {
                if (!cond->ready_for_shutdown()) {
                    timed_out.push_back(cond->name());
                }
            }
            break;
        }

        {
            std::lock_guard lock(mutex_);
            for (auto* cond : conditions_) {
                if (!cond->ready_for_shutdown()) {
                    if (total_elapsed < cond->timeout()) {
                        all_ready = false;
                    } else {
                        // This condition timed out
                        timed_out.push_back(cond->name());
                    }
                }
            }
        }

        if (all_ready) {
            break;
        }

        std::this_thread::sleep_for(poll_interval);
    }

    return timed_out;
}
```

### Example: Disc Controller Integration

```cpp
// In DiscController.hpp

class DiscController : public ShutdownCondition {
public:
    // ... existing disc controller interface ...

    // ShutdownCondition implementation
    std::string name() const override { return "Disc controller"; }

    void prepare_for_shutdown() override {
        // Stop accepting new commands, let current operation finish
        accepting_commands_ = false;
    }

    bool ready_for_shutdown() const override {
        // Ready when no disc operation is in progress
        return !is_busy();
    }

    std::chrono::milliseconds timeout() const override {
        // Disc operations can take a while (track seek + read/write)
        return std::chrono::milliseconds(3000);
    }

private:
    std::atomic<bool> accepting_commands_{true};
};
```

### Example: Audio Buffer Drain

```cpp
class AudioService : public ShutdownCondition {
public:
    std::string name() const override { return "Audio output"; }

    void prepare_for_shutdown() override {
        // Stop accepting new samples
        draining_ = true;
    }

    bool ready_for_shutdown() const override {
        // Ready when buffer is empty or nearly empty
        return buffer_.size() < min_drain_threshold_;
    }

    std::chrono::milliseconds timeout() const override {
        // Audio buffers are typically small, drain quickly
        return std::chrono::milliseconds(500);
    }
};
```

### Integration with Shutdown Sequence

The `ShutdownManager` uses the coordinator:

```cpp
void ShutdownManager::initiate_shutdown(ShutdownMode mode, uint32_t grace_ms) {
    shutting_down_.store(true);

    if (mode == SHUTDOWN_IMMEDIATE) {
        // Skip graceful coordination
        shutdown_callback_();
        return;
    }

    // Graceful: coordinate subsystem shutdown first
    std::thread([this, grace_ms] {
        auto timed_out = coordinator_->coordinate_shutdown();

        if (!timed_out.empty()) {
            // Log which subsystems didn't finish gracefully
            for (const auto& name : timed_out) {
                log_warning("Shutdown timeout: {}", name);
            }
        }

        // Now proceed with server shutdown
        shutdown_callback_();
    }).detach();
}
```

### Reporting Shutdown Progress (Optional Enhancement)

For debugging or UI feedback, the shutdown status could be reported to clients:

```protobuf
message ShutdownProgress {
    message ConditionStatus {
        string name = 1;
        bool ready = 2;
        int32 elapsed_ms = 3;
        int32 timeout_ms = 4;
    }
    repeated ConditionStatus conditions = 1;
}

// Could be sent as additional events on WatchServerStatus stream
// or as a separate RPC for debugging tools
```

### Files to Create

- `src/service/include/beebium/service/ShutdownCondition.hpp` — Interface
- `src/service/include/beebium/service/ShutdownCoordinator.hpp` — Coordinator class
- `src/service/src/ShutdownCoordinator.cpp` — Implementation

### Subsystems to Update (register conditions)

- `src/service/.../DiscService` — Wait for disc I/O completion
- `src/service/.../AudioService` — Drain audio buffers
- Future: Any subsystem with in-flight operations

## Protocol Design

### Shutdown Mode Enum

```protobuf
enum ShutdownMode {
  // Normal shutdown: notify clients, wait for grace period, then terminate
  SHUTDOWN_GRACEFUL = 0;

  // Immediate shutdown: terminate as soon as possible
  SHUTDOWN_IMMEDIATE = 1;
}
```

### Request and Response Messages

```protobuf
message ShutdownRequest {
  // How to shut down
  ShutdownMode mode = 1;

  // Grace period in milliseconds before forced termination
  // 0 = use server default (5000ms)
  // Only meaningful for SHUTDOWN_GRACEFUL
  int32 grace_period_ms = 2;
}

message ShutdownResponse {
  // Whether the shutdown request was accepted
  bool accepted = 1;

  // Human-readable explanation
  // On success: "Shutdown initiated"
  // On failure: "Shutdown refused: multiple clients connected"
  string message = 2;
}
```

### RPC Definition

```protobuf
// In SystemService
rpc RequestShutdown(ShutdownRequest) returns (ShutdownResponse);
```

### Design Notes

- **Unary RPC**: Shutdown is a single request/response, not a stream
- **Non-blocking**: The RPC returns immediately after accepting; actual shutdown proceeds asynchronously
- **Idempotent**: Multiple shutdown requests are safe; subsequent requests return success if already shutting down

## Server Implementation

### Shutdown Manager Class

```cpp
// src/service/include/beebium/service/ShutdownManager.hpp

class ShutdownManager {
public:
    struct Policy {
        bool allow_remote_shutdown = false;  // --allow-shutdown flag
    };

    ShutdownManager(const Provenance& provenance,
                    ConnectionTracker* tracker,
                    Policy policy);

    // Evaluate whether a shutdown request should be accepted
    // Returns (accepted, reason)
    std::pair<bool, std::string> evaluate_request(
        const std::string& requester_instance_uuid) const;

    // Initiate shutdown sequence
    // Called after request is accepted
    void initiate_shutdown(ShutdownMode mode, uint32_t grace_ms);

    // Check if shutdown is in progress
    bool is_shutting_down() const;

private:
    Provenance provenance_;
    ConnectionTracker* tracker_;
    Policy policy_;
    std::atomic<bool> shutting_down_{false};
};
```

### Integration with SystemService

```cpp
template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::RequestShutdown(
    grpc::ServerContext* context,
    const ShutdownRequest* request,
    ShutdownResponse* response) {

    // Extract requester's instance UUID from context metadata
    std::string requester_instance_uuid = extract_instance_uuid(context);

    auto [accepted, message] = shutdown_manager_->evaluate_request(requester_instance_uuid);
    response->set_accepted(accepted);
    response->set_message(message);

    if (accepted) {
        uint32_t grace_ms = request->grace_period_ms();
        if (grace_ms == 0) {
            grace_ms = 5000;  // Default grace period
        }

        shutdown_manager_->initiate_shutdown(request->mode(), grace_ms);

        // Notify watchers
        notify_shutdown(grace_ms);
    }

    return grpc::Status::OK;
}
```

### Client Instance Identification

For provenance matching to work, the server needs to know the requester's instance UUID. Options:

1. **Metadata header**: Client sends `x-beebium-instance-uuid: <uuid>` with the shutdown request
2. **Connection registration**: Client sends UUID once at connection setup
3. **Anonymous fallback**: Treat unknown clients as unmatched, relying on client count or flag

Initial implementation: Use metadata header with anonymous fallback.

```cpp
std::string extract_instance_uuid(grpc::ServerContext* context) {
    auto metadata = context->client_metadata();
    auto it = metadata.find("x-beebium-instance-uuid");
    if (it != metadata.end()) {
        return std::string(it->second.data(), it->second.size());
    }
    return "";  // Empty = no match possible
}
```

### Shutdown Sequence

When shutdown is accepted:

1. **Set shutting_down flag**: Prevents further requests from being accepted
2. **Notify watchers**: Send `SHUTTING_DOWN` event with grace period
3. **Start grace timer**: For graceful mode
4. **Call shutdown callback**: `g_notify_clients_shutdown` to integrate with existing signal handling
5. **After grace period**: Stop gRPC server, terminate process

```cpp
void ShutdownManager::initiate_shutdown(ShutdownMode mode, uint32_t grace_ms) {
    shutting_down_.store(true);

    if (mode == SHUTDOWN_IMMEDIATE) {
        // Skip grace period
        shutdown_callback_();
        return;
    }

    // Graceful: schedule actual shutdown after grace period
    std::thread([this, grace_ms] {
        std::this_thread::sleep_for(std::chrono::milliseconds(grace_ms));
        shutdown_callback_();
    }).detach();
}
```

## Client-Side Shutdown Handling

When the server initiates shutdown (whether via RPC request or signal), all clients with active `WatchServerStatus` streams receive a `SHUTTING_DOWN` event. Clients must handle this event gracefully.

### Expected Client Behavior

When a client receives `SERVER_STATUS_SHUTTING_DOWN`:

1. **Stop sending requests**: New RPCs will likely fail or be interrupted
2. **Cancel pending operations**: Abort any in-flight requests cleanly
3. **Release resources**: Close streams, flush buffers, release handles
4. **Disconnect**: Close the gRPC channel
5. **Update UI** (if applicable): Inform the user the machine has stopped

Clients should complete these steps within the grace period specified in `shutdown_grace_ms`. After the grace period, the server terminates and connections are forcibly closed.

### Python Client Shutdown Handler

```python
# In beebium/client.py

class Beebium:
    def __init__(self, ...):
        # ...
        self._shutdown_callbacks: list[Callable[[], None]] = []
        self._status_watcher_thread: threading.Thread | None = None

    def on_shutdown(self, callback: Callable[[], None]) -> None:
        """Register a callback to be invoked when server shuts down."""
        self._shutdown_callbacks.append(callback)

    def _start_status_watcher(self) -> None:
        """Start background thread watching for server status changes."""
        def watch():
            try:
                for event in self._system_stub.WatchServerStatus(
                    WatchServerStatusRequest()
                ):
                    if event.status == SERVER_STATUS_SHUTTING_DOWN:
                        self._handle_shutdown(event.shutdown_grace_ms)
                        break
            except grpc.RpcError:
                # Stream closed, server gone
                self._handle_shutdown(0)

        self._status_watcher_thread = threading.Thread(target=watch, daemon=True)
        self._status_watcher_thread.start()

    def _handle_shutdown(self, grace_ms: int) -> None:
        """Handle server shutdown notification."""
        # Invoke registered callbacks
        for callback in self._shutdown_callbacks:
            try:
                callback()
            except Exception:
                pass  # Don't let callback errors prevent cleanup

        # Close our connection
        self.close()
```

### Swift/macOS Client Shutdown Handler

```swift
// In MachineConnection.swift

class MachineConnection: ObservableObject {
    @Published var isConnected: Bool = true
    @Published var shutdownReason: String? = nil

    private var statusWatchTask: Task<Void, Never>?

    func startWatchingStatus() {
        statusWatchTask = Task {
            do {
                for try await event in client.watchServerStatus(
                    Beebium_WatchServerStatusRequest()
                ) {
                    await MainActor.run {
                        handleStatusEvent(event)
                    }
                }
            } catch {
                // Stream ended, server gone
                await MainActor.run {
                    handleDisconnect(reason: "Connection lost")
                }
            }
        }
    }

    private func handleStatusEvent(_ event: Beebium_ServerStatusEvent) {
        switch event.status {
        case .shuttingDown:
            shutdownReason = event.message
            isConnected = false

            // Grace period to clean up
            let graceMs = event.shutdownGraceMs
            Task {
                try? await Task.sleep(nanoseconds: UInt64(graceMs) * 1_000_000)
                disconnect()
            }

        case .identityChanged:
            // Handle identity update (existing behavior)
            break

        case .ready, .UNRECOGNIZED:
            break
        }
    }

    private func handleDisconnect(reason: String) {
        isConnected = false
        shutdownReason = reason
        // Notify UI, clean up resources
    }
}
```

### UI Considerations (macOS)

When shutdown is received:

- **Window title**: Could show "(Disconnected)" or "(Stopped)"
- **Menu items**: Disable machine-specific actions
- **Alert** (optional): "The machine 'BBC Model B' has shut down."
- **Reconnect option**: If server might restart, offer reconnection

```swift
// In MachineWindowView.swift

struct MachineWindowView: View {
    @ObservedObject var connection: MachineConnection

    var body: some View {
        ZStack {
            // Normal content
            DisplayView(...)

            // Overlay when disconnected
            if !connection.isConnected {
                DisconnectedOverlay(reason: connection.shutdownReason)
            }
        }
    }
}
```

### Files Requiring Shutdown Handler Implementation

| Client | File | Changes |
|--------|------|---------|
| Python | `clients/python/src/beebium/client.py` | Add `on_shutdown()`, status watcher thread, `_handle_shutdown()` |
| Python | `clients/python/src/beebium/server.py` | `ServerProcess` should handle child shutdown cleanly |
| macOS | `clients/macos/.../MachineConnection.swift` | Add status watching, shutdown handling |
| macOS | `clients/macos/.../MachineWindowView.swift` | Disconnected state UI |

## Client Usage

### Python Client

The Python client tracks its own instance UUID (generated at instantiation). When requesting shutdown, it sends this UUID so the server can verify provenance match.

```python
# In beebium/system.py

class System:
    def __init__(self, stub, instance_uuid: str):
        self._stub = stub
        self._instance_uuid = instance_uuid  # Client's own instance UUID

    def request_shutdown(
        self,
        graceful: bool = True,
        grace_period_ms: int = 5000
    ) -> tuple[bool, str]:
        """
        Request server shutdown.

        Args:
            graceful: If True, use graceful shutdown with grace period.
                      If False, request immediate shutdown.
            grace_period_ms: Grace period in milliseconds (graceful mode only).
                             0 uses server default.

        Returns:
            Tuple of (accepted, message).
            accepted is True if shutdown was initiated.
            message explains the result.
        """
        request = ShutdownRequest(
            mode=SHUTDOWN_GRACEFUL if graceful else SHUTDOWN_IMMEDIATE,
            grace_period_ms=grace_period_ms if graceful else 0,
        )
        response = self._stub.RequestShutdown(
            request,
            metadata=[("x-beebium-instance-uuid", self._instance_uuid)],
        )
        return response.accepted, response.message
```

### Python ServerProcess Integration

```python
# In beebium/server.py

class ServerProcess:
    def stop(self, timeout: float = 5.0) -> bool:
        """
        Stop the server process.

        Attempts graceful RPC shutdown first, falls back to SIGTERM.

        Returns:
            True if server stopped cleanly, False if force-killed.
        """
        if self._client is not None:
            try:
                accepted, _ = self._client.system.request_shutdown(
                    graceful=True,
                    grace_period_ms=int(timeout * 1000),
                )
                if accepted:
                    # Wait for process to exit
                    try:
                        self._process.wait(timeout=timeout + 1)
                        return True
                    except subprocess.TimeoutExpired:
                        pass  # Fall through to SIGTERM
            except grpc.RpcError:
                pass  # Connection lost, fall through

        # Fallback: SIGTERM
        self._process.terminate()
        try:
            self._process.wait(timeout=2.0)
            return True
        except subprocess.TimeoutExpired:
            self._process.kill()
            self._process.wait()
            return False
```

### Swift/macOS Client

The macOS client tracks its instance UUID (generated at app launch). This is passed to the server when requesting shutdown.

```swift
// In SystemClient.swift

extension SystemClient {
    func requestShutdown(
        instanceUUID: String,
        graceful: Bool = true,
        graceMs: UInt32 = 5000
    ) async throws -> (accepted: Bool, message: String) {
        var request = Beebium_ShutdownRequest()
        request.mode = graceful ? .graceful : .immediate
        request.gracePeriodMs = graceful ? Int32(graceMs) : 0

        var callOptions = CallOptions()
        callOptions.customMetadata.add(
            name: "x-beebium-instance-uuid",
            value: instanceUUID
        )

        let response = try await client.requestShutdown(
            request,
            callOptions: callOptions
        )
        return (response.accepted, response.message)
    }
}
```

## Files to Modify

### New Files

- `src/service/include/beebium/service/ShutdownManager.hpp`
- `src/service/src/ShutdownManager.cpp`

### Modified Files

- `src/service/proto/system.proto` — Add `ShutdownMode`, `ShutdownRequest`, `ShutdownResponse`, `RequestShutdown`
- `src/service/include/beebium/service/SystemService.hpp` — Add `RequestShutdown` method, integrate `ShutdownManager`
- `src/server/ServerMain.hpp` — Add `--allow-shutdown` flag, pass to service
- `clients/python/src/beebium/system.py` — Add `request_shutdown` method
- `clients/python/src/beebium/server.py` — Update `ServerProcess.stop()` to try RPC first

## Testing

### Unit Tests

```cpp
TEST_CASE("ShutdownManager policy evaluation") {
    Provenance prov{
        .type = "python-client",
        .instance_uuid = "550e8400-e29b-41d4-a716-446655440000"
    };
    ConnectionTracker tracker;
    ShutdownManager::Policy policy{.allow_remote_shutdown = false};
    ShutdownManager manager(prov, &tracker, policy);

    SECTION("Provenance instance UUID match accepts") {
        tracker.on_client_connected();
        tracker.on_client_connected();  // 2 clients

        auto [accepted, msg] = manager.evaluate_request(
            "550e8400-e29b-41d4-a716-446655440000"  // Same UUID
        );
        REQUIRE(accepted);
    }

    SECTION("Single client accepts regardless of instance UUID") {
        tracker.on_client_connected();  // 1 client

        auto [accepted, msg] = manager.evaluate_request(
            "different-uuid"  // Different UUID, but only client
        );
        REQUIRE(accepted);
    }

    SECTION("Same type but different instance UUID refuses") {
        tracker.on_client_connected();
        tracker.on_client_connected();  // 2 clients

        auto [accepted, msg] = manager.evaluate_request(
            "660e8400-e29b-41d4-a716-446655440001"  // Different UUID
        );
        REQUIRE_FALSE(accepted);
        REQUIRE(msg.find("multiple clients") != std::string::npos);
    }

    SECTION("Empty instance UUID refuses with multiple clients") {
        tracker.on_client_connected();
        tracker.on_client_connected();  // 2 clients

        auto [accepted, msg] = manager.evaluate_request("");  // No UUID provided
        REQUIRE_FALSE(accepted);
    }
}

TEST_CASE("ShutdownManager with allow_remote_shutdown") {
    Provenance prov{
        .type = "terminal",
        .instance_uuid = "550e8400-e29b-41d4-a716-446655440000"
    };
    ConnectionTracker tracker;
    ShutdownManager::Policy policy{.allow_remote_shutdown = true};
    ShutdownManager manager(prov, &tracker, policy);

    tracker.on_client_connected();
    tracker.on_client_connected();  // 2 clients

    auto [accepted, msg] = manager.evaluate_request("unknown-uuid");
    REQUIRE(accepted);  // Flag overrides policy
}
```

### Integration Tests (Python)

```python
def test_shutdown_accepted_single_client():
    """Single client can always shut down."""
    with ServerProcess.launch(mos_filepath=MOS_PATH) as server:
        client = Beebium(port=server.port)
        accepted, message = client.system.request_shutdown(graceful=True)
        assert accepted
        assert "initiated" in message.lower()

        # Server should exit
        server.process.wait(timeout=10)


def test_shutdown_accepted_by_launcher():
    """Launcher's instance UUID grants shutdown authority."""
    with ServerProcess.launch(mos_filepath=MOS_PATH) as server:
        # server.client is the launching client with matching instance UUID
        client1 = server.client

        # Second client connects (different instance UUID)
        client2 = Beebium(port=server.port)

        # Launcher can still shut down despite multiple clients
        accepted, message = client1.system.request_shutdown()
        assert accepted
        assert "initiated" in message.lower()

        server.process.wait(timeout=10)


def test_shutdown_refused_non_launcher_multi_client():
    """Non-launcher with multiple clients refused."""
    with ServerProcess.launch(mos_filepath=MOS_PATH) as server:
        # server.client is the launcher
        launcher = server.client

        # Second client connects (different instance UUID)
        other_client = Beebium(port=server.port)

        # Non-launcher requests shutdown - instance UUID mismatch
        accepted, message = other_client.system.request_shutdown()
        assert not accepted
        assert "multiple" in message.lower() or "refused" in message.lower()

        launcher.close()
        other_client.close()


def test_shutdown_accepted_with_flag():
    """--allow-shutdown overrides policy."""
    with ServerProcess.launch(
        mos_filepath=MOS_PATH,
        extra_args=["--allow-shutdown"]
    ) as server:
        # Two different clients (neither is launcher in terms of UUID)
        client1 = Beebium(port=server.port)
        client2 = Beebium(port=server.port)

        # Any client can shut down with flag
        accepted, message = client1.system.request_shutdown()
        assert accepted

        server.process.wait(timeout=10)
```

### Verification Criteria

1. **Accept single client**: Shutdown always accepted when only one client connected
2. **Accept instance UUID match**: Shutdown accepted when requester's instance UUID matches launch provenance UUID
3. **Accept with flag**: Shutdown accepted when `--allow-shutdown` is set
4. **Refuse multi-client mismatch**: Shutdown refused when multiple clients and no UUID match or flag
5. **Graceful timing**: Grace period is respected before termination
6. **Immediate mode**: Immediate shutdown skips grace period
7. **Idempotent**: Repeated requests don't cause errors
8. **Notification**: WatchServerStatus clients receive `SHUTTING_DOWN` event

## Edge Cases

### Already Shutting Down

If a shutdown request arrives while already shutting down:
- Return `accepted = true`
- Message indicates shutdown already in progress
- No change to grace period or mode

### Client Disconnects During Shutdown

If the requesting client disconnects before shutdown completes:
- Shutdown continues (request was already accepted)
- Other clients still receive notification

### Grace Period Exceeded

If clients don't disconnect within grace period:
- Server terminates anyway
- Streams are closed by gRPC server shutdown

### Shutdown Callback Failure

If the shutdown callback throws or fails:
- Log the error
- Continue termination (process will exit regardless)

### Concurrent Shutdown Requests

Multiple simultaneous shutdown requests:
- First accepted request wins
- Subsequent requests see "already shutting down" state
- Atomic flag prevents race conditions

## Design Decisions

1. **Shutdown reason reporting**: The `SHUTTING_DOWN` event does not include who requested shutdown. Simpler, and no clear use case.

2. **Abort shutdown**: No mechanism to cancel a graceful shutdown once initiated. Adds complexity with unclear benefit.

3. **Instance UUID verification**: Clients self-report their instance UUID via metadata. A malicious client could claim any UUID, but security is not a critical system quality for a BBC Micro emulator.

## Open Questions

1. **SIGTERM behaviour**: SIGTERM is the "polite" termination signal. It should bypass shutdown *policy* (admin has authority by virtue of process access), but should it still coordinate subsystem shutdown (wait for disc I/O, drain buffers)? Leaning yes — SIGTERM requests graceful termination, SIGKILL is for immediate.
