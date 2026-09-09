/// <reference types="bun-types" />

import {beforeEach, describe, expect, jest, mock, test} from "bun:test"

const startLocationUpdatesAsync = jest.fn(async () => {})
const stopLocationUpdatesAsync = jest.fn(async () => {})
const hasStartedLocationUpdatesAsync = jest.fn(async () => false)

mock.module("expo-location", () => ({
  LocationAccuracy: {
    BestForNavigation: 1,
    High: 2,
    Balanced: 3,
    Low: 4,
    Lowest: 5,
  },
  startLocationUpdatesAsync,
  stopLocationUpdatesAsync,
  hasStartedLocationUpdatesAsync,
}))

mock.module("expo-task-manager", () => ({
  defineTask: jest.fn(),
}))

mock.module("../LocalMiniappRuntime", () => ({
  default: {forwardEvent: jest.fn()},
}))

const {
  LOCATION_FOREGROUND_SERVICE,
  LOCATION_TASK_NAME,
  getLocationAccuracy,
  locationTaskOptions,
  setLocationTier,
} = require("../PhoneLocationService") as typeof import("../PhoneLocationService")

describe("PhoneLocationService", () => {
  beforeEach(() => {
    startLocationUpdatesAsync.mockClear()
    stopLocationUpdatesAsync.mockClear()
    hasStartedLocationUpdatesAsync.mockClear()
    hasStartedLocationUpdatesAsync.mockResolvedValue(false)
  })

  test("maps realtime miniapp demand to navigation-grade accuracy", () => {
    expect(getLocationAccuracy("realtime")).toBe(1)
    expect(getLocationAccuracy("high")).toBe(2)
    expect(getLocationAccuracy("unknown")).toBe(5)
  })

  test("starts live GPS as a location foreground service, not a true-background task", async () => {
    await setLocationTier("realtime")

    expect(startLocationUpdatesAsync).toHaveBeenCalledTimes(1)
    expect(startLocationUpdatesAsync.mock.calls[0][0]).toBe(LOCATION_TASK_NAME)
    expect(startLocationUpdatesAsync.mock.calls[0][1]).toEqual(locationTaskOptions("realtime"))
    expect(startLocationUpdatesAsync.mock.calls[0][1].foregroundService).toEqual({
      notificationTitle: LOCATION_FOREGROUND_SERVICE.notificationTitle,
      notificationBody: LOCATION_FOREGROUND_SERVICE.notificationBody,
    })
  })

  test("stops the task when no miniapp is asking for location", async () => {
    hasStartedLocationUpdatesAsync.mockResolvedValue(true)

    await setLocationTier("off")

    expect(stopLocationUpdatesAsync).toHaveBeenCalledWith(LOCATION_TASK_NAME)
    expect(startLocationUpdatesAsync).not.toHaveBeenCalled()
  })
})
