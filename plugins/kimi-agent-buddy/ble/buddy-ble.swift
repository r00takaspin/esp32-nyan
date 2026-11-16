// buddy-ble: line bridge between stdin/stdout and the Agent Buddy's BLE link.
//
// The board exposes a Nordic-UART-style service next to its HID keyboard.
// macOS keeps the keyboard connected on its own; we attach to that same
// connection (retrieveConnectedPeripherals) or scan for the board.
//
//   stdin  : protocol lines for the board ("S|ping")
//   stdout : lines from the board ("I|fw=2.0|...") and "#..." status lines:
//            #scanning  #connected <name>  #disconnected  #off  #unauthorized
//
// Built by the plugin with swiftc; Info.plist is embedded so macOS can ask
// for Bluetooth permission (a CLI without NSBluetoothAlwaysUsageDescription
// is killed on first CoreBluetooth use).
import CoreBluetooth
import Foundation

let svcUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
let rxUUID  = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
let txUUID  = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")

setvbuf(stdout, nil, _IOLBF, 0)
func say(_ s: String) { print(s); fflush(stdout) }

final class Bridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var board: CBPeripheral?
    var rx: CBCharacteristic?
    var inbox = Data()
    var pending: [Data] = []
    let q = DispatchQueue(label: "buddy-ble")

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: q)
    }

    // ---- discovery ----
    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        switch c.state {
        case .poweredOn: find()
        case .unauthorized: say("#unauthorized")
        default: say("#off")
        }
    }

    func find() {
        guard central.state == .poweredOn, board == nil else { return }
        // Already connected by the system as a keyboard: share that link.
        // macOS caches a bonded device's GATT table, so after a firmware update
        // the link service may not be listed yet: also match the board's
        // standard services and check the name.
        let known = [svcUUID, CBUUID(string: "180F"), CBUUID(string: "180A"), CBUUID(string: "1812")]
        if let p = central.retrieveConnectedPeripherals(withServices: known)
            .first(where: { ($0.name ?? "").hasPrefix("AgentBuddy") }) {
            attach(p)
            return
        }
        say("#scanning")
        central.scanForPeripherals(withServices: nil, options: nil)
        // Retry the connected-list now and then: macOS may reconnect the
        // keyboard before our scan sees an advertisement.
        q.asyncAfter(deadline: .now() + 3) { [weak self] in
            guard let self = self, self.board == nil else { return }
            self.central.stopScan()
            self.find()
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData ad: [String: Any], rssi: NSNumber) {
        let name = (ad[CBAdvertisementDataLocalNameKey] as? String) ?? p.name ?? ""
        guard name.hasPrefix("AgentBuddy") else { return }
        c.stopScan()
        attach(p)
    }

    func attach(_ p: CBPeripheral) {
        board = p
        p.delegate = self
        central.connect(p, options: nil)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        p.discoverServices([svcUUID])
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        lost()
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        lost()
    }

    func lost() {
        if rx != nil { say("#disconnected") }
        board = nil
        rx = nil
        inbox.removeAll()
        q.asyncAfter(deadline: .now() + 2) { [weak self] in self?.find() }
    }

    // ---- GATT ----
    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = p.services?.first(where: { $0.uuid == svcUUID }) else {
            central.cancelPeripheralConnection(p)   // old firmware without the link service
            return
        }
        p.discoverCharacteristics([rxUUID, txUUID], for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        for ch in s.characteristics ?? [] {
            if ch.uuid == txUUID { p.setNotifyValue(true, for: ch) }
            if ch.uuid == rxUUID { rx = ch }
        }
    }

    func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor ch: CBCharacteristic, error: Error?) {
        guard ch.uuid == txUUID, ch.isNotifying, rx != nil else { return }
        say("#connected \(p.name ?? "AgentBuddy")")
        let queued = pending
        pending.removeAll()
        queued.forEach { write($0) }
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        guard ch.uuid == txUUID, let v = ch.value else { return }
        inbox.append(v)
        while let nl = inbox.firstIndex(of: 0x0A) {
            let line = inbox.subdata(in: inbox.startIndex..<nl)
            inbox.removeSubrange(inbox.startIndex...nl)
            if let s = String(data: line, encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines), !s.isEmpty {
                say(s)
            }
        }
    }

    // ---- host -> board ----
    func send(_ line: String) {
        q.async {
            guard let d = (line + "\n").data(using: .utf8) else { return }
            if self.rx == nil {
                if self.pending.count < 20 { self.pending.append(d) }
                return
            }
            self.write(d)
        }
    }

    func write(_ d: Data) {
        guard let p = board, let rx = rx else { return }
        let type: CBCharacteristicWriteType = rx.properties.contains(.writeWithoutResponse) ? .withoutResponse : .withResponse
        let max = max(20, p.maximumWriteValueLength(for: type))
        var off = 0
        while off < d.count {
            let end = min(off + max, d.count)
            p.writeValue(d.subdata(in: off..<end), for: rx, type: type)
            off = end
        }
    }
}

let bridge = Bridge()
DispatchQueue.global().async {
    while let line = readLine(strippingNewline: true) {
        if !line.isEmpty { bridge.send(line) }
    }
    exit(0)   // the daemon went away
}
RunLoop.main.run()
