/**
 * @fileoverview ESP32-S3 Watch Hardware Capabilities
 *
 * Unofficial MentraOS support for the Waveshare ESP32-S3-Touch-AMOLED-2.06.
 * Not a Waveshare product and not affiliated with Waveshare.
 */

import type { Capabilities } from "@mentra/sdk";

export const s3Watch: Capabilities = {
  modelName: "ESP32-S3 Watch",

  hasCamera: false,
  camera: null,

  hasDisplay: true,
  display: {
    count: 1,
    isColor: true,
    color: "full_color",
    canDisplayBitmap: true,
    resolution: { width: 410, height: 502 },
    maxTextLines: 16,
    adjustBrightness: true,
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
