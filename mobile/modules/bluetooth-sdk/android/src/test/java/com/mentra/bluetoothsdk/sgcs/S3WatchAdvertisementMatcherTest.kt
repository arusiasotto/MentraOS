package com.mentra.bluetoothsdk.sgcs

import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class S3WatchAdvertisementMatcherTest {
    @Test
    fun matchesS3WatchNamePrefix() {
        assertThat(S3Watch.matchesAdvertisedName("S3Watch-CEC5BA")).isTrue()
        assertThat(S3Watch.matchesAdvertisedName("s3watch")).isTrue()
        assertThat(S3Watch.matchesAdvertisedName("S3Watch")).isTrue()

        assertThat(S3Watch.matchesAdvertisedName("AR99")).isFalse()
        assertThat(S3Watch.matchesAdvertisedName("G1")).isFalse()
        assertThat(S3Watch.matchesAdvertisedName("")).isFalse()
        assertThat(S3Watch.matchesAdvertisedName(null)).isFalse()
    }
}
