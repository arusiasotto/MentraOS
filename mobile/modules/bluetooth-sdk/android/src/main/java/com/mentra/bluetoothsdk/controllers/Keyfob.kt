package com.mentra.bluetoothsdk.controllers

import android.Manifest
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
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import com.mentra.bluetoothsdk.Bridge
import com.mentra.bluetoothsdk.DeviceStore
import com.mentra.bluetoothsdk.PhotoRequest
import com.mentra.bluetoothsdk.controllers.keyfob.KeyfobProtocol
import com.mentra.bluetoothsdk.utils.ControllerTypes
import java.util.ArrayDeque
import java.util.UUID

/**
 * Unofficial MentraOS controller for Seeed Studio XIAO nRF52840 Plus.
 * Not a Seeed product and not affiliated with Seeed Studio.
 */
class Keyfob : ControllerManager() {
    companion object {
        private const val TAG = "Keyfob"
        private const val SCAN_DURATION_MS = 15_000L
        private const val PREFS = "keyfob_prefs"
        private const val PREF_BLE_ADDRESS = "keyfob_bleAddress"
        private const val PREF_NAME = "keyfob_name"

        @JvmStatic
        fun matchesAdvertisedName(name: String?): Boolean = KeyfobProtocol.matchesAdvertisedName(name)
    }

    private val appContext: Context = Bridge.getContext()
    private val prefs = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    private val handler = Handler(Looper.getMainLooper())
    private var bluetoothAdapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()

    private var scanner: BluetoothLeScanner? = null
    private var scanCallback: ScanCallback? = null
    private var scanTimeout: Runnable? = null
    private var deviceSearchId: String = "NOT_SET"
    private val discoveredNames = HashSet<String>()

    private var gatt: BluetoothGatt? = null
    private var ctrlChar: BluetoothGattCharacteristic? = null
    private var evtChar: BluetoothGattCharacteristic? = null
    private val notifyQueue: ArrayDeque<BluetoothGattCharacteristic> = ArrayDeque()
    private var notifyWriteInFlight = false
    private val writeQueue: ArrayDeque<ByteArray> = ArrayDeque()
    private var writeInFlight = false
    private var seq = 1
    private var isDisconnecting = false

    private var bleAddress: String?
        get() = prefs.getString(PREF_BLE_ADDRESS, null)
        set(value) {
            prefs.edit().apply {
                if (value == null) remove(PREF_BLE_ADDRESS) else putString(PREF_BLE_ADDRESS, value)
                apply()
            }
        }

    private var savedName: String?
        get() = prefs.getString(PREF_NAME, null)
        set(value) {
            prefs.edit().apply {
                if (value == null) remove(PREF_NAME) else putString(PREF_NAME, value)
                apply()
            }
        }

    init {
        type = ControllerTypes.KEYFOB
        hasMic = false
    }

    private fun hasScanPermission(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return true
        return ContextCompat.checkSelfPermission(
            appContext, Manifest.permission.BLUETOOTH_SCAN
        ) == PackageManager.PERMISSION_GRANTED
    }

    private fun hasConnectPermission(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return true
        return ContextCompat.checkSelfPermission(
            appContext, Manifest.permission.BLUETOOTH_CONNECT
        ) == PackageManager.PERMISSION_GRANTED
    }

    override fun findCompatibleDevices() {
        Bridge.log("$TAG: findCompatibleDevices()")
        deviceSearchId = "NOT_SET"
        discoveredNames.clear()
        startScan(forConnection = false)
    }

    override fun connectById(id: String) {
        Bridge.log("$TAG: connectById($id)")
        deviceSearchId = id
        if (connectByStoredAddress(id)) return
        startScan(forConnection = true)
    }

    override fun stopScan() {
        scanTimeout?.let { handler.removeCallbacks(it) }
        scanTimeout = null
        val s = scanner ?: return
        val cb = scanCallback ?: return
        try {
            s.stopScan(cb)
        } catch (e: SecurityException) {
            Bridge.log("$TAG: stopScan SecurityException: ${e.message}")
        }
        scanCallback = null
        scanner = null
    }

    private fun startScan(forConnection: Boolean) {
        Bridge.log("$TAG: startScan(forConnection=$forConnection)")
        val adapter = bluetoothAdapter ?: BluetoothAdapter.getDefaultAdapter().also { bluetoothAdapter = it }
        if (adapter == null || !adapter.isEnabled) {
            Bridge.log("$TAG: Bluetooth unavailable or disabled")
            return
        }
        if (!hasScanPermission() || !hasConnectPermission()) {
            Bridge.log("$TAG: Missing Bluetooth permissions")
            return
        }
        if (gatt != null) {
            Bridge.log("$TAG: Already connected, skipping scan")
            return
        }
        isDisconnecting = false
        stopScan()
        scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            Bridge.log("$TAG: BLE scanner unavailable")
            return
        }
        scanCallback =
            object : ScanCallback() {
                override fun onScanResult(callbackType: Int, result: ScanResult) {
                    handleScanResult(result, forConnection)
                }

                override fun onScanFailed(errorCode: Int) {
                    Bridge.log("$TAG: scan failed code=$errorCode")
                    stopScan()
                }
            }
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        try {
            scanner?.startScan(emptyList(), settings, scanCallback)
        } catch (e: SecurityException) {
            Bridge.log("$TAG: startScan SecurityException: ${e.message}")
            return
        }
        scanTimeout = Runnable { stopScan() }
        handler.postDelayed(scanTimeout!!, SCAN_DURATION_MS)
    }

    private fun advertisedName(result: ScanResult): String? {
        val fromRecord = result.scanRecord?.deviceName
        return try {
            result.device?.name ?: fromRecord
        } catch (e: SecurityException) {
            fromRecord
        }
    }

    private fun handleScanResult(result: ScanResult, forConnection: Boolean) {
        val device = result.device ?: return
        val advertised = advertisedName(result)
        if (!matchesAdvertisedName(advertised)) return
        val name = advertised ?: return
        val address = device.address.orEmpty()
        handler.post {
            if (!forConnection) {
                if (discoveredNames.add(name)) {
                    Bridge.sendDiscoveredDevice(type, name, address, result.rssi)
                }
                return@post
            }
            if (gatt != null) {
                stopScan()
                return@post
            }
            val target = deviceSearchId
            val matchesTarget =
                target == "NOT_SET" ||
                    target.isBlank() ||
                    target.equals(address, ignoreCase = true) ||
                    target.equals(name, ignoreCase = true) ||
                    name.contains(target, ignoreCase = true)
            if (!matchesTarget) return@post
            stopScan()
            connectGatt(device)
        }
    }

    private fun connectByStoredAddress(id: String): Boolean {
        val address = bleAddress ?: return false
        val knownName = savedName
        val idMatches =
            id.isBlank() ||
                id == "NOT_SET" ||
                id.equals(address, ignoreCase = true) ||
                (!knownName.isNullOrBlank() && id.equals(knownName, ignoreCase = true))
        if (!idMatches) return false
        val adapter = bluetoothAdapter ?: return false
        val device = try {
            adapter.getRemoteDevice(address)
        } catch (e: IllegalArgumentException) {
            Bridge.log("$TAG: Invalid stored BLE address: $address")
            return false
        }
        connectGatt(device)
        return true
    }

    private fun connectGatt(device: BluetoothDevice) {
        cleanupGatt(keepState = true)
        try {
            bleAddress = device.address
            gatt =
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    device.connectGatt(appContext, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
                } else {
                    device.connectGatt(appContext, false, gattCallback)
                }
            Bridge.log("$TAG: Connecting to ${device.name ?: device.address}")
        } catch (e: SecurityException) {
            Bridge.log("$TAG: connectGatt SecurityException: ${e.message}")
        }
    }

    private val gattCallback =
        object : BluetoothGattCallback() {
            override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    Bridge.log("$TAG: connected to ${gatt.device?.name}")
                    this@Keyfob.gatt = gatt
                    handler.postDelayed({
                        try {
                            gatt.discoverServices()
                        } catch (e: SecurityException) {
                            Bridge.log("$TAG: discoverServices SecurityException: ${e.message}")
                        }
                    }, 200)
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    Bridge.log("$TAG: disconnected status=$status")
                    resetControllerState()
                    cleanupGatt()
                }
            }

            override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Bridge.log("$TAG: service discovery failed status=$status")
                    return
                }
                val service = gatt.getService(KeyfobProtocol.SERVICE_UUID)
                if (service == null) {
                    Bridge.log("$TAG: Keyfob GATT service missing")
                    return
                }
                ctrlChar = service.getCharacteristic(KeyfobProtocol.CTRL_UUID)
                evtChar = service.getCharacteristic(KeyfobProtocol.EVT_UUID)
                notifyQueue.clear()
                evtChar?.let { notifyQueue.add(it) }
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

    private fun processNextNotify() {
        val localGatt = gatt ?: return
        if (notifyWriteInFlight) return
        val next = notifyQueue.poll()
        if (next == null) {
            markReady()
            return
        }
        try {
            localGatt.setCharacteristicNotification(next, true)
        } catch (e: SecurityException) {
            Bridge.log("$TAG: setCharacteristicNotification SecurityException: ${e.message}")
            processNextNotify()
            return
        }
        val cccd = next.getDescriptor(KeyfobProtocol.CCCD_UUID)
        if (cccd == null) {
            processNextNotify()
            return
        }
        notifyWriteInFlight = true
        cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        try {
            localGatt.writeDescriptor(cccd)
        } catch (e: SecurityException) {
            notifyWriteInFlight = false
            Bridge.log("$TAG: writeDescriptor SecurityException: ${e.message}")
            processNextNotify()
        }
    }

    private fun markReady() {
        val connectedName = getConnectedBluetoothName()
        if (!connectedName.isNullOrBlank()) {
            savedName = connectedName
            DeviceStore.apply("bluetooth", "controller_device_name", connectedName)
        }
        val address = try { gatt?.device?.address } catch (e: SecurityException) { null }
        if (!address.isNullOrBlank()) {
            bleAddress = address
            DeviceStore.apply("glasses", "controllerMacAddress", address)
        }
        DeviceStore.apply("glasses", "controllerConnected", true)
        DeviceStore.apply("glasses", "controllerFullyBooted", true)
        Bridge.log("$TAG: ready")
    }

    private fun handleNotify(uuid: UUID?, value: ByteArray?) {
        if (uuid != KeyfobProtocol.EVT_UUID) return
        val decoded = KeyfobProtocol.decode(value) ?: return
        val (opcode, _, payload) = decoded
        when (opcode) {
            KeyfobProtocol.EVT_BATTERY -> {
                if (payload.isNotEmpty()) {
                    val percent = (payload[0].toInt() and 0xFF).coerceIn(0, 100)
                    DeviceStore.apply("glasses", "controllerBatteryLevel", percent)
                }
            }
            KeyfobProtocol.EVT_READY -> markReady()
            KeyfobProtocol.EVT_GESTURE -> {
                if (payload.isEmpty()) return
                val name = KeyfobProtocol.gestureName(payload[0]) ?: return
                val source = if (payload.size > 1) payload[1].toInt() and 0xFF else null
                Bridge.log("$TAG: Gesture: $name button=$source")
                Bridge.sendTouchEvent(type, name, System.currentTimeMillis(), source)
            }
            else -> {}
        }
    }

    private fun enqueueControl(opcode: Byte, payload: ByteArray = ByteArray(0)) {
        writeQueue.add(KeyfobProtocol.encode(opcode, nextSeq(), payload))
        pumpWrites()
    }

    private fun nextSeq(): Int {
        val current = seq
        seq = if (seq == 255) 1 else seq + 1
        return current
    }

    private fun pumpWrites() {
        val localGatt = gatt ?: return
        val characteristic = ctrlChar ?: return
        if (writeInFlight) return
        val packet = writeQueue.poll() ?: return
        writeInFlight = true
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        characteristic.value = packet
        try {
            localGatt.writeCharacteristic(characteristic)
        } catch (e: SecurityException) {
            writeInFlight = false
            Bridge.log("$TAG: writeCharacteristic SecurityException: ${e.message}")
        }
    }

    private fun resetControllerState() {
        DeviceStore.apply("glasses", "controllerConnected", false)
        DeviceStore.apply("glasses", "controllerFullyBooted", false)
    }

    private fun cleanupGatt(keepState: Boolean = false) {
        writeQueue.clear()
        writeInFlight = false
        notifyQueue.clear()
        notifyWriteInFlight = false
        ctrlChar = null
        evtChar = null
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (e: SecurityException) {
            Bridge.log("$TAG: cleanupGatt SecurityException: ${e.message}")
        }
        gatt = null
        if (!keepState && !isDisconnecting) {
            resetControllerState()
        }
    }

    override fun disconnect() {
        Bridge.log("$TAG: disconnect()")
        isDisconnecting = true
        stopScan()
        resetControllerState()
        cleanupGatt(keepState = true)
    }

    override fun forget() {
        disconnect()
        bleAddress = null
        savedName = null
        deviceSearchId = "NOT_SET"
    }

    override fun cleanup() {
        disconnect()
    }

    override fun getConnectedBluetoothName(): String? {
        return try {
            gatt?.device?.name
        } catch (e: SecurityException) {
            null
        }
    }

    override fun ping() {
        enqueueControl(KeyfobProtocol.CMD_PING)
    }

    override fun getBatteryStatus() {
        enqueueControl(KeyfobProtocol.CMD_PING)
    }

    override fun sendRgbLedControl(
        requestId: String,
        packageName: String?,
        action: String,
        color: String?,
        onDurationMs: Int,
        offDurationMs: Int,
        count: Int,
    ) {
        val rgb =
            if (action.equals("off", ignoreCase = true)) {
                byteArrayOf(0, 0, 0)
            } else {
                parseRgb(color)
            }
        enqueueControl(KeyfobProtocol.CMD_LED, rgb)
    }

    private fun parseRgb(color: String?): ByteArray {
        val raw = color?.trim().orEmpty()
        if (raw.startsWith("#") && raw.length >= 7) {
            val r = raw.substring(1, 3).toIntOrNull(16) ?: 0
            val g = raw.substring(3, 5).toIntOrNull(16) ?: 0
            val b = raw.substring(5, 7).toIntOrNull(16) ?: 0
            return byteArrayOf(r.toByte(), g.toByte(), b.toByte())
        }
        return when (raw.lowercase()) {
            "red" -> byteArrayOf(255.toByte(), 0, 0)
            "green" -> byteArrayOf(0, 255.toByte(), 0)
            "blue" -> byteArrayOf(0, 0, 255.toByte())
            "white" -> byteArrayOf(255.toByte(), 255.toByte(), 255.toByte())
            else -> byteArrayOf(255.toByte(), 255.toByte(), 255.toByte())
        }
    }

    override fun sendIncidentId(incidentId: String) {}
    override fun setMicEnabled(enabled: Boolean) {}
    override fun sortMicRanking(list: MutableList<String>): MutableList<String> = list
    override fun sendJson(jsonOriginal: Map<String, Any>, wakeUp: Boolean, requireAck: Boolean) {}
    override fun requestPhoto(request: PhotoRequest) {}
    override fun startVideoRecording(requestId: String, save: Boolean, sound: Boolean) {}
    override fun stopVideoRecording(requestId: String) {}
    override fun startStream(message: Map<String, Any>) {}
    override fun stopStream() {}
    override fun sendStreamKeepAlive(message: Map<String, Any>) {}
    override fun sendButtonPhotoSettings() {}
    override fun sendButtonVideoRecordingSettings() {}
    override fun sendButtonMaxRecordingTime() {}
    override fun setBrightness(level: Int, autoMode: Boolean) {}
    override fun clearDisplay() {}
    override fun sendTextWall(text: String) {}
    override fun sendDoubleTextWall(top: String, bottom: String) {}
    override fun displayBitmap(
        base64ImageData: String,
        x: Int?,
        y: Int?,
        width: Int?,
        height: Int?,
    ): Boolean = false
    override fun showDashboard() {}
    override fun setDashboardPosition(height: Int, depth: Int) {}
    override fun setHeadUpAngle(angle: Int) {}
    override fun setSilentMode(enabled: Boolean) {}
    override fun exit() {}
    override fun sendShutdown() {
        disconnect()
    }
    override fun sendReboot() {}
    override fun requestWifiScan() {}
    override fun sendWifiCredentials(ssid: String, password: String) {}
    override fun forgetWifiNetwork(ssid: String) {}
    override fun sendHotspotState(enabled: Boolean) {}
    override fun sendOtaStart(otaVersionUrl: String?) {}
    override fun sendUserEmailToGlasses(email: String) {}
    override fun queryGalleryStatus() {}
    override fun sendGalleryMode() {}
    override fun requestVersionInfo() {}
}
