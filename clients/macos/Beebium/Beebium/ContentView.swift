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

/// Main content view displaying the emulator output
struct ContentView: View {
    @StateObject private var videoClient = VideoClient()
    @StateObject private var keyboardClient = KeyboardClient()
    @StateObject private var systemClient = SystemClient()
    @StateObject private var indicatorClient = IndicatorClient()
    @StateObject private var discClient = DiscClient()
    @StateObject private var audioClient = AudioClient()
    @StateObject private var audioMixerState = AudioMixerState()
    @Binding var showStatusBar: Bool
    @Binding var showSidebar: Bool
    @ObservedObject var keyboardMappingManager: KeyboardMappingManager
    @State private var sidebarMode: SidebarMode = .storage
    @State private var showConnectDialog = false
    @State private var showNewMachineDialog = false
    @State private var currentWindow: NSWindow?
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
                    EmulatorView(videoClient: videoClient, keyboardClient: keyboardClient)

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
                        keyboardMappingManager: keyboardMappingManager
                    )
                }
            }
            .frame(minWidth: 320, minHeight: 240)
        }
        .navigationSplitViewStyle(.balanced)
        .navigationTitle("Beebium")
        .animation(.default, value: showSidebar)
        .focusedValue(\.sidebarMode, $sidebarMode)
        .focusedValue(\.showConnectDialog, $showConnectDialog)
        .focusedValue(\.showNewMachineDialog, $showNewMachineDialog)
        .focusedValue(\.openNewWindow) { openWindow(id: "main") }
        .onAppear {
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

            // Check for pending connection target (set by Connect dialog)
            if let target = ConnectWindowState.shared.consumePendingTarget() {
                videoClient.reconnect(to: target)
            } else {
                videoClient.connect()
            }
        }
        .onDisappear {
            // Unregister connection when window closes
            ConnectionRegistry.shared.unregister(address: videoClient.target.address)

            audioClient.disconnect()
            discClient.disconnect()
            indicatorClient.disconnect()
            systemClient.disconnect()
            keyboardClient.disconnect()
            videoClient.disconnect()
        }
        .background(WindowAccessor(window: $currentWindow))
        .onChange(of: showConnectDialog) { show in
            if show {
                openWindow(id: "connect")
                showConnectDialog = false  // Reset the trigger
            }
        }
        .sheet(isPresented: $showNewMachineDialog) {
            NewMachineDialog()
        }
        .onChange(of: videoClient.connectionState) { newState in
            // Connect clients when video client connects
            if case .connected = newState, let channel = videoClient.channel {
                keyboardClient.connect(channel: channel)
                systemClient.connect(channel: channel)
                indicatorClient.connect(channel: channel)
                discClient.connect(channel: channel)
                audioClient.connect(channel: channel)

                // Register this connection
                if let window = currentWindow {
                    ConnectionRegistry.shared.register(
                        address: videoClient.target.address,
                        window: window
                    )
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
            } else if case .disconnected = newState {
                // Unregister this connection
                ConnectionRegistry.shared.unregister(address: videoClient.target.address)

                audioClient.disconnect()
                discClient.disconnect()
                indicatorClient.disconnect()
                keyboardClient.disconnect()
                systemClient.disconnect()
            } else if case .error = newState {
                // Unregister this connection
                ConnectionRegistry.shared.unregister(address: videoClient.target.address)

                audioClient.disconnect()
                discClient.disconnect()
                indicatorClient.disconnect()
                keyboardClient.disconnect()
                systemClient.disconnect()
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
            showStatusBar: .constant(true),
            showSidebar: .constant(true),
            keyboardMappingManager: KeyboardMappingManager()
        )
    }
}
#endif

// MARK: - Window Accessor

/// Helper view to capture the NSWindow reference
struct WindowAccessor: NSViewRepresentable {
    @Binding var window: NSWindow?

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async {
            self.window = view.window
        }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        DispatchQueue.main.async {
            if self.window !== nsView.window {
                self.window = nsView.window
            }
        }
    }
}
