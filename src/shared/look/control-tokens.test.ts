// The control and page tokens (src/renderer/common/tokens.css, controls.css; docs/chromium/ui-polish.md): each mode is
// one color family, controls share one height and hairline and draw no shadow, and no surface keeps its own control
// styles.
import { readdirSync, readFileSync, statSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { describe, expect, it } from 'vitest'

const RENDERER = resolve(__dirname, '../../renderer')
const TOKENS = readFileSync(join(RENDERER, 'common/tokens.css'), 'utf8')
const CONTROLS = readFileSync(join(RENDERER, 'common/controls.css'), 'utf8')

/** The custom properties one block of tokens.css sets: the light :root or the dark override. */
function tokenBlock(mode: 'light' | 'dark'): Map<string, string> {
  const selector = mode === 'light' ? ':root {' : ":root[data-theme='dark'] {"
  const start = TOKENS.indexOf(selector)
  const body = TOKENS.slice(start + selector.length, TOKENS.indexOf('\n}', start))
  return new Map([...body.matchAll(/(--[\w-]+):\s*([^;]+);/g)].map((m) => [m[1] as string, (m[2] as string).trim()]))
}

/** Chroma (0..1) of a #rrggbb color: how far it is from gray. A navy (#151c30) is 0.11. */
function chroma(hex: string): number {
  const [r, g, b] = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16)) as [number, number, number]
  return (Math.max(r, g, b) - Math.min(r, g, b)) / 255
}

function cssFiles(dir: string): string[] {
  return readdirSync(dir).flatMap((name) => {
    const p = join(dir, name)
    return statSync(p).isDirectory() ? cssFiles(p) : p.endsWith('.css') ? [p] : []
  })
}

describe('control tokens: one family per mode (never a navy pill on a gray card in a navy window)', () => {
  it('both modes set every control and page token', () => {
    const light = tokenBlock('light')
    const dark = tokenBlock('dark')
    for (const name of ['--control-h', '--control-r', '--group-r', '--hairline-w', '--fs-body', '--primary', '--danger', '--control-focus']) {
      expect(light.has(name), name).toBe(true)
    }
    for (const name of ['--window-bg', '--card-bg', '--card-border', '--hairline', '--control-bg', '--control-bg-hover', '--control-border', '--control-fill', '--control-fill-strong', '--segment-on']) {
      expect(light.has(name), `light ${name}`).toBe(true)
      expect(dark.has(name), `dark ${name}`).toBe(true)
    }
  })

  it('controls are 28 pt with a 7 pt radius inside a 10 pt card, and a 1 px hairline', () => {
    const light = tokenBlock('light')
    expect(light.get('--control-h')).toBe('28px')
    expect(light.get('--control-r')).toBe('7px')
    expect(light.get('--group-r')).toBe('10px')
    expect(light.get('--hairline-w')).toBe('1px')
  })

  it('each mode is one neutral gray: the window, the popover material and the command bar carry no navy', () => {
    const dark = tokenBlock('dark')
    for (const [mode, name] of [['dark', '--window-bg'], ['dark', '--popover-bg'], ['dark', '--cmd-bg'], ['dark', '--peek-button-bg'], ['light', '--window-bg'], ['light', '--card-bg'], ['light', '--control-bg']] as const) {
      const value = (mode === 'dark' ? dark : tokenBlock('light')).get(name) ?? ''
      expect(value, name).toMatch(/^#[0-9a-f]{6}$/)
      expect(chroma(value), `${name} ${value}`).toBeLessThan(0.04)
    }
    // Cards and controls are white at an alpha over the window, so they can only lighten the same gray.
    for (const name of ['--card-bg', '--control-bg', '--control-fill', '--card-border', '--control-border', '--hairline']) {
      expect(dark.get(name), name).toMatch(/^rgb\(255 255 255 \/ \d+%\)$/)
    }
  })

  it('no inline control draws a shadow, and no surface keeps its own button class', () => {
    for (const rule of ['.clu-btn {', '.clu-select > select {', '.clu-input {']) {
      const start = CONTROLS.indexOf(rule)
      expect(start, rule).toBeGreaterThanOrEqual(0)
      expect(CONTROLS.slice(start, CONTROLS.indexOf('}', start))).not.toMatch(/box-shadow/)
    }
    const offenders = cssFiles(RENDERER).filter((f) => /\.clu-(?:button|settings-select|settings-button|prompt-input|editor-input|tweak-switch)\b/.test(readFileSync(f, 'utf8')))
    expect(offenders).toEqual([])
  })
})
