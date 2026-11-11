// No-code Site tweak controls (tweaks-t3): hue, invert, contrast, brightness and saturation, then the
// font and text size. Each change goes straight to onChange; the editor previews it in the page and saves on Save.
import { useId, type CSSProperties } from 'react'
import { DEFAULT_TWEAK_CONTROLS, isTweakFont, TWEAK_FONTS, TWEAK_RANGES, type TweakControls } from '../../../shared/tweaks'
import { Select, Toggle } from '../../common/controls'

const percent = (v: number) => `${Math.round(v * 100)}%`
export const hueText = (v: number) => (v === 0 ? '0°' : `${v > 0 ? '+' : '−'}${Math.abs(Math.round(v))}°`)

type NumKey = 'hue' | 'contrast' | 'brightness' | 'saturation' | 'fontScale'

function Slider({ k, label, value, step, text, onChange }: { k: NumKey; label: string; value: number; step: number; text: string; onChange: (v: number) => void }) {
  const id = useId()
  const { min, max } = TWEAK_RANGES[k]
  return (
    <div className="clu-style-slider" data-control={k}>
      <label htmlFor={id}>{label}</label>
      <output htmlFor={id} aria-hidden="true">
        {text}
      </output>
      <input
        id={id}
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        aria-valuetext={text}
        style={{ '--fill': `${((value - min) / (max - min)) * 100}%` } as CSSProperties}
        onChange={(e) => onChange(Number(e.target.value))}
      />
    </div>
  )
}

const COLOR_KEYS = ['hue', 'invert', 'contrast', 'brightness', 'saturation'] as const
const TEXT_KEYS = ['fontFamily', 'fontScale'] as const

export function StylePanel({ value, onChange }: { value: TweakControls; onChange: (next: TweakControls) => void }) {
  const id = useId()
  const set = <K extends keyof TweakControls>(k: K, v: TweakControls[K]) => onChange({ ...value, [k]: v })
  const reset = (keys: readonly (keyof TweakControls)[]) => onChange({ ...value, ...Object.fromEntries(keys.map((k) => [k, DEFAULT_TWEAK_CONTROLS[k]])) })
  const changed = (keys: readonly (keyof TweakControls)[]) => keys.some((k) => value[k] !== DEFAULT_TWEAK_CONTROLS[k])

  return (
    <div className="clu-style-panel">
      <section aria-labelledby={`${id}-colors`}>
        <div className="clu-style-head">
          <h3 id={`${id}-colors`}>Colors</h3>
          <button type="button" className="clu-link-button" disabled={!changed(COLOR_KEYS)} onClick={() => reset(COLOR_KEYS)} aria-label="Reset colors">
            Reset
          </button>
        </div>
        <Slider k="hue" label="Hue" value={value.hue} step={1} text={hueText(value.hue)} onChange={(v) => set('hue', v)} />
        <div className="clu-style-toggle">
          <span aria-hidden="true">Invert colors</span>
          <Toggle label="Invert colors" checked={value.invert} onChange={(v) => set('invert', v)} />
        </div>
        <Slider k="contrast" label="Contrast" value={value.contrast} step={0.05} text={percent(value.contrast)} onChange={(v) => set('contrast', v)} />
        <Slider k="brightness" label="Brightness" value={value.brightness} step={0.05} text={percent(value.brightness)} onChange={(v) => set('brightness', v)} />
        <Slider k="saturation" label="Saturation" value={value.saturation} step={0.05} text={percent(value.saturation)} onChange={(v) => set('saturation', v)} />
      </section>
      <section aria-labelledby={`${id}-text`}>
        <div className="clu-style-head">
          <h3 id={`${id}-text`}>Text</h3>
          <button type="button" className="clu-link-button" disabled={!changed(TEXT_KEYS)} onClick={() => reset(TEXT_KEYS)} aria-label="Reset text">
            Reset
          </button>
        </div>
        <div className="clu-style-select">
          <label htmlFor={`${id}-font`}>Font</label>
          <Select id={`${id}-font`} value={value.fontFamily ?? ''} onChange={(e) => set('fontFamily', isTweakFont(e.target.value) ? e.target.value : null)}>
            <option value="">Site’s own</option>
            {TWEAK_FONTS.map((f) => (
              <option key={f} value={f}>
                {f}
              </option>
            ))}
          </Select>
        </div>
        <Slider k="fontScale" label="Text size" value={value.fontScale} step={0.05} text={percent(value.fontScale)} onChange={(v) => set('fontScale', v)} />
      </section>
    </div>
  )
}
