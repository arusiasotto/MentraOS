package com.mentra.crust.navigation

import android.app.Activity
import android.util.Log

/**
 * No-op NavigationManager used when the Mapbox Navigation SDK is not on the
 * classpath. Local/debug builds without a real `MAPBOX_DOWNLOADS_TOKEN`
 * (`sk.…`) compile this instead of downloading proprietary Mapbox AARs.
 *
 * The public surface matches the Mapbox implementation so [CrustModule]
 * does not change. Turn-by-turn navigation is unavailable in this build.
 */
object NavigationManager {
  private const val TAG = "NavigationManager"
  private const val UNAVAILABLE =
    "Navigation SDK not included in this build (dummy or missing MAPBOX_DOWNLOADS_TOKEN)"

  data class ManeuverPayload(
    val maneuverType: String,
    val distanceMeters: Int,
    val fromRoad: String?,
    val toRoad: String?,
    val nextStepRoad: String?,
    val distanceToDestinationMeters: Int = -1,
    val timeToDestinationSeconds: Int = -1,
    val currentSpeedMps: Float? = null,
    val speedLimitMps: Float? = null,
    val routeHeadingDeg: Float? = null,
    val instruction: String? = null,
  )

  data class LocationPayload(
    val lat: Double,
    val lng: Double,
    val accuracy: Float?,
    val timestamp: Long,
  )

  data class RoutePoint(val lat: Double, val lng: Double)

  data class RouteStep(
    val lat: Double,
    val lng: Double,
    val routeIndex: Int,
    val road: String?,
    val maneuver: String,
    val distanceMeters: Int,
  )

  interface Callbacks {
    fun onManeuver(payload: ManeuverPayload)
    fun onRerouting()
    fun onArrived()
    fun onError(message: String)
    fun onLocation(payload: LocationPayload)
    fun onRoute(points: List<RoutePoint>, steps: List<RouteStep>?)
    fun onOffRoute(perpendicularDistanceMeters: Double)
  }

  data class StartOptions(
    val stops: List<Pair<Double, Double>>,
    val mode: String = "driving",
    val avoidHighways: Boolean = false,
    val avoidTolls: Boolean = false,
    val avoidFerries: Boolean = false,
    val simulate: Boolean = false,
    val speedMultiplier: Float = 1f,
  )

  fun ensureTermsAccepted(activity: Activity, onResult: (accepted: Boolean) -> Unit) {
    onResult(true)
  }

  fun resetTermsAccepted(activity: Activity) {}

  fun start(activity: Activity, options: StartOptions, callbacks: Callbacks) {
    Log.w(TAG, UNAVAILABLE)
    callbacks.onError(UNAVAILABLE)
  }

  fun stop() {}

  fun simulateDeviation(offsetMeters: Double = 0.0) {}

  fun setWrongSidewalkOffset(enabled: Boolean) {}

  fun setSkipCrossings(enabled: Boolean) {}
}
