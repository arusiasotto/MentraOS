/**
 * @fileoverview ESP32-S3 Watch Hardware Capabilities
 *
 * Waveshare ESP32-S3-Touch-AMOLED-2.06 presented to MentraOS as a
 * single-display wearable (text, JPEG images, microphone). Unofficial
 * community support — not a Waveshare product and not affiliated with
 * Waveshare.
 */

import type { Capabilities } from "../hardware";

export const s3Watch: Capabilities = {
  modelName: "ESP32-S3 Watch",

  hasCamera: false,
  camera: null,

  hasDisplay: true,
  display: {
    count: 1,
    isColor: false,
    color: "green",
    canDisplayBitmap: true,
    resolution: { width: 410, height: 502 },
    maxTextLines: 10,
    adjustBrightness: true,

    // Public canvas is the AMOLED safe area. G2-hardcoded 576×288 boxes
    // are kept via fitOverflowImages; the SGC letterboxes those frames.
    width: 378,
    height: 414,
    canPosition: true,
    maxTextElements: 6,
    maxImageElements: 4,
    maxImagePx: { width: 378, height: 414 },
    shapes: ["rect"],
    intensityLevels: 2,
    partialUpdate: false,
    fitOverflowImages: true,
  },

  hasMicrophone: true,
  microphone: {
    count: 1,
    hasVAD: false,
  },

  hasSpeaker: false,
  speaker: null,

  hasIMU: false,
  imu: null,

  hasButton: true,
  button: {
    count: 1,
    buttons: [{
      type: "swipe1d",
      events: ["TAP", "DOUBLE_TAP", "PRESS_HOLD", "SWIPE_UP", "SWIPE_DOWN"],
      isCapacitive: true,
    }],
  },

  hasLight: false,
  light: null,

  power: {
    hasExternalBattery: false,
  },

  hasWifi: false,
  hasOta: false,
};
