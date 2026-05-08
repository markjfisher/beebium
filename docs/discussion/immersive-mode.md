# Immersive Mode (macOS)

A Beebium-specific full-screen mode for the macOS frontend: no title bar, no
status bar, no on-screen controls or chrome — just the emulation graphics
occupying the screen, rendered with the user's current Display Style. Toggled
via **View ▸ Enter Immersive Mode** (`Cmd+Shift+F`).

Implementation-wise, Immersive Mode is **macOS native fullscreen plus
Beebium-specific customisation on top** rather than a hand-rolled in-place
borderless transformation. The choice was driven by the visual quality of the
result: an in-place borderless `.titled` window keeps the rounded top corners
of the theme frame and a 1pt highlight at the top edge that AppKit draws even
with the title-bar view hidden, both of which read as obviously-not-fullscreen.
A truly borderless `.borderless` window avoids those artifacts but conflicts
with SwiftUI's `WindowGroup` (key-window status and Metal rendering both broke
under experimentation). Native fullscreen sidesteps both problems: the window
moves to its own Space, expands to fill the display with no chrome artifacts,
and AppKit handles screen connect/disconnect natively. The Beebium-specific
behaviour — sidebar / status-bar overlays via existing keyboard shortcuts,
cursor auto-hide on idle, automatic exit on disconnection — is layered on top
of native fullscreen rather than re-implemented.

Status: Implemented on `immersive` branch.

---

## Goals

- A "lights down" emulation experience on demand: just the BBC display, full
  screen, with the user's current Display Style.
- Reachable by keyboard alone (`Cmd+Shift+F` to enter and exit).
- Per-window. Two machine windows can be in Immersive Mode independently, on
  different displays.
- The configuration sidebars and status bar remain reachable in Immersive Mode
  via their existing keyboard shortcuts (`Ctrl+Cmd+S`, `Cmd+/`, `Cmd+1..8`).
  When summoned in Immersive Mode, they appear *over* the emulation rather
  than reflowing it.

## Non-goals

- Replacing or modifying macOS native full-screen. Both modes coexist; the
  user can still use the green button if they want a separate Space with the
  Apple title-bar-on-hover treatment.
- Cross-platform parity. This is a macOS-only proposal. Windows and Linux
  frontends will get their own equivalents when those frontends exist.
- Hiding the cursor while the user is interacting. Idle-only.

## User-facing behaviour

### Entering Immersive Mode

1. The user invokes **View ▸ Enter Immersive Mode** (`Cmd+Shift+F`) on a
   focused machine window.
2. The window's title bar and the in-window status bar disappear. The sidebar
   collapses if it was open. The window borders dissolve and the window grows
   to fill its current screen.
3. The macOS menu bar and Dock auto-hide on that screen (revealable by
   shoving the cursor to the screen edge).
4. The cursor is visible immediately. After ~3 s of no mouse movement it
   hides; any movement reveals it again.
5. The emulation continues uninterrupted; only the framing changes.

### While in Immersive Mode

- The View menu item changes to **Exit Immersive Mode** (same shortcut).
- `Ctrl+Cmd+S` toggles the configuration sidebar as a left-anchored overlay
  drawn in front of the emulation. The sidebar's behaviour is otherwise
  unchanged: the same `SidebarModeToolbar` and `SidebarModeContent`, the
  same per-mode shortcuts (`Cmd+1..8`), the same opaque
  `windowBackgroundColor` background.
- `Cmd+/` toggles a status bar overlay anchored to the bottom of the screen,
  also opaque.
- Both overlays start hidden when Immersive is first entered, regardless of
  whether they were visible beforehand. The pre-immersive visibility state
  is restored on exit.
- Auxiliary windows (Settings, Connect, New Machine) open above the
  immersive window with normal window-level behaviour. The user dismisses
  them and naturally returns to the immersive window.

### Exiting Immersive Mode

The user exits Immersive Mode by:

- Pressing `Cmd+Shift+F` again, or selecting the menu item.
- Disconnecting from the machine. Any transition of `videoClient.connectionState`
  out of `.connected` exits Immersive Mode automatically — Immersive Mode
  is only meaningful while emulation is live.

On exit, the window restores its previous frame, style mask, and the
previously-recorded sidebar / status bar visibility. The macOS menu bar and
Dock return to their normal presentation (subject to other immersive windows
being open; see below).

### Persistence

Immersive Mode is **not** persisted across launches. Every Beebium session
starts in regular windowed mode; the user must deliberately enter Immersive
Mode in each session. The flag is per-window in-memory state only — no
`@AppStorage`, no `NSUserDefaults`, no save/restore.

### What Immersive Mode does *not* hide

- The cursor while the user is moving the mouse.
- macOS modal panels (file open, save, alerts) — these surface above the
  immersive window normally.
- Auxiliary Beebium windows opened by the user.

The principle: chrome belonging to *this machine window* disappears. System
modals and other windows the user explicitly opens are unaffected.

## State model

Each machine window owns a per-window flag, lifted into `ContentView`:

```swift
@State private var isImmersive: Bool = false
@State private var preImmersiveShowSidebar: Bool = true
@State private var preImmersiveShowStatusBar: Bool = true
```

The existing `showSidebar` / `showStatusBar` `@State` flags retain their
meaning. In non-immersive mode they drive `NavigationSplitView`'s column
visibility and the in-detail-pane status bar (as today). In immersive mode
they drive the overlay overlays' visibility.

On entering immersive:

```swift
preImmersiveShowSidebar = showSidebar
preImmersiveShowStatusBar = showStatusBar
showSidebar = false
showStatusBar = false
isImmersive = true
```

On exiting:

```swift
isImmersive = false
showSidebar = preImmersiveShowSidebar
showStatusBar = preImmersiveShowStatusBar
```

The `isImmersive` flag is exposed up the focus chain via a new
`FocusedValueKey` (parallel to the existing `ShowSidebarFocusedValueKey`),
so the App-level View menu can read and toggle it on the focused window.

## Window-level transformation

The chrome-removal heavy lifting is delegated to macOS native fullscreen.
`ImmersiveCoordinator` calls `NSWindow.toggleFullScreen(_:)`; AppKit handles
the animation, the move to a dedicated Space, the menu-bar / Dock auto-hide,
and the screen-frame fitting (no rounded corners, no top highlight, no title
separator).

The Beebium-specific bits layered on top of native fullscreen:

1. **Hide the standard window buttons** (close / miniaturise / zoom) before
   entering, restored on exit. In native fullscreen the title bar reveals on
   hover at the top of the screen; with the buttons hidden the reveal is an
   empty transparent strip.
2. **Hide the title text** (`titleVisibility = .hidden`), again so the
   reveal-on-hover shows nothing.
3. **Observe `NSWindow.didExitFullScreenNotification`** on the immersive
   window. If fullscreen ends through any path other than our menu item
   (e.g. `Cmd+Ctrl+F`, or the green button if it ever becomes accessible),
   the coordinator posts an `immersiveCoordinatorDidExitUnexpectedly`
   notification; ContentView observes that and flips `isImmersive` back to
   `false`, which restores `navigationLayout` and the pre-immersive sidebar
   / status-bar visibility.

### Why not in-place borderless

An earlier iteration kept the window in its current Space and stripped chrome
manually:

- `styleMask = [.borderless]` — broke SwiftUI's `WindowGroup`: key-window
  status was lost and the Metal-backed `EmulatorView` stopped rendering.
  Class-swapping the window to a `canBecomeKey`-overriding subclass via the
  Objective-C runtime did not reliably fix it.
- `styleMask` retaining `.titled` plus `.fullSizeContentView`, transparent
  title bar, hidden buttons, hidden `NSTitlebarView`, separator style `.none`,
  zeroed corner radius on the theme frame, no shadow — closer, but still
  left rounded top corners and a 1pt top highlight that AppKit draws on the
  theme frame regardless.

Neither path produced a result indistinguishable from native fullscreen.
Native fullscreen does, at the cost of moving the window to its own Space —
which is acceptable for the use case.

### Edge cases

- **Screen disconnect / resolution change**: AppKit handles these natively
  for fullscreen windows; no extra code needed.
- **Auxiliary windows opened while immersive** (Settings, Connect): they
  open in their own Space; macOS switches to that Space. The user dismisses
  the auxiliary window and swipes back to the immersive Space.
- **`Cmd+H` (hide app)**: works normally; on unhide the immersive window
  is back as it was.
- **`Cmd+M` (miniaturise)**: native fullscreen disallows miniaturisation, so
  the shortcut is a no-op without our intervention.

## Layout in Immersive Mode

`ContentView`'s `body` branches at the top level on `isImmersive`:

```swift
if isImmersive {
    ImmersiveLayout(
        showSidebar: $showSidebar,
        showStatusBar: $showStatusBar,
        // ... clients, settings, etc.
    )
} else {
    NavigationSplitView { ... } detail: { ... }   // current implementation
}
```

`ImmersiveLayout` is a `ZStack`:

```
ZStack(alignment: .topLeading) {
    EmulatorView(...)                 // fills the entire window
        .ignoresSafeArea()

    if showSidebar {
        HStack(spacing: 0) {
            VStack(spacing: 0) {
                SidebarModeToolbar(selectedMode: $sidebarMode)
                Divider()
                SidebarModeContent(mode: sidebarMode, ...)
            }
            .frame(width: 280)
            .background(Color(nsColor: .windowBackgroundColor))
            Spacer()
        }
        .transition(.move(edge: .leading))
    }

    if showStatusBar {
        VStack(spacing: 0) {
            Spacer()
            StatusBarView(...)
                .background(Color(nsColor: .windowBackgroundColor))
        }
        .transition(.move(edge: .bottom))
    }
}
.animation(.default, value: showSidebar)
.animation(.default, value: showStatusBar)
```

Key points:

- `EmulatorView`'s viewport is the entire window — the `MetalRenderer` and
  shaders are unchanged. The Display Style (`StandardDisplayStyle` or any
  future style) handles the centring of the active 4:3 pixel area within
  whatever rectangle it's given, exactly as today.
- The sidebar overlay is a **fixed width** in immersive mode (no
  drag-resizable splitter). 280 px is the existing `ideal` width. The
  user can change this in non-immersive mode and we can retain that
  preference, but in-immersive resizing is out of scope.
- The status bar overlay is full-width, anchored to the bottom edge.
- Both overlays are drawn opaque (`.windowBackgroundColor`). On 16:9
  displays the active 4:3 pixel area is centred with horizontal letterbox
  margins, and the sidebar typically sits in those margins anyway —
  obscuring at most a corner of the emulation in extreme aspect ratios.
  The translucent-material alternative was considered and rejected for
  readability of the configuration controls.

The `transition` modifiers give a slide-in/out animation matching what
SwiftUI does for the sidebar today. Animation duration and easing follow
SwiftUI's `.default` curve unless we discover a reason to deviate.

## Cursor auto-hide

Active only while `isImmersive == true`. Implemented as an `NSEvent`
local monitor installed on the immersive window:

```swift
NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .leftMouseDown,
                                            .rightMouseDown, .keyDown]) { event in
    cursorActivityTimer.bump()
    return event
}
```

`cursorActivityTimer` keeps the cursor visible and resets a 3 s timer on
each event. On expiry, `NSCursor.hide()`. The next event calls
`NSCursor.unhide()` (idempotent). On exiting Immersive Mode, the timer is
cancelled and `NSCursor.unhide()` is called unconditionally to guarantee
the cursor comes back.

Caveat: `NSCursor.hide()` is process-wide and reference-counted. The
balanced hide / unhide pairing matters; we must never leak a hide. The
exit-Immersive code path is the safety net.

## Disconnect-triggered exit

`ContentView` already observes `videoClient.connectionState` (see the
`onChange` block at `ContentView.swift:227`). Add to the existing handler:

```swift
.onChange(of: videoClient.connectionState) { newState in
    if isImmersive, case .connected = newState {
        // still good — no-op
    } else if isImmersive {
        exitImmersiveMode()
    }
    // ... existing connection wiring ...
}
```

Brief disconnections (e.g., a transient network blip during reconnection)
will exit Immersive Mode. The user re-enters manually after reconnecting.
Auto re-entry on reconnect is deliberately **not** implemented — the model
is "Immersive is something the user invokes on a healthy connection".

## Menu wiring

A new `CommandGroup` in `BeebiumApp.swift`, parallel to the existing
sidebar / status bar groups:

```swift
CommandGroup(after: .toolbar) {  // or wherever fits the View menu order
    Button(isImmersive == true ? "Exit Immersive Mode" : "Enter Immersive Mode") {
        isImmersive?.toggle()
    }
    .keyboardShortcut("f", modifiers: [.command, .shift])
    .disabled(isImmersive == nil)
}
```

`isImmersive` is published via a new `FocusedBinding`:

```swift
struct IsImmersiveFocusedValueKey: FocusedValueKey {
    typealias Value = Binding<Bool>
}

extension FocusedValues {
    var isImmersive: Binding<Bool>? {
        get { self[IsImmersiveFocusedValueKey.self] }
        set { self[IsImmersiveFocusedValueKey.self] = newValue }
    }
}
```

— mirroring the existing `showSidebar` / `showStatusBar` plumbing in
`SidebarMode.swift`.

The toggle's `set` side does not directly flip `isImmersive`; it routes
through an `enterImmersiveMode()` / `exitImmersiveMode()` pair that owns
the snapshot/restore work and the `NSWindow` transformation. Driving the
window manipulation off a `@State` setter directly couples view-update
ordering to AppKit imperative state in a way we'll regret.

## Multi-window and multi-screen

- Each `ContentView` instance owns its own `isImmersive`. Two windows can
  be immersive simultaneously, each occupying its own Space on its own
  display.
- Native fullscreen handles screen disconnect, resolution changes, and
  arrangement changes natively. No additional code in
  `ImmersiveCoordinator`.
- The cursor-auto-hide refcount on `ImmersiveCoordinator` ensures the
  `NSEvent` monitor and idle timer activate while at least one window is
  immersive and tear down cleanly when the last one leaves.

## Auxiliary windows

Settings (`Cmd+,`), Connect, and the New Machine dialog open as separate
`Window` / `Settings` scenes at the App level. Because the immersive
window is in its own Space, opening one of these will switch the user to
the Space the new window appears in (typically the main / non-fullscreen
Space). The user dismisses the auxiliary window and swipes back to the
immersive Space — the same interaction model macOS uses for any
fullscreen app with auxiliary windows.

No menu items are disabled while in Immersive Mode. The user can do
anything they could do normally.

## Open questions / future work

- **Disconnect overlay in Immersive Mode.** The current `statusOverlay`
  shown when not connected becomes moot under the disconnect-exits-immersive
  rule, but the brief moment between disconnection detection and immersive
  exit might flash the overlay. Worth checking in implementation; either
  the exit animation hides it, or the overlay is suppressed while
  `isImmersive` and exit is in flight.
- **Touch Bar.** No special handling. The Touch Bar continues to function
  normally in Immersive Mode.
- **Per-display presentation options.** As above — possible future
  refinement if multi-display users complain about the global menu-bar
  auto-hide.
- **Transitioning between Immersive Mode and native full-screen.** The two
  modes are mutually exclusive in practice (immersive removes the title
  bar so the green button is unreachable, and native full-screen hides
  the same chrome via different mechanisms). We do not need to support a
  direct transition between them; users exit one and enter the other if
  they want to switch. If this turns out to be confusing, the
  `Cmd+Ctrl+F` (native full-screen) shortcut can be intercepted and
  redirected, but for now we leave it alone.

## Phasing

A reasonable implementation order, each step independently testable:

1. **State plumbing.** Add `isImmersive`, the focused-value key, the
   menu item, the keyboard shortcut. Toggle has no visible effect yet
   beyond setting the flag.
2. **Window transformation.** `enterImmersiveMode()` /
   `exitImmersiveMode()` with the `NSWindow` style-mask snapshot/restore
   and `NSApp.presentationOptions` counter. Visible chrome disappears.
3. **Layout split.** Branch `ContentView.body` on `isImmersive`. Sidebar
   and status bar appear as overlays. Pre-immersive visibility snapshot
   and restore.
4. **Cursor auto-hide.** Event monitor + 3 s timer.
5. **Disconnect exit.** Hook `videoClient.connectionState` change
   handler.
6. **Multi-display polish.** `NSWindow.didChangeScreenNotification`
   handling for screen disconnects.
