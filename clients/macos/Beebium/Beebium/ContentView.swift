// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

import AppKit
import SwiftUI

/// Routes the main WindowGroup to either welcome or emulator content based on whether
/// a connection target was pending when the window was created. This eliminates the need
/// for a separate Welcome Window scene and avoids the ghost Window menu entry caused by
/// SwiftUI's unconditional window creation on launch.
struct MainWindowRouter: View {
    @ObservedObject var keyboardMappingManager: KeyboardMappingManager
    @State private var connectionTarget: ConnectionTarget?
    @State private var needsRun: Bool = false
    @State private var provenanceUUID: String?
    @State private var currentWindow: NSWindow?

    var body: some View {
        Group {
            if let target = connectionTarget {
                ContentView(
                    initialTarget: target,
                    initialNeedsRun: needsRun,
                    initialProvenanceUUID: provenanceUUID,
                    keyboardMappingManager: keyboardMappingManager
                )
            } else {
                WelcomeWindowContent(onDismiss: { currentWindow?.close() })
                    .background(WindowAccessor(window: $currentWindow))
                    .onChange(of: currentWindow) { window in
                        guard let window = window else { return }
                        window.titlebarAppearsTransparent = true
                        window.titleVisibility = .hidden
                        window.title = "Welcome to Beebium"
                        window.setContentSize(NSSize(width: 800, height: 640))
                        window.center()
                    }
            }
        }
        .onAppear {
            let (target, run, prov) = ConnectWindowState.shared.consumePendingTarget()
            connectionTarget = target
            needsRun = run
            provenanceUUID = prov
            if target != nil {
                NotificationCenter.default.post(name: .didOpenEmulatorWindow, object: nil)
            }
        }
    }
}

/// Main content view displaying the emulator output
struct ContentView: View {
    @StateObject private var videoClient = VideoClient()
    @StateObject private var keyboardClient = KeyboardClient()
    @StateObject private var systemClient = SystemClient()
    @StateObject private var indicatorClient = IndicatorClient()
    @StateObject private var discClient = DiscClient()
    @StateObject private var audioClient = AudioClient()
    @StateObject private var debuggerClient = DebuggerClient()
    @StateObject private var audioMixerState = AudioMixerState()
    let initialTarget: ConnectionTarget
    let initialNeedsRun: Bool
    let initialProvenanceUUID: String?
    /// Whether this window needs to call Run() after connection (for cores launched with --wait=api).
    /// Initialised from initialNeedsRun in onAppear; reset to false after Run() succeeds.
    @State private var needsRun: Bool = false
    @State private var showStatusBar: Bool = true
    @State private var showSidebar: Bool = true
    @ObservedObject var keyboardMappingManager: KeyboardMappingManager
    @State private var sidebarMode: SidebarMode = .storage
    @State private var currentWindow: NSWindow?
    @State private var closeCoordinator: WindowCloseCoordinator?
    /// Set during teardown to prevent onChange from racing with onDisappear
    @State private var tearingDown: Bool = false
    @Environment(\.openWindow) private var openWindow

    private var columnVisibility: Binding<NavigationSplitViewVisibility> {
        Binding(
            get: { showSidebar ? .all : .detailOnly },
            set: { showSidebar = ($0 != .detailOnly) }
        )
    }

    var body: some View {
        NavigationSplitView(columnVisibility: columnVisibility) {
            VStack(spacing: 0) {
                SidebarModeToolbar(selectedMode: $sidebarMode)
                Divider()
                SidebarModeContent(
                    mode: sidebarMode,
                    discClient: discClient,
                    keyboardMappingManager: keyboardMappingManager,
                    audioClient: audioClient,
                    audioMixerState: audioMixerState
                )
            }
            .background(Color(nsColor: .windowBackgroundColor))
            .navigationSplitViewColumnWidth(min: 180, ideal: 260, max: 500)
        } detail: {
            VStack(spacing: 0) {
                ZStack {
                    // Emulator display (receives frames directly via videoClient.renderer)
                    EmulatorView(
                        videoClient: videoClient,
                        keyboardClient: keyboardClient,
                        indicatorClient: indicatorClient,
                        bbcKeyCache: keyboardMappingManager.bbcKeyCache
                    )

                    // Status overlay when not connected
                    if videoClient.connectionState != .connected {
                        statusOverlay
                    }
                }

                // Status bar at bottom
                if showStatusBar {
                    StatusBarView(
                        systemClient: systemClient,
                        indicatorClient: indicatorClient,
                        keyboardClient: keyboardClient,
                        keyboardMappingManager: keyboardMappingManager,
                        machineManager: MachineManager.shared,
                        connectionTarget: videoClient.target,
                        onUnlink: { unlinkCurrentMachine() }
                    )
                }
            }
            .frame(minWidth: 320, minHeight: 240)
        }
        .navigationSplitViewStyle(.balanced)
        .navigationTitle("Beebium")
        .animation(.default, value: showSidebar)
        .focusedValue(\.sidebarMode, $sidebarMode)
        .focusedValue(\.showSidebar, $showSidebar)
        .focusedValue(\.showStatusBar, $showStatusBar)
        .focusedValue(\.openNewWindow) { openWindow(id: "main") }
        .onAppear {
            // Capture openWindow into shared AppActions for FileCommands
            AppActions.shared.openNewMachine = { [openWindow] in openWindow(id: "new-machine") }
            AppActions.shared.openConnect = { [openWindow] in openWindow(id: "connect") }

            // Wire up keyboard client to mapping manager
            keyboardClient.mappingManager = keyboardMappingManager

            // Wire up audio mixer state to audio client
            audioMixerState.audioClient = audioClient

            // Set up initial Caps Lock sync callback (triggered on first LED update after MOS boot)
            indicatorClient.onInitialCapsLockSync = { [weak keyboardClient, weak indicatorClient] in
                guard let keyboardClient = keyboardClient,
                      let indicatorClient = indicatorClient else { return }
                let macCapsLockIsOn = NSEvent.modifierFlags.contains(.capsLock)
                keyboardClient.syncCapsLockState(
                    macCapsLockIsOn: macCapsLockIsOn,
                    bbcState: indicatorClient.capsLockState
                )
            }

            // Connection target was passed from MainWindowRouter (which consumed the
            // pending target from ConnectWindowState). Connect immediately.
            needsRun = initialNeedsRun
            NSLog("[ContentView] onAppear: target=%@, needsRun=%d, provenanceUUID=%@",
                  initialTarget.address, initialNeedsRun ? 1 : 0, initialProvenanceUUID ?? "nil")
            videoClient.reconnect(to: initialTarget)
        }
        .onDisappear {
            NSLog("[ContentView] onDisappear fired for address %@", videoClient.target.address)
            tearingDown = true
            let address = videoClient.target.address
            let machineManager = MachineManager.shared

            // Dump MachineManager state for diagnostics
            NSLog("[ContentView] onDisappear: MachineManager has %d machines:", machineManager.machines.count)
            for (id, m) in machineManager.machines {
                NSLog("[ContentView]   %@ -> %@ linked=%d running=%d clientCount=%d",
                      id.uuidString, m.target.address, m.lifetimeLinked ? 1 : 0,
                      m.process.isRunning ? 1 : 0, m.lastKnownClientCount)
            }

            let action = machineManager.windowCloseAction(
                forAddress: address,
                clientCount: systemClient.clientCount
            )
            NSLog("[ContentView] onDisappear: windowCloseAction=%@ for address=%@ clientCount=%d",
                  String(describing: action), address, systemClient.clientCount)

            if case .shutdownServer = action {
                // Synchronous SIGTERM — no gRPC channel needed, no async races
                let result = machineManager.shutdownServer(forAddress: address)
                NSLog("[ContentView] onDisappear: shutdownServer returned %d", result ? 1 : 0)
            }

            ConnectionRegistry.shared.unregister(address: address)
            debuggerClient.disconnect()
            audioClient.disconnect()
            discClient.disconnect()
            indicatorClient.disconnect()
            systemClient.disconnect()
            keyboardClient.disconnect()
            videoClient.disconnect()
        }
        .background(WindowAccessor(window: $currentWindow))
        .onChange(of: currentWindow) { window in
            guard let window = window else {
                NSLog("[ContentView] onChange(currentWindow): window is nil")
                return
            }
            NSLog("[ContentView] onChange(currentWindow): window='%@'", window.title)
            // Install close-button interception for the multi-client dialog case.
            // SwiftUI's WindowGroup manages its own window delegate, so we cannot
            // rely on windowShouldClose. Instead, redirect the close button's action
            // to our coordinator, which can show an alert and prevent/allow the close.
            if closeCoordinator == nil {
                let coordinator = WindowCloseCoordinator(
                    systemClient: systemClient,
                    videoClient: videoClient,
                    machineManager: MachineManager.shared,
                    window: window,
                    disconnectClients: { [weak videoClient, weak keyboardClient, weak systemClient,
                                          weak indicatorClient, weak discClient, weak audioClient,
                                          weak debuggerClient] in
                        let address = videoClient?.target.address ?? ""
                        NSLog("[ContentView] disconnectClients for %@", address)
                        ConnectionRegistry.shared.unregister(address: address)
                        debuggerClient?.disconnect()
                        audioClient?.disconnect()
                        discClient?.disconnect()
                        indicatorClient?.disconnect()
                        systemClient?.disconnect()
                        keyboardClient?.disconnect()
                        videoClient?.disconnect()
                    }
                )
                coordinator.install(on: window)
                closeCoordinator = coordinator
                NSLog("[ContentView] closeCoordinator created and installed")
            } else {
                NSLog("[ContentView] closeCoordinator already exists, skipping install")
            }
        }
        .onChange(of: videoClient.connectionState) { newState in
            // Connect clients when video client connects
            if case .connected = newState, let channel = videoClient.channel {
                NSLog("[ContentView] videoClient connected to %@, provenanceUUID=%@",
                      videoClient.target.address, initialProvenanceUUID ?? "nil")
                keyboardClient.connect(channel: channel)
                systemClient.connect(channel: channel, provenanceUUID: initialProvenanceUUID)
                indicatorClient.connect(channel: channel)
                discClient.connect(channel: channel)
                audioClient.connect(channel: channel)
                debuggerClient.connect(channel: channel)

                // Register this connection
                if let window = currentWindow {
                    ConnectionRegistry.shared.register(
                        address: videoClient.target.address,
                        window: window
                    )
                }

                // Verify coordinator installation and dump MachineManager state
                if let coordinator = closeCoordinator {
                    coordinator.verifyInstallation()
                } else {
                    NSLog("[ContentView] WARNING: closeCoordinator is nil at connection time")
                }
                let mm = MachineManager.shared
                NSLog("[ContentView] MachineManager state at connect: %d machines", mm.machines.count)
                for (id, m) in mm.machines {
                    NSLog("[ContentView]   %@ -> %@ linked=%d running=%d",
                          id.uuidString, m.target.address, m.lifetimeLinked ? 1 : 0,
                          m.process.isRunning ? 1 : 0)
                }

                // If this was a freshly launched core with --wait=api, start emulation
                if needsRun {
                    needsRun = false
                    Task {
                        do {
                            try await debuggerClient.run()
                        } catch {
                            NSLog("[ContentView] Failed to start emulation: \(error)")
                        }
                    }
                }

                // Load keyboard mappings from core
                Task {
                    await keyboardClient.loadKeyMappings()
                }

                // Sync Caps Lock state on connection (core may already be running)
                let macCapsLockIsOn = NSEvent.modifierFlags.contains(.capsLock)
                keyboardClient.syncCapsLockState(
                    macCapsLockIsOn: macCapsLockIsOn,
                    bbcState: indicatorClient.capsLockState
                )
            } else if !tearingDown {
                // Handle unexpected disconnection (server dropped connection).
                // Skip during teardown — onDisappear handles orderly shutdown.
                if case .disconnected = newState {
                    ConnectionRegistry.shared.unregister(address: videoClient.target.address)

                    debuggerClient.disconnect()
                    audioClient.disconnect()
                    discClient.disconnect()
                    indicatorClient.disconnect()
                    keyboardClient.disconnect()
                    systemClient.disconnect()
                } else if case .error = newState {
                    ConnectionRegistry.shared.unregister(address: videoClient.target.address)

                    debuggerClient.disconnect()
                    audioClient.disconnect()
                    discClient.disconnect()
                    indicatorClient.disconnect()
                    keyboardClient.disconnect()
                    systemClient.disconnect()
                }
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { _ in
            // Sync Caps Lock state when window gains focus
            // (macOS Caps Lock may have changed while we were unfocused)
            let macCapsLockIsOn = NSEvent.modifierFlags.contains(.capsLock)
            keyboardClient.syncCapsLockState(
                macCapsLockIsOn: macCapsLockIsOn,
                bbcState: indicatorClient.capsLockState
            )
        }
        .onChange(of: systemClient.clientCount) { count in
            // Keep MachineManager's cached client count in sync for the quit handler
            MachineManager.shared.updateClientCount(
                address: videoClient.target.address,
                count: count
            )
        }
        .onChange(of: keyboardMappingManager.isCapsLockSyncEnabled) { isEnabled in
            // Sync immediately when user enables Caps Lock sync
            if isEnabled {
                let macCapsLockIsOn = NSEvent.modifierFlags.contains(.capsLock)
                keyboardClient.syncCapsLockState(
                    macCapsLockIsOn: macCapsLockIsOn,
                    bbcState: indicatorClient.capsLockState
                )
            }
        }
    }

    /// Unlink this window's machine from the app's lifecycle
    private func unlinkCurrentMachine() {
        if let machine = MachineManager.shared.machine(forTarget: videoClient.target) {
            MachineManager.shared.unlink(id: machine.id)
        }
    }

    /// Connect to a different machine target
    private func connectTo(_ target: ConnectionTarget) {
        // Reconnect video client to new target
        // The onChange handler will automatically cascade to other clients
        videoClient.reconnect(to: target)
    }

    @ViewBuilder
    private var statusOverlay: some View {
        VStack(spacing: 16) {
            switch videoClient.connectionState {
            case .disconnected:
                Text("Disconnected")
                    .font(.headline)
                Button("Connect") {
                    videoClient.connect()
                }

            case .connecting:
                ProgressView()
                    .scaleEffect(1.5)
                Text("Connecting to \(videoClient.target.address)...")
                    .font(.headline)

            case .connected:
                EmptyView()

            case .error(let message):
                Image(systemName: "exclamationmark.triangle")
                    .font(.system(size: 48))
                    .foregroundColor(.yellow)
                Text("Connection Error")
                    .font(.headline)
                Text(message)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal)
                Button("Retry") {
                    videoClient.disconnect()
                    videoClient.connect()
                }
                .padding(.top, 8)
            }
        }
        .padding(32)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))
    }
}

#if DEBUG
struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView(
            initialTarget: ConnectionTarget(host: "127.0.0.1", port: 50051),
            initialNeedsRun: false,
            initialProvenanceUUID: nil,
            keyboardMappingManager: KeyboardMappingManager()
        )
    }
}
#endif

// MARK: - Window Accessor

/// NSView subclass that reports window attachment synchronously via viewDidMoveToWindow(),
/// firing before the window is rendered on screen.
class WindowObserverView: NSView {
    var onWindowChanged: ((NSWindow?) -> Void)?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        onWindowChanged?(window)
    }
}

/// Helper view to capture the NSWindow reference from SwiftUI into an @Binding.
/// Uses viewDidMoveToWindow for synchronous capture before the first render.
struct WindowAccessor: NSViewRepresentable {
    @Binding var window: NSWindow?

    func makeNSView(context: Context) -> WindowObserverView {
        let view = WindowObserverView()
        view.onWindowChanged = { newWindow in
            DispatchQueue.main.async {
                self.window = newWindow
            }
        }
        return view
    }

    func updateNSView(_ nsView: WindowObserverView, context: Context) {
        nsView.onWindowChanged = { newWindow in
            DispatchQueue.main.async {
                self.window = newWindow
            }
        }
    }
}

// MARK: - Window Close Coordinator

/// Intercepts the window close button to implement lifecycle-aware behavior.
///
/// SwiftUI's WindowGroup manages its own NSWindowDelegate, so we cannot rely on
/// windowShouldClose. Instead, this coordinator redirects the close button's
/// target/action to itself, enabling pre-close logic.
///
/// All decision logic is delegated to MachineManager.windowCloseAction().
/// This coordinator only handles the UI flow: close immediately, or show
/// a multi-client alert before closing.
@MainActor
class WindowCloseCoordinator: NSObject {
    let systemClient: SystemClient
    let videoClient: VideoClient
    let machineManager: MachineManager
    let disconnectClients: () -> Void
    weak var window: NSWindow?

    init(systemClient: SystemClient, videoClient: VideoClient, machineManager: MachineManager, window: NSWindow, disconnectClients: @escaping () -> Void) {
        self.systemClient = systemClient
        self.videoClient = videoClient
        self.machineManager = machineManager
        self.disconnectClients = disconnectClients
        self.window = window
    }

    /// Redirect the window's close button to our handler
    func install(on window: NSWindow) {
        if let closeButton = window.standardWindowButton(.closeButton) {
            let oldTarget = closeButton.target
            let oldAction = closeButton.action
            closeButton.target = self
            closeButton.action = #selector(handleCloseButton(_:))
            NSLog("[WindowCloseCoordinator] Installed on window '%@'. Close button: oldTarget=%@ oldAction=%@ newTarget=WindowCloseCoordinator",
                  window.title,
                  String(describing: oldTarget), String(describing: oldAction))
        } else {
            NSLog("[WindowCloseCoordinator] WARNING: No close button found on window '%@'",
                  window.title)
        }
    }

    /// Check whether this coordinator is still installed as the close button target
    func verifyInstallation() -> Bool {
        guard let window = window,
              let closeButton = window.standardWindowButton(.closeButton) else {
            NSLog("[WindowCloseCoordinator] verifyInstallation: window or close button is nil")
            return false
        }
        let isInstalled = closeButton.target === self
        NSLog("[WindowCloseCoordinator] verifyInstallation: target=%@ expected=%@ match=%d",
              String(describing: closeButton.target),
              String(describing: self),
              isInstalled ? 1 : 0)
        return isInstalled
    }

    @objc private func handleCloseButton(_ sender: Any?) {
        guard let window = window else {
            NSLog("[WindowCloseCoordinator] handleCloseButton: window is nil")
            return
        }
        let address = videoClient.target.address
        let clientCount = systemClient.clientCount
        NSLog("[WindowCloseCoordinator] handleCloseButton for address %@, clientCount %d", address, clientCount)

        let action = machineManager.windowCloseAction(forAddress: address, clientCount: clientCount)

        switch action {
        case .shutdownServer:
            // Disconnect gRPC clients first — the server's graceful shutdown
            // waits for active streams to close, so we must drop them before
            // sending SIGTERM. onDisappear does NOT fire for WindowGroup windows.
            disconnectClients()
            machineManager.shutdownServer(forAddress: address)
            window.close()
        case .disconnect:
            disconnectClients()
            window.close()
        case .promptUser(let count):
            showMultiClientAlert(window: window, address: address, clientCount: count)
        }
    }

    @MainActor
    private func showMultiClientAlert(window: NSWindow, address: String, clientCount: Int) {
        let machineName = systemClient.machineDisplayName.isEmpty
            ? "This machine"
            : "\"\(systemClient.machineDisplayName)\""

        let alert = NSAlert()
        alert.messageText = "\(machineName) has \(clientCount - 1) other connected client\(clientCount - 1 == 1 ? "" : "s")."
        alert.informativeText = "You can shut down the machine (disconnecting other clients) or leave it running."
        alert.alertStyle = .informational

        let shutDownButton = alert.addButton(withTitle: "Shut Down")
        shutDownButton.hasDestructiveAction = true
        alert.addButton(withTitle: "Leave Running")
        alert.addButton(withTitle: "Cancel")

        alert.beginSheetModal(for: window) { [weak self] response in
            guard let self = self else { return }

            Task { @MainActor in
                switch response {
                case .alertFirstButtonReturn:
                    // Shut Down: gRPC shutdown (notifies other clients), then close
                    await self.machineManager.gracefulShutdownServer(forAddress: address, using: self.systemClient)
                    self.disconnectClients()
                    window.close()
                case .alertSecondButtonReturn:
                    // Leave Running: unlink, then close
                    if let machine = self.machineManager.machine(forAddress: address) {
                        self.machineManager.unlink(id: machine.id)
                    }
                    self.disconnectClients()
                    window.close()
                default:
                    break
                }
            }
        }
    }
}
