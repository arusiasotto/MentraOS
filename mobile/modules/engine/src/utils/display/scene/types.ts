/**
 * Scene pipeline types — the host half of the miniapp `render()` display API.
 *
 * A miniapp describes a full frame ("scene") of positioned elements; the host
 * clamps/budgets/wraps it, diffs it against the last frame sent to the device,
 * and ships ONE annotated SceneFrame over the bridge. SGCs either walk the
 * per-element annotations (G2, Mentra Display) or serialize the whole frame
 * (full-frame devices). Spec: notes/miniapp-display-render-implementation-spec.md
 */

export interface SceneBox {
  x: number
  y: number
  w: number
  h: number
}

export type SceneBreakMode = "character" | "character-no-hyphen" | "word" | "strict-word"

export interface SceneTextStyle {
  /** Border width in px (0/absent = none). */
  border?: number
  /** Border corner radius in px. */
  radius?: number
  /** What happens to text that doesn't fit the box after wrapping. Default "clip". */
  overflow?: "clip" | "ellipsis"
  maxLines?: number
  textWindow?: "start" | "end"
  verticalAlign?: "top" | "bottom"
  /** Line-break policy for wrapping. */
  breakMode?: SceneBreakMode
}

export interface SceneRectStyle {
  border?: number
  radius?: number
}

/** A scene element as it arrives from the miniapp SDK (pre-processing). */
export type SceneElementInput =
  | {type: "text"; id?: string; box: SceneBox; text: string; style?: SceneTextStyle}
  | {type: "image"; id?: string; box: SceneBox; data: string}
  | {type: "rect"; id?: string; box: SceneBox; style?: SceneRectStyle}

export type SceneElementType = SceneElementInput["type"]

export type SceneChange = "created" | "updated" | "moved" | "unchanged"

/**
 * A processed element inside a SceneFrame: id definitely assigned, box clamped,
 * text pre-wrapped (newlines inserted), annotated with what changed vs the last
 * frame sent to this device.
 */
export interface FrameElement {
  id: string
  type: SceneElementType
  box: SceneBox
  text?: string
  /** Image pixels, base64 (PNG documented; SGCs decode → re-encode to wire format and scale to box). */
  data?: string
  style?: SceneTextStyle | SceneRectStyle
  change: SceneChange
  /** Hash of the element's content (text/data + style) — lets SGCs skip BLE re-upload without retaining pixels. */
  contentHash: string
}

/** The unit that crosses the JS→native bridge: one whole frame per render(). */
export interface SceneFrame {
  appId: string
  view: "main" | "dashboard"
  /** Bumps on replay / app switch / reconnect; a bumped epoch bypasses native dedup. */
  sceneEpoch: number
  /** True when the device is expected to rebuild from scratch (all elements arrive "created"). */
  replay?: boolean
  elements: FrameElement[]
  /** Element ids present in the previous frame but absent now — host-computed; apps never send removes. */
  removed: string[]
}

/**
 * Typed display capabilities block (host-side mirror of the SDK's
 * DisplayCapabilities). All limits are DATA — the pipeline acts on them
 * generically; no device names appear in pipeline code.
 */
export interface SceneDisplayCapabilities {
  /** Public drawable canvas in px (the device's safe area, not the physical panel). */
  width: number
  height: number
  /** False ⇒ scenes degrade to legacy text layouts host-side; only sugar-shaped content renders. */
  canPosition: boolean
  /** Text-element budget. Rects share this pool on container-based devices. */
  maxTextElements: number
  maxImageElements: number
  /** Max per-image dimensions the device accepts (box-level check), if limited. */
  maxImagePx?: {width: number; height: number}
  shapes: "rect"[]
  intensityLevels: number
  partialUpdate: boolean
  /** Keep G2-sized overflowing boxes (clamp to 576×288) instead of dropping images. */
  fitOverflowImages?: boolean
}

/** Result of processing a scene — feeds the awaitable render() result. */
export interface SceneProcessResult {
  frame: SceneFrame
  degraded: boolean
  /** Ids (or synthesized ids) of elements dropped by budget/bounds/image limits. */
  dropped: string[]
}

/** Cheap stable string hash (FNV-1a, 32-bit) — content identity for diff + native dedup. */
export function contentHash(...parts: (string | number | undefined)[]): string {
  let h = 0x811c9dc5
  for (const part of parts) {
    const s = part === undefined ? "\u0000" : String(part)
    for (let i = 0; i < s.length; i++) {
      h ^= s.charCodeAt(i)
      h = Math.imul(h, 0x01000193)
    }
    h ^= 0x1f
    h = Math.imul(h, 0x01000193)
  }
  return (h >>> 0).toString(36)
}

export function boxesEqual(a: SceneBox, b: SceneBox): boolean {
  return a.x === b.x && a.y === b.y && a.w === b.w && a.h === b.h
}

/** Content identity of an element (text/data + style), excluding geometry. */
export function elementContentHash(el: {
  type: SceneElementType
  text?: string
  data?: string
  style?: SceneTextStyle | SceneRectStyle
}): string {
  const style = el.style ?? {}
  const styleKey = Object.keys(style)
    .sort()
    .map((k) => `${k}=${(style as Record<string, unknown>)[k]}`)
    .join(",")
  return contentHash(el.type, el.text, el.data, styleKey)
}

/** Host-known render feedback. Offsets use JavaScript UTF-16 indices. */
export interface SceneTextLine {
  text: string
  start: number
  end: number
}

export interface SceneTextLayout {
  lines: SceneTextLine[]
  lineStarts: number[]
  capacity: number
  truncated: boolean
}
