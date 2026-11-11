// Rows the app settings panes share: a switch bound to one global boolean setting, and a pane's intro line.
import { useId, type ReactNode } from 'react'
import { Row, Toggle } from '../../../common/controls'
import { useSetting } from '../../../settings-sections/data/parts/useSetting'

/** The design's colored icon tile before a switch's label: a 20 pt rounded square, its glyph white. */
export type SwitchTile = { tint: 'blue' | 'green' | 'red' | 'magenta'; glyph: ReactNode }

/**
 * A row with a macOS switch for a global boolean setting; the switch shows the saved value, or the default. `info` adds
 * the design's circled i after the label, its text as the tooltip; `tile` the colored icon tile before it.
 */
export function SwitchRow({ settingKey, label, note, info, tile }: { settingKey: string; label: ReactNode; note?: ReactNode; info?: string; tile?: SwitchTile }) {
  const { value, error, set } = useSetting(settingKey, 'global')
  const id = useId()
  const shown = info ? (
    <span className="clu-sw-info-label">
      {label}
      <span className="clu-sw-info" aria-hidden="true" title={info} data-info>
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
          <circle cx="8" cy="8" r="5.8" />
          <path d="M8 7.3v3.6M8 5.2h.01" />
        </svg>
      </span>
    </span>
  ) : label
  const withTile = tile ? (
    <span className="clu-sw-tile-label">
      <span className="clu-sw-tile" data-tint={tile.tint} aria-hidden="true">
        {tile.glyph}
      </span>
      {shown}
    </span>
  ) : shown
  return (
    <Row label={withTile} labelId={`${id}-label`} hint={error ?? note} hintId={`${id}-hint`} hintRole={error ? 'alert' : undefined}>
      <Toggle label={typeof label === 'string' ? label : settingKey} checked={value === true} onChange={(next) => set(next)} aria-describedby={error || note ? `${id}-hint` : undefined} />
    </Row>
  )
}

export function Intro({ children }: { children: ReactNode }) {
  return <p className="clu-sw-intro">{children}</p>
}

export function Heading({ children }: { children: ReactNode }) {
  return <h2 className="clu-sw-heading">{children}</h2>
}
