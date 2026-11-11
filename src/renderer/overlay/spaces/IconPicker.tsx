// The space icon picker, shared by the edit sheet and the Create a Space panel: Emoji with the letter, the
// curated grid and a paste field, or Symbols with a search field.
import { useState, type ClipboardEvent } from 'react'
import { SPACE_COLOR_HEX, spaceLetter, type SpaceIcon } from '../../../shared/spaces'
import { Input } from '../../common/controls'
import { searchSymbols, SpaceSymbolGlyph, symbolLabel } from '../../common/spaceSymbols'
import { CURATED_EMOJI, emojiInput, sameIcon } from './editorModel'

function SymbolGrid({ icon, onPick }: { icon: SpaceIcon; onPick: (icon: SpaceIcon) => void }) {
  const [query, setQuery] = useState('')
  const symbols = searchSymbols(query)
  return (
    <>
      <Input className="clu-editor-symbol-search" aria-label="Search symbols" placeholder="Search" value={query} maxLength={32} spellCheck={false} onChange={(e) => setQuery(e.currentTarget.value)} />
      {symbols.length ? (
        <div className="clu-editor-icons" role="group" aria-label="Symbols">
          {symbols.map((sym) => (
            <button key={sym} type="button" className="clu-editor-icon clu-editor-symbol" aria-label={symbolLabel(sym)} aria-pressed={sameIcon(icon, { kind: 'symbol', value: sym })} onClick={() => onPick({ kind: 'symbol', value: sym })}>
              <SpaceSymbolGlyph symbol={sym} size={18} />
            </button>
          ))}
        </div>
      ) : (
        <span className="clu-editor-hint">No symbols match</span>
      )}
    </>
  )
}

export function IconPicker({ icon, name, color, onPick }: { icon: SpaceIcon; name: string; color: string; onPick: (icon: SpaceIcon) => void }) {
  const [text, setText] = useState('')
  const [error, setError] = useState<string | null>(null)
  // Emoji and Symbols, as two tabs over one grid; a space with a symbol opens on Symbols.
  const [tab, setTab] = useState<'emoji' | 'symbols'>(icon.kind === 'symbol' ? 'symbols' : 'emoji')
  const take = (raw: string) => {
    setText(raw)
    const r = emojiInput(raw)
    if (!r) return setError(null)
    if ('error' in r) return setError(r.error)
    setError(null)
    onPick(r.icon)
  }
  const letter = icon.kind === 'letter'
  const tabs = [
    { id: 'emoji', label: 'Emoji' },
    { id: 'symbols', label: 'Symbols' }
  ] as const
  return (
    <div className="clu-editor-section">
      <div className="clu-editor-icon-head">
        <span className="clu-editor-label" id="clu-editor-icon">
          Icon
        </span>
        <div className="clu-editor-icon-tabs" role="tablist" aria-labelledby="clu-editor-icon">
          {tabs.map((t) => (
            <button key={t.id} type="button" role="tab" id={`clu-editor-tab-${t.id}`} aria-selected={tab === t.id} aria-controls="clu-editor-icon-panel" onClick={() => setTab(t.id)}>
              {t.label}
            </button>
          ))}
        </div>
      </div>
      <div className="clu-editor-icon-panel" id="clu-editor-icon-panel" role="tabpanel" aria-labelledby={`clu-editor-tab-${tab}`}>
        {tab === 'symbols' ? (
          <SymbolGrid icon={icon} onPick={onPick} />
        ) : (
          <>
            <div className="clu-editor-icons" role="group" aria-label="Emoji">
              <button type="button" className="clu-editor-icon" aria-label="Letter" aria-pressed={letter} onClick={() => onPick({ kind: 'letter' })}>
                <span className="clu-editor-letter" style={{ background: SPACE_COLOR_HEX[color as keyof typeof SPACE_COLOR_HEX] ?? SPACE_COLOR_HEX.slate }}>
                  {spaceLetter(name)}
                </span>
              </button>
              {CURATED_EMOJI.map((e) => (
                <button key={e} type="button" className="clu-editor-icon" aria-label={e} aria-pressed={sameIcon(icon, { kind: 'emoji', value: e })} onClick={() => onPick({ kind: 'emoji', value: e })}>
                  {e}
                </button>
              ))}
            </div>
            <div className="clu-editor-paste">
              <Input
                className="clu-editor-emoji"
                aria-label="Emoji"
                aria-invalid={error ? true : undefined}
                aria-describedby={error ? 'clu-editor-emoji-error' : undefined}
                placeholder="Paste"
                value={text}
                maxLength={32}
                spellCheck={false}
                onChange={(e) => take(e.currentTarget.value)}
                onPaste={(e: ClipboardEvent<HTMLInputElement>) => {
                  e.preventDefault()
                  take(e.clipboardData.getData('text/plain'))
                }}
              />
              <span className={error ? 'clu-editor-error' : 'clu-editor-hint'} id="clu-editor-emoji-error" role={error ? 'alert' : undefined}>
                {error ?? 'Any emoji, typed or pasted'}
              </span>
            </div>
          </>
        )}
      </div>
    </div>
  )
}
