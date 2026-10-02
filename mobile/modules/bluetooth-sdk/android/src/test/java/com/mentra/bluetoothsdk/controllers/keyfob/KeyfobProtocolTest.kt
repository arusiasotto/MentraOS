package com.mentra.bluetoothsdk.controllers.keyfob

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class KeyfobProtocolTest {
    @Test
    fun encodeDecodeRoundTrip() {
        val payload = byteArrayOf(255.toByte(), 0, 40)
        val encoded = KeyfobProtocol.encode(KeyfobProtocol.CMD_LED, 4, payload)
        val decoded = KeyfobProtocol.decode(encoded)
        assertThat(decoded).isNotNull
        assertThat(decoded!!.first).isEqualTo(KeyfobProtocol.CMD_LED)
        assertThat(decoded.second).isEqualTo(4)
        assertThat(decoded.third).containsExactly(255.toByte(), 0, 40)
    }

    @Test
    fun mapsGestureBytesToR1Names() {
        assertThat(KeyfobProtocol.gestureName(KeyfobProtocol.GESTURE_HOLD)).isEqualTo("hold")
        assertThat(KeyfobProtocol.gestureName(KeyfobProtocol.GESTURE_SINGLE_TAP)).isEqualTo("single_tap")
        assertThat(KeyfobProtocol.gestureName(KeyfobProtocol.GESTURE_DOUBLE_TAP)).isEqualTo("double_tap")
        assertThat(KeyfobProtocol.gestureName(KeyfobProtocol.GESTURE_SWIPE_UP)).isEqualTo("swipe_up")
        assertThat(KeyfobProtocol.gestureName(KeyfobProtocol.GESTURE_SWIPE_DOWN)).isEqualTo("swipe_down")
        assertThat(KeyfobProtocol.gestureName(0x7F.toByte())).isNull()
    }

    @Test
    fun matchesKeyfobNamePrefix() {
        assertThat(KeyfobProtocol.matchesAdvertisedName("Keyfob-CEC5BA")).isTrue()
        assertThat(KeyfobProtocol.matchesAdvertisedName("keyfob")).isTrue()
        assertThat(KeyfobProtocol.matchesAdvertisedName("Keyfob")).isTrue()
        assertThat(KeyfobProtocol.matchesAdvertisedName("EVEN R1_CEC5BA")).isFalse()
        assertThat(KeyfobProtocol.matchesAdvertisedName("S3Watch")).isFalse()
        assertThat(KeyfobProtocol.matchesAdvertisedName("")).isFalse()
        assertThat(KeyfobProtocol.matchesAdvertisedName(null)).isFalse()
    }
}
