# Client Connection Tracking Design

## Overview

Client connection tracking allows emulator cores to know how many clients are connected. This information supports:

- **Resource management**: Throttle or pause when no clients connected
- **Shutdown policy**: Refuse shutdown if multiple clients depend on the machine
- **Diagnostics**: Understand machine usage patterns
- **UI feedback**: Show connection status in frontends

Together with provenance (Phase 1) and identity (Phase 2), connection tracking completes the machine's awareness of its operational context.

| Concept | Purpose | Example |
|---------|---------|---------|
| Provenance | Who launched me? | "Python script launched me" |
| Identity | Who am I? | "I'm Teletext Server (uuid: abc...)" |
| **Connections** | **Who's connected?** | **"2 clients connected"** |

## Design Principles

1. **Passive tracking**: Connections are tracked automatically, not registered explicitly
2. **No client identity**: Track *counts*, not *who*—clients remain anonymous
3. **Eventual consistency**: Small delays in count updates are acceptable

## What Counts as a "Client"?

A client is counted when it has an active `WatchServerStatus` stream. This is the canonical "I'm here" signal—clients that want to be notified of shutdown must watch server status, so this stream naturally indicates presence.

Unary RPCs (one-shot requests) don't count toward the client count since they're transient and don't indicate ongoing presence.

## Protocol Design

### Connection Info Message (`system.proto`)

```protobuf
message ConnectionInfo {
  // Number of clients with active WatchServerStatus streams
  int32 client_count = 1;
}
```

### Extended SystemInfo Response

```protobuf
message SystemInfo {
  LaunchProvenance provenance = 3;
  MachineIdentity identity = 4;
  ConnectionInfo connections = 5;  // NEW
}
```

### Connection Events (Streaming)

For real-time connection monitoring:

```protobuf
message ConnectionEvent {
  enum Type {
    CLIENT_CONNECTED = 0;
    CLIENT_DISCONNECTED = 1;
  }
  Type type = 1;
  ConnectionInfo current = 2;  // State after this event
}

rpc WatchConnections(google.protobuf.Empty) returns (stream ConnectionEvent);
```

This allows frontends to show real-time connection status without polling.

## Server Implementation

### Connection Tracker Class

```cpp
// src/service/include/beebium/service/ConnectionTracker.hpp

class ConnectionTracker {
public:
    // Called when WatchServerStatus streams start/end
    void on_client_connected();
    void on_client_disconnected();

    // Query current state
    int client_count() const;

    // Subscribe to changes
    using Callback = std::function<void(const ConnectionEvent&)>;
    void add_listener(Callback cb);

private:
    std::atomic<int> client_count_{0};

    mutable std::mutex listeners_mutex_;
    std::vector<Callback> listeners_;

    void notify(ConnectionEvent::Type type);
};
```

### Integration Point

The SystemService calls the tracker when `WatchServerStatus` streams start and end:

```cpp
// In SystemService::WatchServerStatus
grpc::Status WatchServerStatus(grpc::ServerContext* context,
                               const WatchServerStatusRequest* request,
                               grpc::ServerWriter<ServerStatusEvent>* writer) {
    connection_tracker_->on_client_connected();

    // RAII guard for cleanup on any exit path
    auto guard = finally([this] {
        connection_tracker_->on_client_disconnected();
    });

    // ... stream status events ...
}
```

### Thread Safety

- Atomic counter for lock-free reads
- Mutex only for listener notification
- Eventual consistency is acceptable (UI updates can lag slightly)

## Client Usage

### Python Client

```python
class System:
    @property
    def client_count(self) -> int:
        """Get number of connected clients."""
        info = self._stub.GetSystemInfo(GetSystemInfoRequest())
        return info.connections.client_count
```

### Connection Watching

```python
def watch_connections(self) -> Iterator[ConnectionEvent]:
    """Stream connection events."""
    for event in self._stub.WatchConnections(Empty()):
        yield ConnectionEvent(
            type=event.type,
            client_count=event.current.client_count,
        )
```

## Use Cases

### Shutdown Policy (Phase 4)

```cpp
bool can_accept_shutdown_request() {
    int count = connection_tracker_->client_count();

    // Single client can always shut down their own machine
    if (count <= 1) {
        return true;
    }

    // Multiple clients: need explicit flag or refuse
    if (config_.allow_remote_shutdown) {
        return true;
    }

    return false;  // Refuse: other clients connected
}
```

### Resource Throttling (Future)

```cpp
void on_connection_changed(const ConnectionEvent& event) {
    if (event.current.client_count == 0) {
        // No clients: reduce CPU usage
        emulator_->set_throttle_mode(ThrottleMode::Paused);
    } else {
        // Clients connected: full operation
        emulator_->set_throttle_mode(ThrottleMode::Normal);
    }
}
```

### UI Status Display

```swift
// macOS frontend
Text("Connected: \(clientCount) client(s)")
    .foregroundColor(clientCount > 0 ? .green : .secondary)
```

## Files to Modify

### New Files

- `src/service/include/beebium/service/ConnectionTracker.hpp`
- `src/service/src/ConnectionTracker.cpp`

### Modified Files

- `src/service/proto/system.proto` — Add `ConnectionInfo`, `ConnectionEvent`, `WatchConnections`
- `src/service/include/beebium/service/Server.hpp` — Own and provide `ConnectionTracker`
- `src/service/src/Server.cpp` — Instantiate tracker, pass to services
- `src/service/include/beebium/service/SystemService.hpp` — Return connection info, track streams
- `src/service/src/SystemService.cpp` — Implement `WatchConnections`, call tracker on stream lifecycle
- `clients/beebium-python-client/src/beebium/system.py` — Add `client_count` property

## Testing

### Unit Tests

```cpp
TEST_CASE("ConnectionTracker counts correctly") {
    ConnectionTracker tracker;

    REQUIRE(tracker.client_count() == 0);

    tracker.on_client_connected();
    REQUIRE(tracker.client_count() == 1);

    tracker.on_client_connected();
    REQUIRE(tracker.client_count() == 2);

    tracker.on_client_disconnected();
    REQUIRE(tracker.client_count() == 1);
}
```

### Integration Tests (Python)

```python
def test_connection_counting():
    with Beebium.launch(mos_filepath=MOS_PATH) as client1:
        # Single client (us)
        assert client1.system.client_count == 1

        # Second client connects
        client2 = Beebium(port=client1.port)
        assert client1.system.client_count == 2

        # Second client disconnects
        client2.close()
        assert client1.system.client_count == 1
```

### Verification Criteria

1. **Initial state**: New server reports `client_count == 0`
2. **Increment**: Each new `WatchServerStatus` stream increments count
3. **Decrement**: Stream close decrements count (even on client crash)
4. **Thread safety**: Concurrent connects/disconnects don't corrupt count
5. **No underflow**: Count never goes negative

## Edge Cases

### Client Crash

When a client crashes without clean disconnect:
- gRPC detects broken stream eventually (keep-alive timeout)
- Stream end callback fires, count decrements
- May have brief over-count during timeout window

### Rapid Connect/Disconnect

Multiple rapid connections:
- Atomic operations ensure correct count
- Event stream may coalesce rapid changes
- Final state is always consistent

### Server Restart

On server restart:
- Count starts at 0
- All clients must reconnect
- No persistent connection state

## Future Considerations

- **Client identification**: Optional client ID for debugging/logging
- **Connection metadata**: Track connection duration, data transferred
- **Per-service counts**: Track which services each client is using (video, audio, debugger)
- **Connection limits**: Reject connections beyond a threshold
- **Health monitoring**: Detect and drop stale connections proactively
