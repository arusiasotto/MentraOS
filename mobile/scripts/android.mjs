#!/usr/bin/env zx
import {existsSync} from "fs"
import {homedir} from "os"
import {readFile, writeFile} from "fs/promises"
import {setBuildEnv} from "./set-build-env.mjs"
await setBuildEnv()

function resolveAndroidSdk() {
  const fromEnv = process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT
  const candidates = [fromEnv, `${homedir()}/Android/Sdk`, `${homedir()}/Android/sdk`].filter(Boolean)
  return candidates.find((path) => existsSync(path)) ?? null
}

function resolveJavaHome() {
  if (process.env.JAVA_HOME && existsSync(process.env.JAVA_HOME)) {
    return process.env.JAVA_HOME
  }
  const candidates = [
    "/usr/lib/jvm/java-17-temurin-jdk",
    "/usr/lib/jvm/temurin-17-jdk",
    "/usr/lib/jvm/java-17-openjdk",
  ]
  return candidates.find((path) => existsSync(path)) ?? null
}

const androidSdk = resolveAndroidSdk()
if (!androidSdk) {
  console.error("Android SDK not found. Install Android Studio or set ANDROID_HOME.")
  process.exit(1)
}
process.env.ANDROID_HOME = androidSdk
process.env.ANDROID_SDK_ROOT = androidSdk

const javaHome = resolveJavaHome()
if (javaHome) {
  process.env.JAVA_HOME = javaHome
  process.env.PATH = `${javaHome}/bin:${process.env.PATH}`
}

// prebuild android:
await $({stdio: "inherit"})`bun expo prebuild --platform android`

await writeFile("android/local.properties", `sdk.dir=${androidSdk}\n`)

// Firebase google-services.json only lists com.mentra.mentra. Clone that
// client for MENTRAOS_BUILD_NAME suffixes (e.g. com.mentra.mentra.watch) so
// processDebugGoogleServices does not fail. Same approach as android-release.mjs.
const variantName = process.env.MENTRAOS_BUILD_NAME?.trim() || null
if (variantName && /^[a-zA-Z][a-zA-Z0-9_ ]*$/.test(variantName)) {
  const newPkg = `com.mentra.mentra.${variantName.toLowerCase().replace(/[^a-zA-Z0-9_]/g, "")}`
  const gsPath = "android/app/google-services.json"
  if (existsSync(gsPath)) {
    const gs = JSON.parse(await readFile(gsPath, "utf-8"))
    const baseClient = gs.client?.find(
      (c) => c.client_info?.android_client_info?.package_name === "com.mentra.mentra",
    )
    const alreadyHas = gs.client?.some(
      (c) => c.client_info?.android_client_info?.package_name === newPkg,
    )
    if (baseClient && !alreadyHas) {
      const clone = JSON.parse(JSON.stringify(baseClient))
      clone.client_info.android_client_info.package_name = newPkg
      gs.client.push(clone)
      await writeFile(gsPath, JSON.stringify(gs, null, 2) + "\n")
      console.log(`[google-services] cloned client entry for ${newPkg}`)
    }
  }
}

// Bust stale RN autolinking caches. The RN gradle plugin caches autolinking.json
// keyed only by its own inputs, not by android/app/build.gradle. Prebuild can
// rewrite the namespace between runs, but the cached JSON keeps the old
// packageName — producing a wrong-package BuildConfig reference in the
// generated ReactNativeApplicationEntryPoint.java. Wipe after every prebuild.
await $({stdio: "inherit", nothrow: true})`rm -rf android/build/generated/autolinking android/app/build/generated/autolinking`

// Get connected devices with details
const adbOutput = await $`adb devices -l`
const lines = adbOutput.stdout.trim().split('\n').slice(1)

// Filter to physical devices that don't contain "live"
const validDevices = lines.filter(line => 
  line.trim() && 
  !line.includes('emulator') && 
  !line.toLowerCase().includes('live') &&
  !line.startsWith('emulator')
)

if (validDevices.length === 0) {
  console.error('No suitable physical device found')
  process.exit(1)
}

// build only for real devices new arch:
process.env.ORG_GRADLE_PROJECT_reactNativeArchitectures = 'arm64-v8a'

if (validDevices.length > 1) {
  console.log('Multiple devices found, launching interactive picker')
  await $({stdio: "inherit"})`bun expo run:android --device`
} else {
  const modelMatch = validDevices[0].match(/model:(\S+)/)
  const deviceName = modelMatch ? modelMatch[1] : validDevices[0].split(/\s+/)[0]
  console.log(`Using device: ${deviceName}`)
  await $({stdio: "inherit"})`bun expo run:android --device ${deviceName}`
}