package com.mentra.bluetoothsdk.sgcs

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Base64
import com.mentra.bluetoothsdk.Bridge
import com.mentra.bluetoothsdk.DeviceManager
import com.mentra.bluetoothsdk.DeviceStore
import com.mentra.bluetoothsdk.PhotoRequest
import com.mentra.bluetoothsdk.sgcs.s3watch.S3WatchProtocol
import com.mentra.bluetoothsdk.utils.ConnTypes
import com.mentra.bluetoothsdk.utils.DeviceTypes
import java.io.ByteArrayOutputStream
import java.nio.charset.StandardCharsets
import java.util.ArrayDeque
import java.util.UUID

/**
 * Unofficial MentraOS SGC for the Waveshare ESP32-S3-Touch-AMOLED-2.06.
 * Not a Waveshare product and not affiliated with or endorsed by Waveshare.
 */
class S3Watch : SGCManager() {
    companion object {
        private const val TAG = "S3Watch"
        private const val SCAN_DURATION_MS = 15_000L

        @JvmStatic
        fun matchesAdvertisedName(name: String?): Boolean = S3WatchProtocol.matchesAdvertisedName(name)
    }

    private val context = Bridge.getContext()
    private val handler = Handler(Looper.getMainLooper())
    private val bluetoothAdapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()

    private var scanner: BluetoothLeScanner? = null
    private var scanCallback: ScanCallback? = null
    private var scanTimeout: Runnable? = null
    private var scanning = false
    private var connecting = false
    private var targetIdentifier: String? = null
    private val discoveredNames = HashSet<String>()

    private var gatt: BluetoothGatt? = null
    private var ctrlChar: BluetoothGattCharacteristic? = null
    private var evtChar: BluetoothGattCharacteristic? = null
    private var imgChar: BluetoothGattCharacteristic? = null
    private var micChar: BluetoothGattCharacteristic? = null
    private val notifyQueue: ArrayDeque<BluetoothGattCharacteristic> = ArrayDeque()
    private var notifyWriteInFlight = false
    private val writeQueue: ArrayDeque<WriteOp> = ArrayDeque()
    private var writeInFlight = false
    private var seq = 1
    private var negotiatedMtu = 23

    init {
        type = DeviceTypes.S3_WATCH
        hasMic = true
        DeviceStore.apply("glasses", "micEnabled", false)
    }

    override fun setMicEnabled(enabled: Boolean) {
        DeviceStore.apply("glasses", "micEnabled", enabled)
        enqueueControl(S3WatchProtocol.CMD_MIC_ENABLE, byteArrayOf(if (enabled) 1 else 0))
    }

    override fun sortMicRanking(list: MutableList<String>): MutableList<String> = list

    override fun requestPhoto(request: PhotoRequest) {
        Bridge.log("$TAG: requestPhoto not supported")
    }

    override fun startStream(message: MutableMap<String, Any>) {}

    override fun stopStream() {}

    override fun sendStreamKeepAlive(message: MutableMap<String, Any>) {}

    override fun startVideoRecording(requestId: String, save: Boolean, sound: Boolean) {}

    override fun stopVideoRecording(requestId: String) {}

    override fun sendButtonPhotoSettings() {}

    override fun sendButtonVideoRecordingSettings() {}

    override fun sendButtonMaxRecordingTime() {}

    override fun sendCameraFovSetting() {}

    override fun setBrightness(level: Int, autoMode: Boolean) {
        val clamped = level.coerceIn(0, 100)
        enqueueControl(S3WatchProtocol.CMD_BRIGHTNESS, byteArrayOf(clamped.toByte()))
    }

    override fun clearDisplay() {
        enqueueControl(S3WatchProtocol.CMD_CLEAR)
    }

    override fun sendText(text: String) {
        sendTextWall(text)
    }

    override fun sendTextWall(text: String) {
        enqueueControl(S3WatchProtocol.CMD_TEXT, text.toByteArray(StandardCharsets.UTF_8))
    }

    override fun sendDoubleTextWall(top: String, bottom: String) {
        sendTextWall("$top\n$bottom")
    }

    override fun displayBitmap(
        base64ImageData: String,
        x: Int?,
        y: Int?,
        width: Int?,
        height: Int?,
    ): Boolean {
        val jpeg = decodeToJpeg(base64ImageData) ?: return false
        sendJpeg(jpeg)
        return true
    }

    override fun applySceneFrame(frame: SceneFrame) {
        val jpeg = rasterizeScene(frame) ?: return
        sendJpeg(jpeg)
    }

    override fun showDashboard() {}

    override fun setDashboardPosition(height: Int, depth: Int) {}

    override fun setHeadUpAngle(angle: Int) {}

    override fun getBatteryStatus() {}

    override fun setSilentMode(enabled: Boolean) {}

    override fun exit() {
        disconnect()
    }

    override fun sendShutdown() {}

    override fun sendReboot() {}

    override fun sendRgbLedControl(
        requestId: String,
        packageName: String?,
        action: String,
        color: String?,
        onDurationMs: Int,
        offDurationMs: Int,
        count: Int,
    ) {
        Bridge.sendRgbLedControlResponse(requestId, false, "device_not_supported")
    }

    override fun disconnect() {
        stopScan()
        cleanupGatt()
        updateConnectionState(ConnTypes.DISCONNECTED)
        DeviceStore.apply("glasses", "connected", false)
        DeviceStore.apply("glasses", "fullyBooted", false)
        DeviceStore.apply("glasses", "micEnabled", false)
    }

    override fun forget() {
        targetIdentifier = null
        disconnect()
    }

    override fun findCompatibleDevices() {
        discoveredNames.clear()
        startScan(forConnection = false)
    }

    override fun stopScan() {
        scanTimeout?.let { handler.removeCallbacks(it) }
        scanTimeout = null
        if (!scanning) return
        try {
            val cb = scanCallback
            if (scanner != null && cb != null) scanner?.stopScan(cb)
        } catch (_: Throwable) {
        }
        scanning = false
        scanCallback = null
        scanner = null
    }

    override fun connectById(id: String) {
        targetIdentifier = id.trim()
        discoveredNames.clear()
        val target = targetIdentifier.orEmpty()
        if (looksLikeMac(target) && bluetoothAdapter != null) {
            try {
                connectGatt(bluetoothAdapter.getRemoteDevice(target))
                return
            } catch (e: IllegalArgumentException) {
                Bridge.log("$TAG: invalid reconnect address $target: ${e.message}")
            }
        }
        startScan(forConnection = true)
    }

    override fun getConnectedBluetoothName(): String {
        return gatt?.device?.name.orEmpty()
    }

    override fun cleanup() {
        disconnect()
    }

    override fun ping() {}

    override fun dbg1() {}

    override fun dbg2() {}

    override fun requestWifiScan(scanId: String?) {}

    override fun sendWifiCredentials(ssid: String, password: String) {}

    override fun forgetWifiNetwork(ssid: String) {}

    override fun sendHotspotState(enabled: Boolean) {}

    override fun sendSetSystemTime(timestampMs: Long) {
        val seconds = (timestampMs / 1000L).toInt()
        val payload =
            byteArrayOf(
                (seconds and 0xFF).toByte(),
                ((seconds shr 8) and 0xFF).toByte(),
                ((seconds shr 16) and 0xFF).toByte(),
                ((seconds shr 24) and 0xFF).toByte(),
            )
        enqueueControl(S3WatchProtocol.CMD_TIME_SYNC, payload)
    }

    override fun sendUserEmailToGlasses(email: String) {}

    override fun sendIncidentId(incidentId: String, apiBaseUrl: String?) {}

    override fun queryGalleryStatus() {}

    override fun sendGalleryMode() {}

    override fun requestVersionInfo() {}

    private fun startScan(forConnection: Boolean) {
        val adapter = bluetoothAdapter
        if (adapter == null || !adapter.isEnabled) {
            Bridge.log("$TAG: Bluetooth unavailable or disabled")
            return
        }
        stopScan()
        scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            Bridge.log("$TAG: BLE scanner unavailable")
            return
        }
        updateConnectionState(ConnTypes.SCANNING)
        connecting = forConnection
        scanning = true
        scanCallback =
            object : ScanCallback() {
                override fun onScanResult(callbackType: Int, result: ScanResult) {
                    handleScanResult(result, forConnection)
                }

                override fun onScanFailed(errorCode: Int) {
                    Bridge.log("$TAG: scan failed code=$errorCode")
                    stopScan()
                    if (connectionState == ConnTypes.SCANNING) {
                        updateConnectionState(ConnTypes.DISCONNECTED)
                    }
                }
            }
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner?.startScan(emptyList(), settings, scanCallback)
        scanTimeout = Runnable { stopScan() }
        handler.postDelayed(scanTimeout!!, SCAN_DURATION_MS)
    }

    private fun handleScanResult(result: ScanResult, forConnection: Boolean) {
        val device = result.device ?: return
        val advertised = advertisedName(result)
        if (!matchesAdvertisedName(advertised)) return
        val name = advertised ?: return
        val address = device.address.orEmpty()

        if (!forConnection) {
            if (discoveredNames.add(name)) {
                Bridge.sendDiscoveredDevice(type, name, address, result.rssi)
            }
            return
        }

        val target = targetIdentifier.orEmpty()
        val matchesTarget =
            target.isBlank() ||
                target.equals(address, ignoreCase = true) ||
                target.equals(name, ignoreCase = true)
        if (!matchesTarget) return

        if (address.isNotBlank()) {
            DeviceManager.getInstance().deviceAddress = address
        }
        stopScan()
        connectGatt(device)
    }

    private fun connectGatt(device: BluetoothDevice) {
        cleanupGatt(keepState = true)
        updateConnectionState(ConnTypes.CONNECTING)
        connecting = true
        gatt =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
            } else {
                device.connectGatt(context, false, gattCallback)
            }
    }

    private val gattCallback =
        object : BluetoothGattCallback() {
            override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    Bridge.log("$TAG: connected to ${gatt.device?.name}")
                    this@S3Watch.gatt = gatt
                    gatt.requestMtu(S3WatchProtocol.REQUESTED_MTU)
                    handler.postDelayed({ gatt.discoverServices() }, 200)
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    Bridge.log("$TAG: disconnected status=$status")
                    cleanupGatt()
                    updateConnectionState(ConnTypes.DISCONNECTED)
                    DeviceStore.apply("glasses", "connected", false)
                    DeviceStore.apply("glasses", "fullyBooted", false)
                }
            }

            override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    negotiatedMtu = mtu
                    Bridge.log("$TAG: MTU $mtu")
                }
            }

            override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Bridge.log("$TAG: service discovery failed status=$status")
                    return
                }
                val service = gatt.getService(S3WatchProtocol.SERVICE_UUID)
                if (service == null) {
                    Bridge.log("$TAG: S3Watch GATT service missing")
                    return
                }
                ctrlChar = service.getCharacteristic(S3WatchProtocol.CTRL_UUID)
                evtChar = service.getCharacteristic(S3WatchProtocol.EVT_UUID)
                imgChar = service.getCharacteristic(S3WatchProtocol.IMG_UUID)
                micChar = service.getCharacteristic(S3WatchProtocol.MIC_UUID)
                notifyQueue.clear()
                evtChar?.let { notifyQueue.add(it) }
                micChar?.let { notifyQueue.add(it) }
                if (notifyQueue.isEmpty()) {
                    markReady()
                } else {
                    processNextNotify()
                }
            }

            override fun onDescriptorWrite(
                gatt: BluetoothGatt,
                descriptor: BluetoothGattDescriptor,
                status: Int,
            ) {
                notifyWriteInFlight = false
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Bridge.log("$TAG: descriptor write failed status=$status")
                }
                processNextNotify()
            }

            override fun onCharacteristicWrite(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int,
            ) {
                writeInFlight = false
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Bridge.log("$TAG: characteristic write failed status=$status")
                }
                pumpWrites()
            }

            override fun onCharacteristicChanged(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
            ) {
                handleNotify(characteristic.uuid, characteristic.value)
            }

            override fun onCharacteristicChanged(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                value: ByteArray,
            ) {
                handleNotify(characteristic.uuid, value)
            }
        }

    private fun handleNotify(uuid: UUID?, value: ByteArray?) {
        if (uuid == null || value == null || value.isEmpty()) return
        when (uuid) {
            S3WatchProtocol.EVT_UUID -> handleEvent(value)
            S3WatchProtocol.MIC_UUID -> {
                DeviceManager.getInstance().reportGlassesAudioActivity()
                DeviceManager.getInstance().handlePcm(value)
            }
        }
    }

    private fun processNextNotify() {
        val localGatt = gatt ?: return
        if (notifyWriteInFlight) return
        val next = notifyQueue.poll()
        if (next == null) {
            markReady()
            return
        }
        localGatt.setCharacteristicNotification(next, true)
        val cccd = next.getDescriptor(S3WatchProtocol.CCCD_UUID)
        if (cccd == null) {
            processNextNotify()
            return
        }
        notifyWriteInFlight = true
        cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        localGatt.writeDescriptor(cccd)
    }

    private fun markReady() {
        val name = getConnectedBluetoothName()
        if (name.isNotBlank()) {
            DeviceStore.apply("glasses", "bluetoothName", name)
        }
        DeviceStore.apply("glasses", "connected", true)
        DeviceStore.apply("glasses", "fullyBooted", true)
        DeviceStore.apply("glasses", "deviceModel", type)
        updateConnectionState(ConnTypes.CONNECTED)
        sendSetSystemTime(System.currentTimeMillis())
    }

    private fun handleEvent(packet: ByteArray) {
        val decoded = S3WatchProtocol.decode(packet) ?: return
        val (opcode, _, payload) = decoded
        when (opcode) {
            S3WatchProtocol.EVT_BATTERY -> {
                if (payload.isNotEmpty()) {
                    val percent = payload[0].toInt() and 0xFF
                    DeviceStore.apply("glasses", "batteryLevel", percent.coerceIn(0, 100))
                }
            }
            S3WatchProtocol.EVT_READY -> markReady()
            S3WatchProtocol.EVT_GESTURE -> {
                if (payload.isEmpty()) return
                val name = S3WatchProtocol.gestureName(payload[0]) ?: return
                Bridge.sendTouchEvent(type, name, System.currentTimeMillis())
            }
            else -> {}
        }
    }

    private fun enqueueControl(opcode: Byte, payload: ByteArray = ByteArray(0)) {
        val packet = S3WatchProtocol.encode(opcode, nextSeq(), payload)
        writeQueue.add(WriteOp(S3WatchProtocol.CTRL_UUID, packet, noResponse = false))
        pumpWrites()
    }

    private fun sendJpeg(jpeg: ByteArray) {
        val begin =
            byteArrayOf(
                (jpeg.size and 0xFF).toByte(),
                ((jpeg.size shr 8) and 0xFF).toByte(),
                ((jpeg.size shr 16) and 0xFF).toByte(),
                ((jpeg.size shr 24) and 0xFF).toByte(),
                (S3WatchProtocol.DISPLAY_WIDTH and 0xFF).toByte(),
                ((S3WatchProtocol.DISPLAY_WIDTH shr 8) and 0xFF).toByte(),
                (S3WatchProtocol.DISPLAY_HEIGHT and 0xFF).toByte(),
                ((S3WatchProtocol.DISPLAY_HEIGHT shr 8) and 0xFF).toByte(),
            )
        enqueueControl(S3WatchProtocol.CMD_IMG_BEGIN, begin)
        val chunkSize = (negotiatedMtu - 3).coerceIn(20, 180)
        var offset = 0
        while (offset < jpeg.size) {
            val end = (offset + chunkSize).coerceAtMost(jpeg.size)
            writeQueue.add(WriteOp(S3WatchProtocol.IMG_UUID, jpeg.copyOfRange(offset, end), noResponse = true))
            offset = end
        }
        enqueueControl(S3WatchProtocol.CMD_IMG_END)
    }

    private fun pumpWrites() {
        val localGatt = gatt ?: return
        if (writeInFlight) return
        val op = writeQueue.poll() ?: return
        val characteristic =
            when (op.uuid) {
                S3WatchProtocol.CTRL_UUID -> ctrlChar
                S3WatchProtocol.IMG_UUID -> imgChar
                else -> null
            } ?: return
        characteristic.value = op.payload
        characteristic.writeType =
            if (op.noResponse) {
                BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            } else {
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            }
        writeInFlight = true
        if (!localGatt.writeCharacteristic(characteristic)) {
            writeInFlight = false
            Bridge.log("$TAG: writeCharacteristic returned false")
        }
    }

    private fun nextSeq(): Int {
        val current = seq
        seq = if (seq >= 255) 1 else seq + 1
        return current
    }

    private fun decodeToJpeg(base64ImageData: String): ByteArray? {
        return try {
            val raw = Base64.decode(base64ImageData, Base64.DEFAULT)
            if (raw.size >= 2 && raw[0] == 0xFF.toByte() && raw[1] == 0xD8.toByte()) {
                return raw
            }
            val bitmap = BitmapFactory.decodeByteArray(raw, 0, raw.size) ?: return null
            compressJpeg(bitmap)
        } catch (e: Exception) {
            Bridge.log("$TAG: decodeToJpeg failed: ${e.message}")
            null
        }
    }

    private fun rasterizeScene(frame: SceneFrame): ByteArray? {
        val bitmap =
            Bitmap.createBitmap(
                S3WatchProtocol.DISPLAY_WIDTH,
                S3WatchProtocol.DISPLAY_HEIGHT,
                Bitmap.Config.ARGB_8888,
            )
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.BLACK)
        val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
        val stroke =
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                color = Color.WHITE
                style = Paint.Style.STROKE
            }
        val textPaint =
            Paint(Paint.ANTI_ALIAS_FLAG).apply {
                color = Color.WHITE
                textSize = 22f
            }
        for (el in frame.elements) {
            when (el.type) {
                "rect" -> {
                    stroke.strokeWidth = maxOf(1, el.border).toFloat()
                    canvas.drawRoundRect(
                        RectF(el.x.toFloat(), el.y.toFloat(), (el.x + el.w).toFloat(), (el.y + el.h).toFloat()),
                        el.radius.toFloat(),
                        el.radius.toFloat(),
                        stroke,
                    )
                }
                "text" -> {
                    val text = el.text.orEmpty()
                    val fm = textPaint.fontMetrics
                    canvas.drawText(text, el.x.toFloat(), el.y - fm.ascent, textPaint)
                }
                "image" -> {
                    val data = el.data ?: continue
                    val bytes =
                        try {
                            Base64.decode(data, Base64.DEFAULT)
                        } catch (_: Exception) {
                            continue
                        }
                    val decoded = BitmapFactory.decodeByteArray(bytes, 0, bytes.size) ?: continue
                    canvas.drawBitmap(decoded, null, Rect(el.x, el.y, el.x + el.w, el.y + el.h), fill)
                }
            }
        }
        return compressJpeg(bitmap)
    }

    private fun compressJpeg(bitmap: Bitmap): ByteArray {
        val scaled =
            if (bitmap.width == S3WatchProtocol.DISPLAY_WIDTH && bitmap.height == S3WatchProtocol.DISPLAY_HEIGHT) {
                bitmap
            } else {
                Bitmap.createScaledBitmap(
                    bitmap,
                    S3WatchProtocol.DISPLAY_WIDTH,
                    S3WatchProtocol.DISPLAY_HEIGHT,
                    true,
                )
            }
        val out = ByteArrayOutputStream()
        scaled.compress(Bitmap.CompressFormat.JPEG, S3WatchProtocol.JPEG_QUALITY, out)
        return out.toByteArray()
    }

    private fun advertisedName(result: ScanResult): String? {
        result.device?.name?.takeIf { it.isNotBlank() }?.let { return it }
        val record = result.scanRecord?.bytes ?: return null
        return parseCompleteLocalName(record)
    }

    private fun parseCompleteLocalName(scanRecord: ByteArray): String? {
        var pos = 0
        while (pos < scanRecord.size - 1) {
            val size = scanRecord[pos].toInt() and 0xFF
            if (size == 0 || pos + size >= scanRecord.size) break
            val type = scanRecord[pos + 1].toInt() and 0xFF
            if (type == 0x08 || type == 0x09) {
                return String(scanRecord, pos + 2, size - 1, StandardCharsets.UTF_8).trim()
            }
            pos += size + 1
        }
        return null
    }

    private fun looksLikeMac(value: String): Boolean {
        return value.matches(Regex("([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}"))
    }

    private fun updateConnectionState(state: String) {
        DeviceStore.apply("glasses", "connectionState", state)
    }

    private fun cleanupGatt(keepState: Boolean = false) {
        writeQueue.clear()
        notifyQueue.clear()
        writeInFlight = false
        notifyWriteInFlight = false
        ctrlChar = null
        evtChar = null
        imgChar = null
        micChar = null
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (_: Throwable) {
        }
        gatt = null
        if (!keepState) {
            connecting = false
        }
    }

    private data class WriteOp(val uuid: UUID, val payload: ByteArray, val noResponse: Boolean)
}
