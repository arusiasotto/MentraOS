package com.mentra.bluetoothsdk.sgcs.s3watch

import java.util.UUID

/**
 * Wire format shared with `firmware/s3-watch/settings.h`.
 * Control/event frame: `[opcode:u8][seq:u8][len:u16le][payload]`.
 *
 * Unofficial MentraOS integration for Waveshare ESP32-S3-Touch-AMOLED-2.06
 * hardware. Not a Waveshare product and not affiliated with Waveshare.
 */
object S3WatchProtocol {
    const val ADV_NAME_PREFIX = "S3Watch"

    val SERVICE_UUID: UUID = UUID.fromString("c3a1b410-9e2f-4d6a-8c15-7b4e2f90d010")
    val CTRL_UUID: UUID = UUID.fromString("c3a1b410-9e2f-4d6a-8c15-7b4e2f90d011")
    val EVT_UUID: UUID = UUID.fromString("c3a1b410-9e2f-4d6a-8c15-7b4e2f90d012")
    val IMG_UUID: UUID = UUID.fromString("c3a1b410-9e2f-4d6a-8c15-7b4e2f90d013")
    val MIC_UUID: UUID = UUID.fromString("c3a1b410-9e2f-4d6a-8c15-7b4e2f90d014")
    val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    const val HDR_LEN = 4

    const val CMD_TEXT: Byte = 0x01
    const val CMD_CLEAR: Byte = 0x02
    const val CMD_BRIGHTNESS: Byte = 0x03
    const val CMD_MIC_ENABLE: Byte = 0x04
    const val CMD_TIME_SYNC: Byte = 0x05
    const val CMD_IMG_BEGIN: Byte = 0x10
    const val CMD_IMG_END: Byte = 0x12

    const val EVT_ACK: Byte = 0x80.toByte()
    const val EVT_BATTERY: Byte = 0x81.toByte()
    const val EVT_READY: Byte = 0x82.toByte()
    const val EVT_GESTURE: Byte = 0x83.toByte()

    const val GESTURE_SWIPE_UP: Byte = 0x01
    const val GESTURE_SWIPE_DOWN: Byte = 0x02
    const val GESTURE_SINGLE_TAP: Byte = 0x03
    const val GESTURE_DOUBLE_TAP: Byte = 0x04
    const val GESTURE_LONG_PRESS: Byte = 0x05

    const val DISPLAY_WIDTH = 410
    const val DISPLAY_HEIGHT = 502
    const val JPEG_QUALITY = 70
    const val REQUESTED_MTU = 512

    const val MIC_SAMPLE_RATE = 16000

    fun matchesAdvertisedName(name: String?): Boolean {
        val trimmed = name?.trim().orEmpty()
        return trimmed.startsWith(ADV_NAME_PREFIX, ignoreCase = true)
    }

    fun gestureName(id: Byte): String? =
        when (id) {
            GESTURE_SWIPE_UP -> "swipe_up"
            GESTURE_SWIPE_DOWN -> "swipe_down"
            GESTURE_SINGLE_TAP -> "single_tap"
            GESTURE_DOUBLE_TAP -> "double_tap"
            GESTURE_LONG_PRESS -> "long_press"
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
