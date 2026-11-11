// Copyright 2026 The Rio Authors. All rights reserved.
//
// The M11 look probes (src/shared/look/targets.ts) read inside a Clu WebUI: the WebUI harness evaluates this file in
// the frame and overlay documents (e2e/chromium-webui/look.webui.ts), clu_browsertests through content::EvalJs
// (clu_look_browsertest.cc), then call cluLookMeasure(probes). Running animations and transitions are finished
// first, so every box is the state's resting one. Boxes are in the document's viewport, which is the window: both
// WebUIs fill it. A pixel probe answers the window point to sample; the caller reads it from its screenshot.
/* global document, getComputedStyle */

globalThis.cluLookMeasure = (probes) => {
  for (const a of document.getAnimations()) {
    try {
      a.finish()
    } catch {
      // Infinite animations (the loading pulse, spinners) cannot finish; they never move a box.
    }
  }
  const ms = (list) =>
    Math.max(
      0,
      ...String(list)
        .split(',')
        .map((s) => (s.trim().endsWith('ms') ? parseFloat(s) : parseFloat(s) * 1000))
        .filter(Number.isFinite)
    )
  // One property's own transition when it has one (a row's hover fade, its press), else the longest.
  const fadeMs = (cs, names = ['background-color', 'background']) => {
    const props = cs.transitionProperty.split(',').map((s) => s.trim())
    const durations = cs.transitionDuration.split(',')
    const i = props.findIndex((p) => names.includes(p))
    const own = i >= 0 ? ms(durations[i % durations.length]) : props.includes('all') ? ms(durations[props.indexOf('all') % durations.length]) : ms(cs.transitionDuration)
    return Math.max(own, cs.animationName === 'none' ? 0 : ms(cs.animationDuration))
  }
  const scaleOf = (cs) => {
    if (cs.scale && cs.scale !== 'none') return parseFloat(cs.scale)
    const m = /^matrix\(([^,]+),/.exec(cs.transform)
    return m ? parseFloat(m[1]) : 1
  }
  const out = {}
  for (const { id, probe } of probes) {
    const el = document.querySelector(probe.sel)
    if (!el) {
      out[id] = null
      continue
    }
    const r = el.getBoundingClientRect()
    const cs = getComputedStyle(el)
    if ('box' in probe) {
      const v = {
        x: r.left,
        y: r.top,
        w: r.width,
        h: r.height,
        cx: r.left + r.width / 2,
        cy: r.top + r.height / 2,
        fromRight: document.documentElement.clientWidth - r.right,
        fromBottom: document.documentElement.clientHeight - r.bottom
      }[probe.box]
      out[id] = Number.isFinite(v) ? v : null
    } else if ('css' in probe) {
      out[id] = {
        radius: parseFloat(cs.borderTopLeftRadius),
        'font-size': parseFloat(cs.fontSize),
        'font-weight': Number(cs.fontWeight),
        'transition-ms': fadeMs(cs),
        'transform-ms': fadeMs(cs, ['transform', 'scale']),
        scale: scaleOf(cs)
      }[probe.css]
    } else if ('pixel' in probe) {
      const [dx, dy] = probe.pixel
      out[id] = { at: [dx < 0 ? r.right + dx : r.left + dx, dy < 0 ? r.bottom + dy : r.top + dy] }
    } else {
      const other = document.querySelector(probe.gapTo)
      const o = other ? other.getBoundingClientRect() : null
      out[id] = o ? (probe.axis === 'x' ? o.left - r.right : o.top - r.bottom) : null
    }
  }
  return out
}
