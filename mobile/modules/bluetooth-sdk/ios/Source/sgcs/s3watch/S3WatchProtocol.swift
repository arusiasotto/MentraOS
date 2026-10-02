//
//  S3WatchProtocol.swift
//
//  UNTESTED ALPHA — iOS has never been run against the watch.
//  Wire format shared with firmware/s3-watch/settings.h and
//  android/.../sgcs/s3watch/S3WatchProtocol.kt.
//
//  Unofficial MentraOS integration for Waveshare ESP32-S3-Touch-AMOLED-2.06
//  hardware. Not a Waveshare product and not affiliated with Waveshare.
//

import CoreBluetooth
import CoreGraphics
import Foundation

enum S3WatchProtocol {
    static let advNamePrefix = "S3Watch"

    static let serviceUUID = CBUUID(string: "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d010")
    static let ctrlUUID = CBUUID(string: "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d011")
    static let evtUUID = CBUUID(string: "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d012")
    static let imgUUID = CBUUID(string: "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d013")
    static let micUUID = CBUUID(string: "c3a1b410-9e2f-4d6a-8c15-7b4e2f90d014")

    static let hdrLen = 4

    static let cmdText: UInt8 = 0x01
    static let cmdClear: UInt8 = 0x02
    static let cmdBrightness: UInt8 = 0x03
    static let cmdMicEnable: UInt8 = 0x04
    /// Payload: unix seconds u32le, timezone offset minutes i16le (includes DST).
    static let cmdTimeSync: UInt8 = 0x05
    static let cmdMenu: UInt8 = 0x06
    static let cmdImgBegin: UInt8 = 0x10
    static let cmdImgEnd: UInt8 = 0x12

    static let evtAck: UInt8 = 0x80
    static let evtBattery: UInt8 = 0x81
    static let evtReady: UInt8 = 0x82
    static let evtGesture: UInt8 = 0x83
    static let evtMenuSelect: UInt8 = 0x84

    static let menuMaxItems = 10
    static let menuNameMax = 15

    struct MenuEntry {
        let running: Bool
        let name: String
    }

    static func encodeMenu(_ items: [MenuEntry]) -> Data {
        let capped = items.prefix(menuMaxItems)
        var out = Data()
        out.append(UInt8(capped.count))
        for item in capped {
            let trimmed = String(item.name.trimmingCharacters(in: .whitespacesAndNewlines).prefix(menuNameMax))
            let name = Data(trimmed.utf8)
            out.append(item.running ? 1 : 0)
            out.append(UInt8(name.count))
            out.append(name)
        }
        return out
    }

    static let gestureSwipeUp: UInt8 = 0x01
    static let gestureSwipeDown: UInt8 = 0x02
    static let gestureSingleTap: UInt8 = 0x03
    static let gestureDoubleTap: UInt8 = 0x04
    static let gestureLongPress: UInt8 = 0x05

    static let displayWidth = 410
    static let displayHeight = 502
    /// Public scene canvas = AMOLED safe area. G2 frames use 576×288.
    static let sceneWidth = 378
    static let sceneHeight = 414
    static let g2Width = 576
    static let g2Height = 288
    static let sceneOriginX = 16
    static let sceneOriginY = 64
    static let sceneLineHeight: CGFloat = 40
    static let sceneTextSize: CGFloat = 28
    static let jpegQuality: CGFloat = 0.85
    /// Mentra HUD green (#00FF88) — same tint as GlassesDisplayMirror.
    static let hudGreenR = 0
    static let hudGreenG = 255
    static let hudGreenB = 136
    /// G2 image path is 4-bit (16 greens). Quantize to that, do not 1-bit dither.
    static let hudIntensityLevels = 16
    static let requestedMtu = 512
    static let micSampleRate = 16000

    static func matchesAdvertisedName(_ name: String?) -> Bool {
        let trimmed = name?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return trimmed.lowercased().hasPrefix(advNamePrefix.lowercased())
    }

    static func gestureName(_ id: UInt8) -> String? {
        switch id {
        case gestureSwipeUp: return "swipe_up"
        case gestureSwipeDown: return "swipe_down"
        case gestureSingleTap: return "single_tap"
        case gestureDoubleTap: return "double_tap"
        case gestureLongPress: return "long_press"
        default: return nil
        }
    }

    static func encode(opcode: UInt8, seq: UInt8, payload: Data = Data()) -> Data {
        var out = Data(count: hdrLen + payload.count)
        out[0] = opcode
        out[1] = seq
        out[2] = UInt8(payload.count & 0xFF)
        out[3] = UInt8((payload.count >> 8) & 0xFF)
        if !payload.isEmpty {
            out.replaceSubrange(hdrLen ..< (hdrLen + payload.count), with: payload)
        }
        return out
    }

    static func decode(_ packet: Data?) -> (opcode: UInt8, seq: UInt8, payload: Data)? {
        guard let packet, packet.count >= hdrLen else { return nil }
        let len = Int(packet[2]) | (Int(packet[3]) << 8)
        let end = min(hdrLen + len, packet.count)
        return (packet[0], packet[1], packet.subdata(in: hdrLen ..< end))
    }
}
