//
//  S3Watch.swift
//
//  UNTESTED ALPHA — never compiled or run on a Mac / iPhone / watch pair.
//  Port of android/.../sgcs/S3Watch.kt. Pairing, GATT, JPEG, PCM, and HUD
//  rasterization are best-effort translations. Do not treat this as supported.
//
//  Unofficial MentraOS SGC for the Waveshare ESP32-S3-Touch-AMOLED-2.06.
//  Not a Waveshare product and not affiliated with or endorsed by Waveshare.
//

import CoreBluetooth
import CoreGraphics
import Foundation
#if canImport(UIKit)
import UIKit
#endif

@MainActor
class S3Watch: NSObject, SGCManager {
    private static let tag = "S3Watch"
    private static let scanDuration: TimeInterval = 15
    private static let jpegAckTimeout: TimeInterval = 0.8
    private static let bluetoothQueue = DispatchQueue(label: "BluetoothS3Watch", qos: .userInitiated)

    var type: String = DeviceTypes.S3_WATCH
    let hasMic = true

    private var centralManager: CBCentralManager?
    private var peripheral: CBPeripheral?
    private var ctrlChar: CBCharacteristic?
    private var evtChar: CBCharacteristic?
    private var imgChar: CBCharacteristic?
    private var micChar: CBCharacteristic?

    private var targetIdentifier: String?
    private var discoveredNames = Set<String>()
    private var scanTimeoutWork: DispatchWorkItem?
    private var connecting = false
    private var pendingNotifyCount = 0

    private var writeQueue: [WriteOp] = []
    private var writeInFlight = false
    private var currentOp: WriteOp?
    private var pendingJpeg: Data?
    private var jpegTransferActive = false
    private var pendingControls: [WriteOp] = []
    private var jpegAckTimeoutWork: DispatchWorkItem?

    private var seq: UInt8 = 1
    private var negotiatedChunk = 20
    private var pcmPackets = 0
    private var dashboardMenuItems: [S3WatchProtocol.MenuEntry] = []
    private var dashboardMenuPackages: [String] = []
    private var lastMenuSelectMs: Int64 = 0

    override init() {
        super.init()
        DeviceStore.shared.apply("glasses", "micEnabled", false)
        centralManager = CBCentralManager(delegate: self, queue: S3Watch.bluetoothQueue)
        Bridge.log("\(S3Watch.tag): UNTESTED ALPHA iOS SGC constructed")
    }

    func setMicEnabled(_ enabled: Bool) {
        Bridge.log("\(S3Watch.tag): setMicEnabled \(enabled)")
        DeviceStore.shared.apply("glasses", "micEnabled", enabled)
        enqueueControl(S3WatchProtocol.cmdMicEnable, payload: Data([enabled ? 1 : 0]))
    }

    func sortMicRanking(list: [String]) -> [String] {
        let glasses = list.filter { $0.localizedCaseInsensitiveContains("glasses") }
        let rest = list.filter { !$0.localizedCaseInsensitiveContains("glasses") }
        return glasses + rest
    }

    func sendJson(_: [String: Any], wakeUp _: Bool, requireAck _: Bool) {}

    func requestPhoto(_ request: PhotoRequest) {
        Bridge.log("\(S3Watch.tag): requestPhoto not supported save=\(request.save)")
    }

    func startStream(_: [String: Any]) {}
    func stopStream() {}
    func sendStreamKeepAlive(_: [String: Any]) {}
    func startVideoRecording(requestId _: String, save _: Bool, sound _: Bool) {}
    func stopVideoRecording(requestId _: String) {}
    func sendButtonPhotoSettings() {}
    func sendButtonVideoRecordingSettings() {}
    func sendButtonMaxRecordingTime() {}
    func sendCameraFovSetting() {}

    func setBrightness(_ level: Int, autoMode _: Bool) {
        let clamped = UInt8(min(max(level, 0), 100))
        enqueueControl(S3WatchProtocol.cmdBrightness, payload: Data([clamped]))
    }

    func clearDisplay() {
        Bridge.log("\(S3Watch.tag): clearDisplay")
        enqueueControl(S3WatchProtocol.cmdClear)
    }

    func sendText(_ text: String) async {
        await sendTextWall(text)
    }

    func sendTextWall(_ text: String) async {
        Bridge.log("\(S3Watch.tag): textWall \(text.prefix(48))")
        enqueueControl(S3WatchProtocol.cmdText, payload: Data(text.utf8))
    }

    func sendDoubleTextWall(_ top: String, _ bottom: String) async {
        await sendTextWall("\(top)\n\(bottom)")
    }

    func displayBitmap(
        base64ImageData: String, x: Int32?, y: Int32?, width: Int32?, height: Int32?
    ) async -> Bool {
        guard let jpeg = decodeToJpeg(base64ImageData, x: x, y: y, width: width, height: height) else {
            return false
        }
        Bridge.log("\(S3Watch.tag): displayBitmap jpeg=\(jpeg.count) pos=\(x ?? -1),\(y ?? -1)")
        sendJpeg(jpeg)
        return true
    }

    func sendPositionedText(
        _ text: String, x: Int32, y: Int32, width: Int32, height: Int32,
        borderWidth: Int32, borderRadius: Int32
    ) async {
        await applySceneFrame(
            SceneFrame(
                appId: "legacy",
                epoch: 0,
                replay: true,
                elements: [
                    SceneElement(
                        id: "pos",
                        type: "text",
                        x: x,
                        y: y,
                        w: width,
                        h: height,
                        text: text,
                        data: nil,
                        border: borderWidth,
                        radius: borderRadius,
                        change: "created",
                        contentHash: ""
                    ),
                ],
                removed: []
            )
        )
    }

    func applySceneFrame(_ frame: SceneFrame) async {
        guard let jpeg = rasterizeScene(frame) else { return }
        Bridge.log("\(S3Watch.tag): scene jpeg=\(jpeg.count) elements=\(frame.elements.count)")
        sendJpeg(jpeg)
    }

    func showDashboard() {}
    func setDashboardPosition(_: Int, _: Int) {}

    func setDashboardMenu(_ items: [[String: Any]]) {
        let parsed: [(String, String, Bool)] = items.compactMap { dict in
            guard let packageName = dict["packageName"] as? String,
                  let name = dict["name"] as? String
            else { return nil }
            let running = dict["running"] as? Bool ?? false
            return (packageName, name, running)
        }
        dashboardMenuPackages = parsed.map(\.0)
        dashboardMenuItems = parsed.map { S3WatchProtocol.MenuEntry(running: $0.2, name: $0.1) }
        Bridge.log("\(S3Watch.tag): setDashboardMenu \(dashboardMenuItems.count) items")
        enqueueControl(S3WatchProtocol.cmdMenu, payload: S3WatchProtocol.encodeMenu(dashboardMenuItems))
    }

    func setHeadUpAngle(_: Int) {}
    func getBatteryStatus() {}
    func setSilentMode(_: Bool) {}

    func exit() {
        disconnect()
    }

    func sendShutdown() {}
    func sendReboot() {}

    func sendRgbLedControl(
        requestId: String, packageName _: String?, action _: String, color _: String?,
        onDurationMs _: Int, offDurationMs _: Int, count _: Int
    ) {
        Bridge.sendRgbLedControlResponse(requestId: requestId, success: false, error: "device_not_supported")
    }

    func disconnect() {
        stopScan()
        cleanupGatt()
        updateConnectionState(ConnTypes.DISCONNECTED)
        DeviceStore.shared.apply("glasses", "connected", false)
        DeviceStore.shared.apply("glasses", "fullyBooted", false)
        DeviceStore.shared.apply("glasses", "micEnabled", false)
    }

    func forget() {
        targetIdentifier = nil
        disconnect()
    }

    func findCompatibleDevices() {
        discoveredNames.removeAll()
        targetIdentifier = nil
        startScan(forConnection: false)
    }

    func stopScan() {
        scanTimeoutWork?.cancel()
        scanTimeoutWork = nil
        centralManager?.stopScan()
    }

    func connectById(_ id: String) {
        targetIdentifier = id.trimmingCharacters(in: .whitespacesAndNewlines)
        discoveredNames.removeAll()
        if connectByUUID(targetIdentifier ?? "") { return }
        startScan(forConnection: true)
    }

    func getConnectedBluetoothName() -> String? {
        peripheral?.name
    }

    func connectController() {}
    func disconnectController() {}

    func cleanup() {
        disconnect()
    }

    func ping() {}
    func dbg1() {}
    func dbg2() {}

    func requestWifiScan(scanId _: String?) {}
    func sendWifiCredentials(_: String, _: String) {}
    func forgetWifiNetwork(_: String) {}
    func sendHotspotState(_: Bool) {}
    func sendOtaStart(otaVersionUrl _: String?) {}
    func sendOtaQueryStatus() {}

    func sendSetSystemTime(_ timestampMs: Int64) {
        let seconds = UInt32(clamping: timestampMs / 1000)
        let date = Date(timeIntervalSince1970: TimeInterval(timestampMs) / 1000)
        let offsetMin = Int16(clamping: TimeZone.current.secondsFromGMT(for: date) / 60)
        var payload = Data(count: 6)
        payload[0] = UInt8(seconds & 0xFF)
        payload[1] = UInt8((seconds >> 8) & 0xFF)
        payload[2] = UInt8((seconds >> 16) & 0xFF)
        payload[3] = UInt8((seconds >> 24) & 0xFF)
        let offsetBits = UInt16(bitPattern: offsetMin)
        payload[4] = UInt8(offsetBits & 0xFF)
        payload[5] = UInt8((offsetBits >> 8) & 0xFF)
        enqueueControl(S3WatchProtocol.cmdTimeSync, payload: payload)
    }

    func sendUserEmailToGlasses(_: String) {}
    func sendIncidentId(_: String, apiBaseUrl _: String?) {}
    func queryGalleryStatus() {}
    func sendGalleryMode() {}
    func requestVersionInfo() {}

    private func startScan(forConnection: Bool) {
        guard let centralManager else { return }
        if centralManager.state != .poweredOn {
            Bridge.log("\(S3Watch.tag): Bluetooth not powered on")
            return
        }
        stopScan()
        connecting = forConnection
        updateConnectionState(ConnTypes.SCANNING)
        centralManager.scanForPeripherals(
            withServices: nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
        let work = DispatchWorkItem { [weak self] in
            Task { @MainActor in
                self?.stopScan()
            }
        }
        scanTimeoutWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + S3Watch.scanDuration, execute: work)
    }

    private func connectByUUID(_ id: String) -> Bool {
        guard let uuid = UUID(uuidString: id), let centralManager else { return false }
        guard let target = centralManager.retrievePeripherals(withIdentifiers: [uuid]).first else {
            return false
        }
        connect(target)
        return true
    }

    private func connect(_ peripheral: CBPeripheral) {
        stopScan()
        cleanupGatt(keepState: true)
        connecting = true
        self.peripheral = peripheral
        peripheral.delegate = self
        updateConnectionState(ConnTypes.CONNECTING)
        let address = peripheral.identifier.uuidString
        DeviceStore.shared.apply("bluetooth", "pending_device_address", address)
        DeviceStore.shared.apply("bluetooth", "device_address", address)
        if let name = peripheral.name, !name.isEmpty {
            DeviceStore.shared.apply("bluetooth", "pending_device_name", name)
        }
        centralManager?.connect(peripheral, options: nil)
        Bridge.log("\(S3Watch.tag): Connecting to \(peripheral.name ?? address)")
    }

    private func markReady() {
        if let name = getConnectedBluetoothName(), !name.isEmpty {
            DeviceStore.shared.apply("glasses", "bluetoothName", name)
        }
        DeviceStore.shared.apply("glasses", "connected", true)
        DeviceStore.shared.apply("glasses", "deviceModel", type)
        DeviceStore.shared.apply("glasses", "fullyBooted", true)
        updateConnectionState(ConnTypes.CONNECTED)
        sendSetSystemTime(Int64(Date().timeIntervalSince1970 * 1000))
        sendStoredMenu()
        pumpWrites()
    }

    private func sendStoredMenu() {
        guard let items = DeviceStore.shared.get("bluetooth", "menu_apps") as? [[String: Any]],
              !items.isEmpty
        else { return }
        setDashboardMenu(items)
    }

    private func handleEvent(_ packet: Data) {
        guard let decoded = S3WatchProtocol.decode(packet) else { return }
        switch decoded.opcode {
        case S3WatchProtocol.evtBattery:
            if let percent = decoded.payload.first {
                DeviceStore.shared.apply("glasses", "batteryLevel", min(max(Int(percent), 0), 100))
            }
        case S3WatchProtocol.evtAck:
            if decoded.payload.first == S3WatchProtocol.cmdImgEnd {
                finishJpegTransfer()
            }
        case S3WatchProtocol.evtReady:
            markReady()
        case S3WatchProtocol.evtGesture:
            guard let first = decoded.payload.first, let name = S3WatchProtocol.gestureName(first) else { return }
            Bridge.log("\(S3Watch.tag): gesture \(name)")
            switch name {
            case "swipe_up":
                DeviceStore.shared.apply("glasses", "headUp", true)
            case "swipe_down":
                DeviceStore.shared.apply("glasses", "headUp", false)
            default:
                break
            }
            Bridge.sendTouchEvent(
                deviceModel: type,
                gestureName: name,
                timestamp: Int64(Date().timeIntervalSince1970 * 1000)
            )
        case S3WatchProtocol.evtMenuSelect:
            guard let first = decoded.payload.first else { return }
            let now = Int64(Date().timeIntervalSince1970 * 1000)
            if now - lastMenuSelectMs < 500 { return }
            lastMenuSelectMs = now
            let index = Int(first)
            guard dashboardMenuPackages.indices.contains(index) else { return }
            let packageName = dashboardMenuPackages[index]
            Bridge.log("\(S3Watch.tag): menu select \(packageName)")
            Bridge.sendMiniappSelected(packageName: packageName)
        default:
            break
        }
    }

    private func enqueueControl(_ opcode: UInt8, payload: Data = Data()) {
        let op = WriteOp(uuid: S3WatchProtocol.ctrlUUID, payload: encodeControl(opcode, payload: payload), noResponse: false)
        // Mic must not wait for a JPEG blit ACK. Captions turns the mic on while
        // the first HUD frame is still in flight.
        if jpegTransferActive, opcode != S3WatchProtocol.cmdMicEnable {
            pendingControls.append(op)
        } else {
            writeQueue.append(op)
            pumpWrites()
        }
    }

    private func sendJpeg(_ jpeg: Data) {
        pendingJpeg = jpeg
        if !jpegTransferActive {
            enqueuePendingJpeg()
        }
    }

    private func finishJpegTransfer() {
        jpegAckTimeoutWork?.cancel()
        jpegAckTimeoutWork = nil
        guard jpegTransferActive else { return }
        jpegTransferActive = false
        writeQueue.append(contentsOf: pendingControls)
        pendingControls.removeAll()
        enqueuePendingJpeg()
        pumpWrites()
    }

    private func enqueuePendingJpeg() {
        guard let jpeg = pendingJpeg else { return }
        pendingJpeg = nil
        jpegTransferActive = true
        var begin = Data(count: 8)
        let size = UInt32(jpeg.count)
        begin[0] = UInt8(size & 0xFF)
        begin[1] = UInt8((size >> 8) & 0xFF)
        begin[2] = UInt8((size >> 16) & 0xFF)
        begin[3] = UInt8((size >> 24) & 0xFF)
        begin[4] = UInt8(S3WatchProtocol.displayWidth & 0xFF)
        begin[5] = UInt8((S3WatchProtocol.displayWidth >> 8) & 0xFF)
        begin[6] = UInt8(S3WatchProtocol.displayHeight & 0xFF)
        begin[7] = UInt8((S3WatchProtocol.displayHeight >> 8) & 0xFF)
        writeQueue.append(
            WriteOp(
                uuid: S3WatchProtocol.ctrlUUID,
                payload: encodeControl(S3WatchProtocol.cmdImgBegin, payload: begin),
                noResponse: false
            )
        )
        let chunkSize = min(max(negotiatedChunk, 20), 180)
        var offset = 0
        while offset < jpeg.count {
            let end = min(offset + chunkSize, jpeg.count)
            writeQueue.append(
                WriteOp(
                    uuid: S3WatchProtocol.imgUUID,
                    payload: jpeg.subdata(in: offset ..< end),
                    noResponse: true
                )
            )
            offset = end
        }
        writeQueue.append(
            WriteOp(
                uuid: S3WatchProtocol.ctrlUUID,
                payload: encodeControl(S3WatchProtocol.cmdImgEnd),
                noResponse: false,
                endsJpeg: true
            )
        )
        pumpWrites()
    }

    private func encodeControl(_ opcode: UInt8, payload: Data = Data()) -> Data {
        S3WatchProtocol.encode(opcode: opcode, seq: nextSeq(), payload: payload)
    }

    private func pumpWrites() {
        guard let peripheral, !writeInFlight, !writeQueue.isEmpty else { return }
        let op = writeQueue.removeFirst()
        let characteristic: CBCharacteristic?
        switch op.uuid {
        case S3WatchProtocol.ctrlUUID:
            characteristic = ctrlChar
        case S3WatchProtocol.imgUUID:
            characteristic = imgChar
        default:
            characteristic = nil
        }
        guard let characteristic else { return }
        currentOp = op
        if op.noResponse {
            if !peripheral.canSendWriteWithoutResponse {
                writeQueue.insert(op, at: 0)
                return
            }
            peripheral.writeValue(op.payload, for: characteristic, type: .withoutResponse)
            currentOp = nil
            pumpWrites()
            return
        }
        writeInFlight = true
        peripheral.writeValue(op.payload, for: characteristic, type: .withResponse)
    }

    private func nextSeq() -> UInt8 {
        let current = seq
        seq = seq == 255 ? 1 : seq &+ 1
        return current
    }

    private func updateConnectionState(_ state: String) {
        DeviceStore.shared.apply("glasses", "connectionState", state)
    }

    private func cleanupGatt(keepState: Bool = false) {
        writeQueue.removeAll()
        writeInFlight = false
        currentOp = nil
        pendingJpeg = nil
        jpegTransferActive = false
        pendingControls.removeAll()
        jpegAckTimeoutWork?.cancel()
        jpegAckTimeoutWork = nil
        pendingNotifyCount = 0
        ctrlChar = nil
        evtChar = nil
        imgChar = nil
        micChar = nil
        pcmPackets = 0
        if let peripheral {
            centralManager?.cancelPeripheralConnection(peripheral)
        }
        self.peripheral = nil
        if !keepState {
            connecting = false
        }
    }

    private struct WriteOp {
        let uuid: CBUUID
        let payload: Data
        let noResponse: Bool
        var endsJpeg = false
    }
}

extension S3Watch: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        DispatchQueue.main.async { [weak self] in
            Bridge.log("\(S3Watch.tag): Bluetooth state: \(central.state.rawValue)")
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let advertised = peripheral.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            guard S3WatchProtocol.matchesAdvertisedName(advertised), let name = advertised else { return }
            let address = peripheral.identifier.uuidString
            if self.targetIdentifier == nil {
                if self.discoveredNames.insert(name).inserted {
                    Bridge.sendDiscoveredDevice(
                        DeviceTypes.S3_WATCH,
                        name,
                        deviceAddress: address,
                        rssi: RSSI.intValue
                    )
                }
                return
            }
            if self.peripheral != nil { return }
            let target = self.targetIdentifier ?? ""
            let matches =
                target.isEmpty
                || name.caseInsensitiveCompare(target) == .orderedSame
                || address.caseInsensitiveCompare(target) == .orderedSame
            guard matches else { return }
            self.connect(peripheral)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        DispatchQueue.main.async { [weak self] in
            Bridge.log("\(S3Watch.tag): connected to \(peripheral.name ?? "?")")
            let mtu = peripheral.maximumWriteValueLength(for: .withoutResponse)
            self?.negotiatedChunk = mtu
            Bridge.log("\(S3Watch.tag): write chunk \(mtu)")
            peripheral.discoverServices([S3WatchProtocol.serviceUUID])
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?
    ) {
        DispatchQueue.main.async { [weak self] in
            Bridge.log("\(S3Watch.tag): failed to connect: \(error?.localizedDescription ?? "unknown")")
            self?.cleanupGatt()
            self?.updateConnectionState(ConnTypes.DISCONNECTED)
        }
    }

    nonisolated func centralManager(
        _ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?
    ) {
        DispatchQueue.main.async { [weak self] in
            Bridge.log("\(S3Watch.tag): disconnected \(error?.localizedDescription ?? "")")
            self?.cleanupGatt()
            self?.updateConnectionState(ConnTypes.DISCONNECTED)
            DeviceStore.shared.apply("glasses", "connected", false)
            DeviceStore.shared.apply("glasses", "fullyBooted", false)
            DeviceStore.shared.apply("glasses", "micEnabled", false)
        }
    }
}

extension S3Watch: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        DispatchQueue.main.async {
            if let error {
                Bridge.log("\(S3Watch.tag): service discovery error: \(error.localizedDescription)")
                return
            }
            guard let service = peripheral.services?.first(where: { $0.uuid == S3WatchProtocol.serviceUUID }) else {
                Bridge.log("\(S3Watch.tag): S3Watch GATT service missing")
                return
            }
            peripheral.discoverCharacteristics(
                [
                    S3WatchProtocol.ctrlUUID,
                    S3WatchProtocol.evtUUID,
                    S3WatchProtocol.imgUUID,
                    S3WatchProtocol.micUUID,
                ],
                for: service
            )
        }
    }

    nonisolated func peripheral(
        _ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?
    ) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            if let error {
                Bridge.log("\(S3Watch.tag): characteristic discovery error: \(error.localizedDescription)")
                return
            }
            var notify: [CBCharacteristic] = []
            for char in service.characteristics ?? [] {
                switch char.uuid {
                case S3WatchProtocol.ctrlUUID:
                    self.ctrlChar = char
                case S3WatchProtocol.evtUUID:
                    self.evtChar = char
                    notify.append(char)
                case S3WatchProtocol.imgUUID:
                    self.imgChar = char
                case S3WatchProtocol.micUUID:
                    self.micChar = char
                    notify.append(char)
                default:
                    break
                }
            }
            self.pendingNotifyCount = notify.count
            if notify.isEmpty {
                self.markReady()
                return
            }
            for char in notify {
                peripheral.setNotifyValue(true, for: char)
            }
        }
    }

    nonisolated func peripheral(
        _ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?
    ) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            if let error {
                Bridge.log("\(S3Watch.tag): notify error: \(error.localizedDescription)")
            }
            if self.pendingNotifyCount > 0 {
                self.pendingNotifyCount -= 1
            }
            if self.pendingNotifyCount == 0 {
                self.markReady()
            }
        }
    }

    nonisolated func peripheral(
        _ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error _: Error?
    ) {
        let data = characteristic.value
        let uuid = characteristic.uuid
        DispatchQueue.main.async { [weak self] in
            guard let self, let data, !data.isEmpty else { return }
            switch uuid {
            case S3WatchProtocol.evtUUID:
                self.handleEvent(data)
            case S3WatchProtocol.micUUID:
                self.pcmPackets += 1
                if self.pcmPackets == 1 || self.pcmPackets % 50 == 0 {
                    Bridge.log("\(S3Watch.tag): pcm #\(self.pcmPackets) \(data.count)b")
                }
                DeviceManager.shared.reportGlassesAudioActivity()
                DeviceManager.shared.handlePcm(data)
            default:
                break
            }
        }
    }

    nonisolated func peripheral(
        _: CBPeripheral, didWriteValueFor _: CBCharacteristic, error: Error?
    ) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.writeInFlight = false
            let finished = self.currentOp
            self.currentOp = nil
            if let error {
                Bridge.log("\(S3Watch.tag): characteristic write failed: \(error.localizedDescription)")
            }
            if finished?.endsJpeg == true {
                self.jpegAckTimeoutWork?.cancel()
                let work = DispatchWorkItem { [weak self] in
                    Task { @MainActor in
                        Bridge.log("\(S3Watch.tag): JPEG blit ack timed out")
                        self?.finishJpegTransfer()
                    }
                }
                self.jpegAckTimeoutWork = work
                DispatchQueue.main.asyncAfter(deadline: .now() + S3Watch.jpegAckTimeout, execute: work)
            }
            self.pumpWrites()
        }
    }

    nonisolated func peripheralIsReady(toSendWriteWithoutResponse _: CBPeripheral) {
        DispatchQueue.main.async { [weak self] in
            self?.pumpWrites()
        }
    }
}

#if canImport(UIKit)
extension S3Watch {
    private func decodeToJpeg(
        _ base64ImageData: String, x: Int32?, y: Int32?, width: Int32?, height: Int32?
    ) -> Data? {
        guard let raw = Data(base64Encoded: base64ImageData), let image = UIImage(data: raw) else {
            Bridge.log("\(S3Watch.tag): decodeToJpeg failed")
            return nil
        }
        let green = toHudGreen(image)
        let positioned = x != nil || y != nil || width != nil || height != nil
        if !positioned {
            return compressJpeg(green)
        }
        let gx = Int(x ?? 0)
        let gy = Int(y ?? 0)
        let gw = Int(width ?? Int32(green.size.width))
        let gh = Int(height ?? Int32(green.size.height))
        let g2 = usesG2Layout(x: gx, y: gy, w: gw, h: gh)
        let box = sceneRectOnPanel(x: gx, y: gy, w: gw, h: gh, g2: g2)
        let renderer = UIGraphicsImageRenderer(
            size: CGSize(width: S3WatchProtocol.displayWidth, height: S3WatchProtocol.displayHeight)
        )
        let panel = renderer.image { ctx in
            UIColor.black.setFill()
            ctx.fill(CGRect(x: 0, y: 0, width: S3WatchProtocol.displayWidth, height: S3WatchProtocol.displayHeight))
            green.draw(in: box)
        }
        return compressJpeg(panel)
    }

    private func rasterizeScene(_ frame: SceneFrame) -> Data? {
        let g2 = frame.elements.contains { usesG2Layout(x: Int($0.x), y: Int($0.y), w: Int($0.w), h: Int($0.h)) }
        let srcW = g2 ? S3WatchProtocol.g2Width : S3WatchProtocol.sceneWidth
        let srcH = g2 ? S3WatchProtocol.g2Height : S3WatchProtocol.sceneHeight
        let sceneRenderer = UIGraphicsImageRenderer(size: CGSize(width: srcW, height: srcH))
        let scene = sceneRenderer.image { ctx in
            UIColor.black.setFill()
            ctx.fill(CGRect(x: 0, y: 0, width: srcW, height: srcH))
            for el in frame.elements where el.type == "image" {
                guard let data = el.data, let raw = Data(base64Encoded: data), let decoded = UIImage(data: raw) else {
                    continue
                }
                decoded.draw(in: CGRect(x: Int(el.x), y: Int(el.y), width: Int(el.w), height: Int(el.h)))
            }
        }
        let green = toHudGreen(scene)
        let scale: CGFloat =
            g2
            ? min(
                CGFloat(S3WatchProtocol.sceneWidth) / CGFloat(S3WatchProtocol.g2Width),
                CGFloat(S3WatchProtocol.sceneHeight) / CGFloat(S3WatchProtocol.g2Height)
            )
            : 1
        let hud = UIColor(
            red: CGFloat(S3WatchProtocol.hudGreenR) / 255,
            green: CGFloat(S3WatchProtocol.hudGreenG) / 255,
            blue: CGFloat(S3WatchProtocol.hudGreenB) / 255,
            alpha: 1
        )
        let panelRenderer = UIGraphicsImageRenderer(
            size: CGSize(width: S3WatchProtocol.displayWidth, height: S3WatchProtocol.displayHeight)
        )
        let panel = panelRenderer.image { ctx in
            UIColor.black.setFill()
            ctx.fill(CGRect(x: 0, y: 0, width: S3WatchProtocol.displayWidth, height: S3WatchProtocol.displayHeight))
            green.draw(in: sceneRectOnPanel(x: 0, y: 0, w: srcW, h: srcH, g2: g2))
            let cg = ctx.cgContext
            cg.setStrokeColor(hud.cgColor)
            cg.setFillColor(hud.cgColor)
            let font = UIFont.systemFont(ofSize: S3WatchProtocol.sceneTextSize * scale)
            let lineH = S3WatchProtocol.sceneLineHeight * scale
            for el in frame.elements where el.type == "text" || el.type == "rect" {
                let box = sceneRectOnPanel(x: Int(el.x), y: Int(el.y), w: Int(el.w), h: Int(el.h), g2: g2)
                if el.type == "rect" || el.border > 0 {
                    cg.saveGState()
                    cg.addRect(box)
                    cg.clip()
                    let path = UIBezierPath(roundedRect: box, cornerRadius: CGFloat(el.radius))
                    path.lineWidth = CGFloat(max(1, el.border))
                    hud.setStroke()
                    path.stroke()
                    cg.restoreGState()
                }
                if el.type == "text" {
                    drawWrappedText(el.text ?? "", in: box, font: font, color: hud, lineHeight: lineH)
                }
            }
        }
        return compressJpeg(panel)
    }

    private func usesG2Layout(x: Int, y: Int, w: Int, h: Int) -> Bool {
        x + w > S3WatchProtocol.sceneWidth || y + h > S3WatchProtocol.sceneHeight
    }

    private func sceneRectOnPanel(x: Int, y: Int, w: Int, h: Int, g2: Bool) -> CGRect {
        if !g2 {
            return CGRect(
                x: S3WatchProtocol.sceneOriginX + x,
                y: S3WatchProtocol.sceneOriginY + y,
                width: w,
                height: h
            )
        }
        let scale = min(
            CGFloat(S3WatchProtocol.sceneWidth) / CGFloat(S3WatchProtocol.g2Width),
            CGFloat(S3WatchProtocol.sceneHeight) / CGFloat(S3WatchProtocol.g2Height)
        )
        let sceneW = CGFloat(S3WatchProtocol.g2Width) * scale
        let sceneH = CGFloat(S3WatchProtocol.g2Height) * scale
        let ox = CGFloat(S3WatchProtocol.sceneOriginX) + (CGFloat(S3WatchProtocol.sceneWidth) - sceneW) / 2
        let oy = CGFloat(S3WatchProtocol.sceneOriginY) + (CGFloat(S3WatchProtocol.sceneHeight) - sceneH) / 2
        return CGRect(
            x: ox + CGFloat(x) * scale,
            y: oy + CGFloat(y) * scale,
            width: CGFloat(w) * scale,
            height: CGFloat(h) * scale
        )
    }

    private func drawWrappedText(_ text: String, in box: CGRect, font: UIFont, color: UIColor, lineHeight: CGFloat) {
        guard !text.isEmpty, box.width > 0, box.height > 0 else { return }
        let paragraph = NSMutableParagraphStyle()
        paragraph.lineSpacing = max(0, lineHeight - font.lineHeight)
        let attrs: [NSAttributedString.Key: Any] = [
            .font: font,
            .foregroundColor: color,
            .paragraphStyle: paragraph,
        ]
        let bounded = CGRect(x: box.minX, y: box.minY, width: box.width, height: box.height)
        (text as NSString).draw(with: bounded, options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine], attributes: attrs, context: nil)
    }

    private func toHudGreen(_ src: UIImage) -> UIImage {
        guard let cg = src.cgImage else { return src }
        let w = cg.width
        let h = cg.height
        let bytesPerPixel = 4
        let bytesPerRow = w * bytesPerPixel
        var px = [UInt8](repeating: 0, count: h * bytesPerRow)
        guard let ctx = CGContext(
            data: &px,
            width: w,
            height: h,
            bitsPerComponent: 8,
            bytesPerRow: bytesPerRow,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
        ) else { return src }
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))
        let levels = max(S3WatchProtocol.hudIntensityLevels, 2)
        let last = CGFloat(levels - 1)
        let hr = CGFloat(S3WatchProtocol.hudGreenR)
        let hg = CGFloat(S3WatchProtocol.hudGreenG)
        let hb = CGFloat(S3WatchProtocol.hudGreenB)
        for i in stride(from: 0, to: px.count, by: 4) {
            let lum = CGFloat(px[i]) * 0.299 + CGFloat(px[i + 1]) * 0.587 + CGFloat(px[i + 2]) * 0.114
            let q = min(max(Int((min(max(lum, 0), 255) / 255) * last + 0.5), 0), levels - 1)
            if q == 0 {
                px[i] = 0
                px[i + 1] = 0
                px[i + 2] = 0
                px[i + 3] = 255
            } else {
                let t = CGFloat(q) / last
                px[i] = UInt8(hr * t)
                px[i + 1] = UInt8(hg * t)
                px[i + 2] = UInt8(hb * t)
                px[i + 3] = 255
            }
        }
        guard let out = ctx.makeImage() else { return src }
        return UIImage(cgImage: out, scale: src.scale, orientation: .up)
    }

    private func compressJpeg(_ image: UIImage) -> Data? {
        let target = CGSize(width: S3WatchProtocol.displayWidth, height: S3WatchProtocol.displayHeight)
        let scaled: UIImage
        if Int(image.size.width.rounded()) == S3WatchProtocol.displayWidth,
           Int(image.size.height.rounded()) == S3WatchProtocol.displayHeight
        {
            scaled = image
        } else {
            let renderer = UIGraphicsImageRenderer(size: target)
            scaled = renderer.image { _ in
                image.draw(in: CGRect(origin: .zero, size: target))
            }
        }
        return scaled.jpegData(compressionQuality: S3WatchProtocol.jpegQuality)
    }
}
#else
extension S3Watch {
    private func decodeToJpeg(
        _: String, x _: Int32?, y _: Int32?, width _: Int32?, height _: Int32?
    ) -> Data? {
        Bridge.log("\(S3Watch.tag): UNTESTED ALPHA JPEG path unavailable without UIKit")
        return nil
    }

    private func rasterizeScene(_: SceneFrame) -> Data? {
        Bridge.log("\(S3Watch.tag): UNTESTED ALPHA scene rasterize unavailable without UIKit")
        return nil
    }
}
#endif
