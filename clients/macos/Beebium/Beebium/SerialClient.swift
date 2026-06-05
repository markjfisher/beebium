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

import Foundation
import GRPC

// Observes the BBC's on-board serial hardware (MC6850 ACIA + Serial ULA) via the
// core SerialService WatchSerialStatus stream. Used to show the serial port's own
// interface state (connector standard + RS423 TX/RX baud) above the serial
// extension panels in the Peripherals sidebar.
@MainActor
final class SerialClient: ObservableObject, Disconnectable {

    @Published private(set) var hasSerialSocket: Bool = false
    // Static, machine-defined connector standard ("RS423" / "RS232").
    @Published private(set) var connector: String = ""
    // RS423 TX/RX baud, captured from the most recent status where RS423 (not
    // cassette) was the selected mode. The SERPROC shares one pair of baud bits
    // between RS423 and cassette; we never surface the cassette rate here, so we
    // only adopt the rate while RS423 is selected and hold it otherwise.
    @Published private(set) var rs423TxBaud: UInt32 = 0
    @Published private(set) var rs423RxBaud: UInt32 = 0
    @Published private(set) var hasRs423Rates: Bool = false

    private var client: Beebium_SerialServiceNIOClient?
    private var statusStreamCall: ServerStreamingCall<
        Beebium_WatchSerialStatusRequest, Beebium_SerialStatus>?

    func connect(channel: GRPCChannel) {
        client = Beebium_SerialServiceNIOClient(channel: channel)
        startStatusStream()
    }

    func disconnect() {
        statusStreamCall?.cancel(promise: nil)
        statusStreamCall = nil
        client = nil
        hasSerialSocket = false
        connector = ""
        rs423TxBaud = 0
        rs423RxBaud = 0
        hasRs423Rates = false
    }

    // MARK: - Private

    private func startStatusStream() {
        guard let client = client else { return }

        let request = Beebium_WatchSerialStatusRequest()
        let call = client.watchSerialStatus(request) { [weak self] status in
            Task { @MainActor [weak self] in
                self?.handleStatusUpdate(status)
            }
        }
        statusStreamCall = call

        call.status.whenComplete { [weak self] result in
            Task { @MainActor [weak self] in
                if case .failure(let error) = result {
                    NSLog("[SerialClient] Status stream error: %@",
                          error.localizedDescription)
                }
                self?.statusStreamCall = nil
            }
        }
    }

    private func handleStatusUpdate(_ status: Beebium_SerialStatus) {
        hasSerialSocket = status.hasSerialSocket_p
        connector = status.connector
        if status.rs423Selected {
            rs423TxBaud = status.txBaud
            rs423RxBaud = status.rxBaud
            hasRs423Rates = true
        }
    }
}
