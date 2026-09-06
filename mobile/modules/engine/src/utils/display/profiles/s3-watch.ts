import {DisplayProfile} from "./types"

/**
 * ESP32-S3 Watch text profile.
 *
 * Firmware draws Arduino GFX default font at textSize 2: 12×16 px cells,
 * origin (16, 64), 28 px line height. Keep these numbers in lockstep with
 * firmware/s3-watch/settings.h.
 */
const CHAR_W = 12

export const S3_WATCH_PROFILE: DisplayProfile = {
  id: "esp32-s3-watch",
  name: "ESP32-S3 Watch",
  displayWidthPx: 410 - 16 - 16,
  displayHeightPx: 502,
  maxLines: 13,
  lineHeightPx: 28,
  maxPayloadBytes: 512,
  bleChunkSize: 180,
  fontMetrics: {
    glyphWidths: new Map(),
    defaultGlyphWidth: CHAR_W / 2,
    renderFormula: (glyphWidth: number) => glyphWidth * 2,
    uniformScripts: {
      cjk: CHAR_W,
      hiragana: CHAR_W,
      katakana: CHAR_W,
      korean: CHAR_W,
      cyrillic: CHAR_W,
    },
    fallback: {
      latinMaxWidth: CHAR_W,
      unknownBehavior: "useLatinMax",
    },
  },
}

export const S3_WATCH_HYPHEN_WIDTH_PX = CHAR_W
export const S3_WATCH_SPACE_WIDTH_PX = CHAR_W
