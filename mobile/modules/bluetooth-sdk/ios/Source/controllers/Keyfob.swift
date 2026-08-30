//
//  Keyfob.swift
//  MentraOS_Manager
//
//  Unofficial MentraOS controller for Seeed Studio XIAO nRF52840 Plus.
//  Not a Seeed product; not affiliated with or endorsed by Seeed Studio.
//  Protocol shared with firmware/keyfob/settings.h and KeyfobProtocol.kt.
//

import CoreBluetooth
import Foundation

private enum KeyfobBLE {
    static let SERVICE_UUID = CBUUID(string: "d4b2c520-8f1e-4c7a-9b03-6a5d4e80e020")
    static let CTRL_UUID = CBUUID(string: "d4b2c520-8f1e-4c7a-9b03-6a5d4e80e021")
    static let EVT_UUID = CBUUID(string: "d4b2c520-8f1e-4c7a-9b03-6a5d4e80e022")
    static let NAME_PREFIX = "Keyfob"
    static let HDR_LEN = 4
    static let CMD_LED: UInt8 = 0x01
    static let CMD_PING: UInt8 = 0x02
    static let EVT_BATTERY: UInt8 = 0x81
    static let EVT_READY: UInt8 = 0x82
    static let EVT_GESTURE: UInt8 = 0x83
    static let SCAN_TIMEOUT: TimeInterval = 15.0

    static func matchesName(_ name: String?) -> Bool {
        guard let name, !name.trimmingCharacters(in: .whitespaces).isEmpty else { return false }
        return name.lowercased().hasPrefix(NAME_PREFIX.lowercased())
    }

    static func gestureName(_ id: UInt8) -> String? {
        switch id {
        case 0x01: return "hold"
        case 0x02: return "single_tap"
        case 0x03: return "double_tap"
        case 0x04: return "swipe_up"
        case 0x05: return "swipe_down"
        default: return nil
        }
    }

    static func encode(opcode: UInt8, seq: UInt8, payload: Data = Data()) -> Data {
        var out = Data(count: HDR_LEN + payload.count)
        out[0] = opcode
        out[1] = seq
        out[2] = UInt8(payload.count & 0xFF)
        out[3] = UInt8((payload.count >> 8) & 0xFF)
        if !payload.isEmpty {
            out.replaceSubrange(HDR_LEN ..< (HDR_LEN + payload.count), with: payload)
        }
        return out
    }

    static func decode(_ packet: Data?) -> (UInt8, UInt8, Data)? {
        guard let packet, packet.count >= HDR_LEN else { return nil }
        let len = Int(packet[2]) | (Int(packet[3]) << 8)
        let end = min(HDR_LEN + len, packet.count)
        return (packet[0], packet[1], packet.subdata(in: HDR_LEN ..< end))
    }
}

@MainActor
class Keyfob: NSObject, ControllerManager {
    var type = ControllerTypes.KEYFOB
    let hasMic = false

    private var centralManager: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var ctrlChar: CBCharacteristic?
    private var evtChar: CBCharacteristic?
    private var isDisconnecting = false
    private var seq: UInt8 = 1
    var DEVICE_SEARCH_ID = "NOT_SET"
    private var discoveredNames = Set<String>()
    private var scanTimeoutWork: DispatchWorkItem?

    private var savedUUID: UUID? {
        get { UserDefaults.standard.string(forKey: "keyfob_uuid").flatMap { UUID(uuidString: $0) } }
        set {
            if let v = newValue {
                UserDefaults.standard.set(v.uuidString, forKey: "keyfob_uuid")
            } else {
                UserDefaults.standard.removeObject(forKey: "keyfob_uuid")
            }
        }
    }

    private var savedName: String? {
        get { UserDefaults.standard.string(forKey: "keyfob_name") }
        set { UserDefaults.standard.set(newValue, forKey: "keyfob_name") }
    }

    static let bluetoothQueue = DispatchQueue(label: "BluetoothKeyfob", qos: .userInitiated)

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: Keyfob.bluetoothQueue)
    }

    func findCompatibleDevices() {
        Bridge.log("Keyfob: findCompatibleDevices()")
        DEVICE_SEARCH_ID = "NOT_SET"
        discoveredNames.removeAll()
        startScan()
    }

    func connectById(_ id: String) {
        Bridge.log("Keyfob: connectById(\(id))")
        DEVICE_SEARCH_ID = id
        if connectBySavedUUID(id) { return }
        startScan()
    }

    func stopScan() {
        scanTimeoutWork?.cancel()
        scanTimeoutWork = nil
        centralManager?.stopScan()
    }

    private func startScan() {
        guard let centralManager else { return }
        if centralManager.state != .poweredOn {
            Bridge.log("Keyfob: Bluetooth not powered on")
            return
        }
        if peripheral != nil {
            Bridge.log("Keyfob: Already connected, skipping scan")
            return
        }
        isDisconnecting = false
        stopScan()
        centralManager.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        let work = DispatchWorkItem { [weak self] in
            self?.stopScan()
        }
        scanTimeoutWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + KeyfobBLE.SCAN_TIMEOUT, execute: work)
    }

    private func connectBySavedUUID(_ id: String) -> Bool {
        guard let uuid = savedUUID, let centralManager else { return false }
        let known = savedName
        let matches =
            id.isEmpty || id == "NOT_SET" || uuid.uuidString.caseInsensitiveCompare(id) == .orderedSame
            || (known?.caseInsensitiveCompare(id) == .orderedSame)
        guard matches else { return false }
        let retrieved = centralManager.retrievePeripherals(withIdentifiers: [uuid])
        guard let target = retrieved.first else { return false }
        connect(target)
        return true
    }

    private func connect(_ peripheral: CBPeripheral) {
        stopScan()
        self.peripheral = peripheral
        peripheral.delegate = self
        savedUUID = peripheral.identifier
        centralManager?.connect(peripheral, options: nil)
        Bridge.log("Keyfob: Connecting to \(peripheral.name ?? peripheral.identifier.uuidString)")
    }

    private func nextSeq() -> UInt8 {
        let current = seq
        seq = seq == 255 ? 1 : seq &+ 1
        return current
    }

    private func enqueueControl(opcode: UInt8, payload: Data = Data()) {
        guard let ctrlChar, let peripheral else { return }
        let packet = KeyfobBLE.encode(opcode: opcode, seq: nextSeq(), payload: payload)
        peripheral.writeValue(packet, for: ctrlChar, type: .withResponse)
    }

    private func markReady() {
        if let name = peripheral?.name, !name.isEmpty {
            savedName = name
            DeviceStore.shared.apply("bluetooth", "controller_device_name", name)
        }
        if let uuid = peripheral?.identifier {
            savedUUID = uuid
            DeviceStore.shared.apply("glasses", "controllerMacAddress", uuid.uuidString)
        }
        DeviceStore.shared.apply("glasses", "controllerConnected", true)
        DeviceStore.shared.apply("glasses", "controllerFullyBooted", true)
        Bridge.log("Keyfob: ready")
    }

    private func resetControllerState() {
        DeviceStore.shared.apply("glasses", "controllerConnected", false)
        DeviceStore.shared.apply("glasses", "controllerFullyBooted", false)
    }

    func disconnect() {
        Bridge.log("Keyfob: disconnect()")
        isDisconnecting = true
        stopScan()
        resetControllerState()
        if let peripheral {
            centralManager?.cancelPeripheralConnection(peripheral)
        }
        self.peripheral = nil
        ctrlChar = nil
        evtChar = nil
    }

    func forget() {
        disconnect()
        savedUUID = nil
        savedName = nil
        DEVICE_SEARCH_ID = "NOT_SET"
    }

    func cleanup() {
        disconnect()
    }

    func getConnectedBluetoothName() -> String? {
        peripheral?.name
    }

    func ping() {
        enqueueControl(opcode: KeyfobBLE.CMD_PING)
    }

    func getBatteryStatus() {
        enqueueControl(opcode: KeyfobBLE.CMD_PING)
    }

    func sendRgbLedControl(
        requestId _: String, packageName _: String?, action: String, color: String?, onDurationMs _: Int,
        offDurationMs _: Int, count _: Int
    ) {
        let rgb: Data
        if action.lowercased() == "off" {
            rgb = Data([0, 0, 0])
        } else {
            rgb = parseRgb(color)
        }
        enqueueControl(opcode: KeyfobBLE.CMD_LED, payload: rgb)
    }

    private func parseRgb(_ color: String?) -> Data {
        let raw = color?.trimmingCharacters(in: .whitespaces) ?? ""
        if raw.hasPrefix("#"), raw.count >= 7 {
            let r = UInt8(raw.dropFirst().prefix(2), radix: 16) ?? 0
            let g = UInt8(raw.dropFirst(3).prefix(2), radix: 16) ?? 0
            let b = UInt8(raw.dropFirst(5).prefix(2), radix: 16) ?? 0
            return Data([r, g, b])
        }
        switch raw.lowercased() {
        case "red": return Data([255, 0, 0])
        case "green": return Data([0, 255, 0])
        case "blue": return Data([0, 0, 255])
        default: return Data([255, 255, 255])
        }
    }

    func sendIncidentId(_: String, apiBaseUrl _: String?) {}
    func setMicEnabled(_: Bool) {}
    func sortMicRanking(list: [String]) -> [String] { list }
    func sendJson(_: [String: Any], wakeUp _: Bool, requireAck _: Bool) {}
    func requestPhoto(_: PhotoRequest) {}
    func startVideoRecording(requestId _: String, save _: Bool, sound _: Bool) {}
    func stopVideoRecording(requestId _: String) {}
    func startStream(_: [String: Any]) {}
    func stopStream() {}
    func sendStreamKeepAlive(_: [String: Any]) {}
    func sendButtonPhotoSettings() {}
    func sendButtonVideoRecordingSettings() {}
    func sendButtonMaxRecordingTime() {}
    func setBrightness(_: Int, autoMode _: Bool) {}
    func clearDisplay() {}
    func sendTextWall(_: String) {}
    func sendDoubleTextWall(_: String, _: String) {}
    func displayBitmap(
        base64ImageData _: String, x _: Int32? = nil, y _: Int32? = nil, width _: Int32? = nil, height _: Int32? = nil
    ) async -> Bool {
        false
    }
    func showDashboard() {}
    func setDashboardPosition(_: Int, _: Int) {}
    func setHeadUpAngle(_: Int) {}
    func setSilentMode(_: Bool) {}
    func exit() {}
    func sendShutdown() { disconnect() }
    func sendReboot() {}
    func requestWifiScan() {}
    func sendWifiCredentials(_: String, _: String) {}
    func forgetWifiNetwork(_: String) {}
    func sendHotspotState(_: Bool) {}
    func sendOtaStart(otaVersionUrl _: String?) {}
    func sendOtaQueryStatus() {}
    func sendUserEmailToGlasses(_: String) {}
    func queryGalleryStatus() {}
    func sendGalleryMode() {}
    func requestVersionInfo() {}
}

extension Keyfob: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        DispatchQueue.main.async { [weak self] in
            Bridge.log("Keyfob: Bluetooth state: \(central.state.rawValue)")
            if central.state == .poweredOn {
                self?.startScan()
            }
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let name = peripheral.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            guard KeyfobBLE.matchesName(name), let name else { return }
            if self.DEVICE_SEARCH_ID == "NOT_SET" {
                if self.discoveredNames.insert(name).inserted {
                    Bridge.sendDiscoveredDevice(
                        ControllerTypes.KEYFOB,
                        name,
                        deviceAddress: peripheral.identifier.uuidString,
                        rssi: RSSI.intValue
                    )
                }
                return
            }
            if self.peripheral != nil { return }
            let target = self.DEVICE_SEARCH_ID
            let matches =
                target.isEmpty
                || name.caseInsensitiveCompare(target) == .orderedSame
                || name.localizedCaseInsensitiveContains(target)
                || peripheral.identifier.uuidString.caseInsensitiveCompare(target) == .orderedSame
            guard matches else { return }
            self.connect(peripheral)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        DispatchQueue.main.async { [weak self] in
            Bridge.log("Keyfob: Connected to \(peripheral.name ?? "?")")
            peripheral.discoverServices([KeyfobBLE.SERVICE_UUID])
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?
    ) {
        DispatchQueue.main.async { [weak self] in
            Bridge.log("Keyfob: failed to connect: \(error?.localizedDescription ?? "unknown")")
            self?.resetControllerState()
            self?.peripheral = nil
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?
    ) {
        DispatchQueue.main.async { [weak self] in
            Bridge.log("Keyfob: disconnected \(error?.localizedDescription ?? "")")
            self?.resetControllerState()
            self?.peripheral = nil
            self?.ctrlChar = nil
            self?.evtChar = nil
        }
    }
}

extension Keyfob: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        DispatchQueue.main.async {
            if let error {
                Bridge.log("Keyfob: service discovery error: \(error.localizedDescription)")
                return
            }
            peripheral.services?.forEach { peripheral.discoverCharacteristics(nil, for: $0) }
        }
    }

    nonisolated func peripheral(
        _ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?
    ) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            if let error {
                Bridge.log("Keyfob: characteristic discovery error: \(error.localizedDescription)")
                return
            }
            for char in service.characteristics ?? [] {
                switch char.uuid {
                case KeyfobBLE.CTRL_UUID:
                    self.ctrlChar = char
                case KeyfobBLE.EVT_UUID:
                    self.evtChar = char
                    peripheral.setNotifyValue(true, for: char)
                default:
                    break
                }
            }
        }
    }

    nonisolated func peripheral(
        _ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?
    ) {
        DispatchQueue.main.async { [weak self] in
            if let error {
                Bridge.log("Keyfob: notify error: \(error.localizedDescription)")
                return
            }
            if characteristic.uuid == KeyfobBLE.EVT_UUID {
                self?.markReady()
            }
        }
    }

    nonisolated func peripheral(
        _: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error _: Error?
    ) {
        let data = characteristic.value
        let uuid = characteristic.uuid
        DispatchQueue.main.async { [weak self] in
            guard let self, uuid == KeyfobBLE.EVT_UUID, let decoded = KeyfobBLE.decode(data) else { return }
            let (opcode, _, payload) = decoded
            switch opcode {
            case KeyfobBLE.EVT_BATTERY:
                if let percent = payload.first {
                    DeviceStore.shared.apply("glasses", "controllerBatteryLevel", Int(percent))
                }
            case KeyfobBLE.EVT_READY:
                self.markReady()
            case KeyfobBLE.EVT_GESTURE:
                guard let first = payload.first, let name = KeyfobBLE.gestureName(first) else { return }
                let source = payload.count > 1 ? Int32(payload[1]) : nil
                Bridge.sendTouchEvent(
                    deviceModel: ControllerTypes.KEYFOB,
                    gestureName: name,
                    timestamp: Int64(Date().timeIntervalSince1970 * 1000),
                    source: source
                )
            default:
                break
            }
        }
    }
}
