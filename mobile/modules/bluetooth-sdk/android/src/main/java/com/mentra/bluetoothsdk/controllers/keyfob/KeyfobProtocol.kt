package com.mentra.bluetoothsdk.controllers.keyfob

import java.util.UUID

/**
 * Wire format shared with `firmware/keyfob/settings.h`.
 * Control/event frame: `[opcode:u8][seq:u8][len:u16le][payload]`.
 *
 * Unofficial MentraOS integration for Seeed Studio XIAO nRF52840 Plus.
 * Not a Seeed product and not affiliated with Seeed Studio.
 */
object KeyfobProtocol {
    const val ADV_NAME_PREFIX = "Keyfob"

    val SERVICE_UUID: UUID = UUID.fromString("d4b2c520-8f1e-4c7a-9b03-6a5d4e80e020")
    val CTRL_UUID: UUID = UUID.fromString("d4b2c520-8f1e-4c7a-9b03-6a5d4e80e021")
    val EVT_UUID: UUID = UUID.fromString("d4b2c520-8f1e-4c7a-9b03-6a5d4e80e022")
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    const val HDR_LEN = 4

    const val CMD_LED: Byte = 0x01
    const val CMD_PING: Byte = 0x02

    const val EVT_ACK: Byte = 0x80.toByte()
    const val EVT_BATTERY: Byte = 0x81.toByte()
    const val EVT_READY: Byte = 0x82.toByte()
    const val EVT_GESTURE: Byte = 0x83.toByte()

    const val GESTURE_HOLD: Byte = 0x01
    const val GESTURE_SINGLE_TAP: Byte = 0x02
    const val GESTURE_DOUBLE_TAP: Byte = 0x03
    const val GESTURE_SWIPE_UP: Byte = 0x04
    const val GESTURE_SWIPE_DOWN: Byte = 0x05

    fun matchesAdvertisedName(name: String?): Boolean {
        val trimmed = name?.trim().orEmpty()
        return trimmed.startsWith(ADV_NAME_PREFIX, ignoreCase = true)
    }

    fun gestureName(id: Byte): String? =
        when (id) {
            GESTURE_HOLD -> "hold"
            GESTURE_SINGLE_TAP -> "single_tap"
            GESTURE_DOUBLE_TAP -> "double_tap"
            GESTURE_SWIPE_UP -> "swipe_up"
            GESTURE_SWIPE_DOWN -> "swipe_down"
            else -> null
        }

    fun encode(opcode: Byte, seq: Int, payload: ByteArray = ByteArray(0)): ByteArray {
        val out = ByteArray(HDR_LEN + payload.size)
        out[0] = opcode
        out[1] = (seq and 0xFF).toByte()
        out[2] = (payload.size and 0xFF).toByte()
        out[3] = ((payload.size shr 8) and 0xFF).toByte()
        if (payload.isNotEmpty()) {
            System.arraycopy(payload, 0, out, HDR_LEN, payload.size)
        }
        return out
    }

    fun decode(packet: ByteArray?): Triple<Byte, Int, ByteArray>? {
        if (packet == null || packet.size < HDR_LEN) return null
        val len = (packet[2].toInt() and 0xFF) or ((packet[3].toInt() and 0xFF) shl 8)
        val end = (HDR_LEN + len).coerceAtMost(packet.size)
        return Triple(packet[0], packet[1].toInt() and 0xFF, packet.copyOfRange(HDR_LEN, end))
    }
}
