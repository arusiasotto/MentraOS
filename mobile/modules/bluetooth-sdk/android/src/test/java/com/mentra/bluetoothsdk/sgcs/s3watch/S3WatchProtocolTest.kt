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
}
