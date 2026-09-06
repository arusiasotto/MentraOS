import {DisplayProfile} from "./types"
import {G2_PROFILE} from "./g2"

/**
 * ESP32-S3 Watch text profile.
 *
 * Public scene canvas is the AMOLED safe area (378×414). Glyph metrics
 * match G2 so Mentra Maps wrap still fits when a frame is letterboxed.
 * Firmware CMD_TEXT still uses Arduino GFX textSize 2 at origin (16, 64).
 */
export const S3_WATCH_PROFILE: DisplayProfile = {
  ...G2_PROFILE,
  id: "esp32-s3-watch",
  name: "ESP32-S3 Watch",
  displayWidthPx: 378,
  displayHeightPx: 414,
  maxLines: 10,
  lineHeightPx: 40,
  maxPayloadBytes: 512,
  bleChunkSize: 180,
}

export const S3_WATCH_HYPHEN_WIDTH_PX = 10
export const S3_WATCH_SPACE_WIDTH_PX = 6
