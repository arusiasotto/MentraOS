//
//  S3WatchProtocolTests.swift
//
//  UNTESTED ALPHA — protocol unit tests only. No iOS hardware run.
//  Mirrors android/.../s3watch/S3WatchProtocolTest.kt.
//

import XCTest
@testable import MentraBluetoothSDK

final class S3WatchProtocolTests: XCTestCase {
    func testEncodeDecodeRoundTrip() {
        let payload = Data([1, 2, 3])
        let encoded = S3WatchProtocol.encode(opcode: S3WatchProtocol.cmdBrightness, seq: 7, payload: payload)
        let decoded = S3WatchProtocol.decode(encoded)
        XCTAssertNotNil(decoded)
        XCTAssertEqual(decoded?.opcode, S3WatchProtocol.cmdBrightness)
        XCTAssertEqual(decoded?.seq, 7)
        XCTAssertEqual(Array(decoded?.payload ?? Data()), [1, 2, 3])
    }

    func testMapsGestureBytesToMentraNames() {
        XCTAssertEqual(S3WatchProtocol.gestureName(S3WatchProtocol.gestureSwipeUp), "swipe_up")
        XCTAssertEqual(S3WatchProtocol.gestureName(S3WatchProtocol.gestureSwipeDown), "swipe_down")
        XCTAssertEqual(S3WatchProtocol.gestureName(S3WatchProtocol.gestureSingleTap), "single_tap")
        XCTAssertEqual(S3WatchProtocol.gestureName(S3WatchProtocol.gestureDoubleTap), "double_tap")
        XCTAssertEqual(S3WatchProtocol.gestureName(S3WatchProtocol.gestureLongPress), "long_press")
        XCTAssertNil(S3WatchProtocol.gestureName(0x7F))
    }

    func testEncodeMenuWritesCountRunningAndTruncatedNames() {
        let encoded = S3WatchProtocol.encodeMenu([
            .init(running: true, name: "Captions"),
            .init(running: false, name: "ThisNameIsWayTooLong"),
        ])
        XCTAssertEqual(encoded[0], 2)
        XCTAssertEqual(encoded[1], 1)
        XCTAssertEqual(encoded[2], 8)
        XCTAssertEqual(String(data: encoded.subdata(in: 3 ..< 11), encoding: .utf8), "Captions")
        let second = 3 + 8
        XCTAssertEqual(encoded[second], 0)
        XCTAssertEqual(Int(encoded[second + 1]), S3WatchProtocol.menuNameMax)
        let nameRange = (second + 2) ..< (second + 2 + S3WatchProtocol.menuNameMax)
        XCTAssertEqual(String(data: encoded.subdata(in: nameRange), encoding: .utf8), "ThisNameIsWayTo")
    }

    func testMatchesAdvertisedName() {
        XCTAssertTrue(S3WatchProtocol.matchesAdvertisedName("S3Watch-90C859"))
        XCTAssertTrue(S3WatchProtocol.matchesAdvertisedName("s3watch"))
        XCTAssertFalse(S3WatchProtocol.matchesAdvertisedName("Mentra Live"))
        XCTAssertFalse(S3WatchProtocol.matchesAdvertisedName(nil))
    }
}
