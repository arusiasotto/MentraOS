package com.mentra.bluetoothsdk.sgcs.s3watch

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class S3WatchProtocolTest {
    @Test
    fun encodeDecodeRoundTrip() {
        val payload = byteArrayOf(1, 2, 3)
        val encoded = S3WatchProtocol.encode(S3WatchProtocol.CMD_BRIGHTNESS, 7, payload)
        val decoded = S3WatchProtocol.decode(encoded)
        assertThat(decoded).isNotNull
        assertThat(decoded!!.first).isEqualTo(S3WatchProtocol.CMD_BRIGHTNESS)
        assertThat(decoded.second).isEqualTo(7)
        assertThat(decoded.third).containsExactly(1, 2, 3)
    }

    @Test
    fun mapsGestureBytesToMentraNames() {
        assertThat(S3WatchProtocol.gestureName(S3WatchProtocol.GESTURE_SWIPE_UP)).isEqualTo("swipe_up")
        assertThat(S3WatchProtocol.gestureName(S3WatchProtocol.GESTURE_SWIPE_DOWN)).isEqualTo("swipe_down")
        assertThat(S3WatchProtocol.gestureName(S3WatchProtocol.GESTURE_SINGLE_TAP)).isEqualTo("single_tap")
        assertThat(S3WatchProtocol.gestureName(S3WatchProtocol.GESTURE_DOUBLE_TAP)).isEqualTo("double_tap")
        assertThat(S3WatchProtocol.gestureName(S3WatchProtocol.GESTURE_LONG_PRESS)).isEqualTo("long_press")
        assertThat(S3WatchProtocol.gestureName(0x7F.toByte())).isNull()
    }

    @Test
    fun encodeMenuWritesCountRunningAndTruncatedNames() {
        val encoded =
            S3WatchProtocol.encodeMenu(
                listOf(
                    S3WatchProtocol.MenuEntry(true, "Captions"),
                    S3WatchProtocol.MenuEntry(false, "ThisNameIsWayTooLong"),
                )
            )
        assertThat(encoded[0]).isEqualTo(2)
        assertThat(encoded[1]).isEqualTo(1)
        assertThat(encoded[2]).isEqualTo(8)
        assertThat(String(encoded, 3, 8, Charsets.UTF_8)).isEqualTo("Captions")
        val second = 3 + 8
        assertThat(encoded[second]).isEqualTo(0)
        assertThat(encoded[second + 1].toInt() and 0xFF).isEqualTo(S3WatchProtocol.MENU_NAME_MAX)
        assertThat(String(encoded, second + 2, S3WatchProtocol.MENU_NAME_MAX, Charsets.UTF_8))
            .isEqualTo("ThisNameIsWayTo")
    }
}
