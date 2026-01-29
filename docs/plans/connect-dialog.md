# Connect Dialog Design

## Overview

The Connect Dialog enables users to attach to already-running emulator cores, whether launched locally by another process or running on a remote machine. This is distinct from "New…" which creates a new core — Connect… attaches to an existing one.

The dialog combines two discovery mechanisms:
1. **Automatic discovery** via Bonjour/mDNS (implemented in Phase 5)
2. **Manual entry** for machines not discoverable or on different networks

This phase builds on:
- Phase 5 (Service Advertisement) — cores advertise themselves
- Phase 6 (File Menu Skeleton) — the Connect… menu item already exists as a stub

## Design Principles

1. **Discoverable first**: Automatic discovery is the primary path; manual entry is a fallback
2. **Recent connections**: Frequently-used manual addresses are remembered
3. **Error recovery**: Connection failures don't dismiss the dialog — user can retry or try another
4. **Machine identity**: Display machine names and UUIDs from Phase 2, not just host:port

## Dialog Layout

```
┌─────────────────────────────────────────────────────────────┐
│  Connect to Machine                                         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Discovered Machines                                        │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ This Mac                                                ││
│  │   ● BBC Model B "My Model B"                           ││
│  │     BBC Model B "Testing"                              ││
│  │                                                         ││
│  │ Network                                                 ││
│  │     BBC Master 128 "Ellie's Beeb"       ellies-macbook ││
│  └─────────────────────────────────────────────────────────┘│
│                                                             │
│  ─ or ─                                                     │
│                                                             │
│  Manual Connection                                          │
│  Host: [localhost                    ] Port: [48875]        │
│                                                             │
│  Recent: [▼ Select recent connection                      ] │
│                                                             │
│                             ┌─────────┐ ┌─────────────────┐ │
│                             │ Cancel  │ │     Connect     │ │
│                             └─────────┘ └─────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## Interaction Modes

The user can either:
1. **Select from discovered list**: Click a discovered machine, then Connect
2. **Enter manually**: Type host and port, then Connect
3. **Use recent**: Select from dropdown of previous manual connections

Selection in the discovered list clears manual entry focus (and vice versa), making the active choice unambiguous.

## Discovery List

The list is divided into two sections:

### This Mac

Machines running on the local computer. Shows:
- **Status indicator**: Green dot for responsive, grey for stale
- **Machine type**: "BBC Model B", "BBC Master 128", etc.
- **Machine name** (from Phase 2): User-assigned or auto-generated name

### Network

Machines discovered on other hosts. Shows:
- **Status indicator** as above
- **Machine type** and **machine name** as above
- **Hostname**: From SRV record, without `.local` suffix (e.g., "ellies-macbook")

### Technical Details

Address details (host:port) are available via:
- **Tooltip**: Hover over a machine row to see full address
- **Context menu**: Right-click shows "Copy Address" to copy `host:port` to clipboard

This keeps the UI clean for selection while providing access to technical details when needed (e.g., for scripting or manual connection from another tool).

### Empty States

If a section has no machines, it is omitted entirely rather than showing "None".

### Sorting

Within each section, machines are sorted by most recent advertisement.

### Empty States

**No discovered machines**:
```
No machines found on the local network.
Start a machine with the Beebium app, or enter an address manually.
```

**Discovery unavailable** (Bonjour not running):
```
Network discovery unavailable.
Enter the machine address manually.
```

## Recent Connections

### Data Model

```swift
struct RecentConnection: Codable, Identifiable {
    let id: UUID
    let host: String
    let port: Int
    let displayName: String?  // Remembered from last successful connection
    let lastUsed: Date
    let lastSuccessful: Bool
}
```

### Storage

Stored in `UserDefaults` under key `recentConnections`:
- Maximum 10 entries
- Oldest entries pruned when limit reached
- Updated on successful connection (moves to top, updates timestamp)
- Failed attempts update `lastSuccessful` but don't remove the entry

### UI Behaviour

The Recent dropdown shows:
- Display name if known, otherwise "host:port"
- Relative timestamp: "2 hours ago", "Yesterday", "3 days ago"
- Visual indicator for last-failed connections (but still selectable)

Selecting a recent connection populates the Host and Port fields.

## Error Handling

### Connection Failures

When Connect fails:
1. Dialog remains open
2. Error banner appears at top of dialog:
   ```
   ⚠ Could not connect to localhost:48875
     Connection refused
   ```
3. User can edit the address and retry, select a different machine, or cancel

### Validation

- Port must be numeric, 1-65535
- Host must be non-empty
- Connect button disabled until valid input

### Timeout

Connection attempt times out after 5 seconds with message:
```
⚠ Connection timed out
  The machine may not be running or may be unreachable.
```

## Files to Create

| File | Purpose |
|------|---------|
| `clients/macos/Beebium/Beebium/ConnectDialog.swift` | Main dialog view (replaces placeholder) |
| `clients/macos/Beebium/Beebium/DiscoveryClient.swift` | Bonjour browser wrapper |
| `clients/macos/Beebium/Beebium/RecentConnections.swift` | Recent connections persistence |

## Files to Modify

| File | Changes |
|------|---------|
| `clients/macos/Beebium/Beebium/FileCommands.swift` | Enable Connect… menu item |
| `clients/macos/Beebium/Beebium/MachineManager.swift` | Add `connect(host:port:)` method |

## Testing

### Manual Verification

1. **Discovery list**
   - Start a beebium-core manually, verify it appears in list
   - Start multiple cores, verify all appear
   - Stop a core, verify it disappears (or shows stale)

2. **Manual entry**
   - Enter valid host:port, verify Connect succeeds
   - Enter invalid port (e.g., "abc"), verify Connect disabled
   - Enter unreachable address, verify error message

3. **Recent connections**
   - Connect successfully, close dialog
   - Re-open dialog, verify address appears in Recent
   - Quit and relaunch app, verify recent connections persist

4. **Error handling**
   - Connect to non-existent address, verify dialog stays open with error
   - Fix address, verify error clears on retry

5. **Keyboard navigation**
   - Return key activates Connect (when enabled)
   - Escape key cancels

## Edge Cases

### Multiple Cores on Same Host

If multiple cores run on the same host (different ports), they should all appear in the discovered list with distinct entries. The machine name (from Phase 2) helps distinguish them.

### IPv6 Addresses

The Host field should accept IPv6 addresses. Display may need to handle bracket notation for clarity.

### Discovery Lag

Bonjour advertisements may take a few seconds to propagate. The UI should handle:
- Initial empty state gracefully
- Machines appearing incrementally
- No "loading" spinner (discovery is continuous)

## Design Decisions

1. **Discovery + Manual in same dialog**: Rather than separate flows, both are in one dialog. This reduces navigation and matches user mental model.

2. **Recent connections as dropdown, not list**: Keeps the dialog compact. The discovered machines list is the primary visual focus.

3. **No "Refresh" button**: Bonjour discovery is continuous. A refresh button implies polling, which is the wrong model.

4. **Error stays in dialog**: Connection failures are retriable. Closing would lose the user's input.

5. **Port as separate field**: Separate fields reduce parsing complexity and make validation clearer.

6. **No authentication**: Not supported currently. May be added later if needed.

7. **No TLS**: Plain connections only. Advanced users can tunnel over VPN if security is needed.

See the main [lifecycle-management.md](lifecycle-management.md) for the overall phase roadmap.
