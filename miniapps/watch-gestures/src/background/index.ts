import {registerMiniapp} from "@mentra/miniapp/background"

const LABELS: Record<string, string> = {
  swipe_up: "swipe up",
  swipe_down: "swipe down",
  single_tap: "tap",
  double_tap: "double tap",
  long_press: "hold",
}

function labelOf(kind: string): string {
  return LABELS[kind] ?? kind
}

registerMiniapp((session) => {
  let lastKind = "waiting…"
  let count = 0

  const ui = session.ui as unknown as {
    send: (channel: "gestures:last", payload: {kind: string; count: number}) => void
    onOpen: (cb: () => void) => () => void
  }

  const paint = (kind: string) => {
    lastKind = kind
    const d = session.capabilities?.display
    const w = d?.width ?? 378
    void session.display.render([
      {type: "text", id: "title", box: {x: 16, y: 24, w: w - 32, h: 48}, text: "Watch Gestures"},
      {type: "text", id: "kind", box: {x: 16, y: 96, w: w - 32, h: 72}, text: labelOf(kind)},
      {
        type: "text",
        id: "hint",
        box: {x: 16, y: 180, w: w - 32, h: 40},
        text: count === 0 ? "swipe or tap" : `#${count}`,
      },
    ])
    ui.send("gestures:last", {kind, count})
  }

  paint("waiting…")

  ui.onOpen(() => {
    ui.send("gestures:last", {kind: lastKind, count})
  })

  session.input.onTouch((data) => {
    count += 1
    paint(data.kind)
  })
})
