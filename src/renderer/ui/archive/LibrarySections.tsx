// The Library's Site tweaks section and the Spaces glyph (the rail lists
// Media, Downloads, Spaces, Site tweaks and Archived Tabs; the Spaces cards are LibrarySpaces.tsx). Site
// tweaks lists this profile's tweaked sites; a click opens the site in a new tab, where Site controls edits its tweak.
import { useState, type SVGProps } from 'react'
import { tweakParts } from '../../../shared/tweaks'
import { LetterTile } from '../../common/LetterTile'
import { clu } from '../../common/clu'
import { useCluState, useWindowId } from '../../common/useCluState'
import { useSiteTweaks } from '../../settings-sections/tweaks/useSiteTweaks'

const glyph = { width: 22, height: 22, viewBox: '0 0 22 22', fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const
export const SpacesGlyph = (p: SVGProps<SVGSVGElement>) => (
  <svg {...glyph} {...p}>
    <rect x="7" y="3.5" width="11" height="14" rx="2.5" />
    <path d="M4.5 6.5v10a2.5 2.5 0 0 0 2.5 2.5h7.5" />
  </svg>
)
export const TweaksGlyph = (p: SVGProps<SVGSVGElement>) => (
  <svg {...glyph} {...p}>
    <path d="m4 18 9-9M11.5 5.5l5 5M13.5 3.5l5 5-2 2-5-5Z" />
    <path d="M5.5 4v3M4 5.5h3M17 15v3M15.5 16.5h3" />
  </svg>
)

const matches = (text: string, query: string) => !query.trim() || text.toLowerCase().includes(query.trim().toLowerCase())

function Search({ label, value, onChange }: { label: string; value: string; onChange: (v: string) => void }) {
  return (
    <div className="clu-library-head">
      <label className="clu-archive-search-field">
        <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" aria-hidden="true">
          <circle cx="7" cy="7" r="4.75" />
          <path d="m10.5 10.5 3.25 3.25" />
        </svg>
        <input
          className="clu-archive-search"
          type="search"
          aria-label={`Search ${label}`}
          placeholder={`Search ${label}…`}
          value={value}
          maxLength={200}
          spellCheck={false}
          autoFocus
          onChange={(e) => onChange(e.target.value)}
        />
      </label>
    </div>
  )
}

export function TweaksView() {
  const windowId = useWindowId() ?? ''
  const profileId = useCluState((s) => s.spaces[s.windows[windowId]?.activeSpaceId ?? '']?.profileId ?? '')
  const tweaks = useSiteTweaks(profileId)
  const [query, setQuery] = useState('')
  const rows = (tweaks.rows ?? []).filter((r) => typeof r.site === 'string' && r.site !== '' && matches(r.site, query))
  return (
    <section className="clu-archive clu-library-tweaks" aria-label="Site tweaks">
      <Search label="Site tweaks" value={query} onChange={setQuery} />
      <div className="clu-archive-list">
        <ul className="clu-archive-rows">
          {rows.map((r) => {
            const site = r.site.slice(0, 253)
            return (
              <li key={r.site} className="clu-archive-row" data-tweak-site={site} data-enabled={r.enabled}>
                <button type="button" className="clu-archive-open" onClick={() => void clu().tabs.create({ url: `https://${site}/`, activate: true })}>
                  <span className="clu-library-icon">
                    <LetterTile text={site} size={20} />
                  </span>
                  <span className="clu-archive-text">
                    <span className="clu-archive-title">{site}</span>
                    <span className="clu-archive-meta">{[r.enabled ? null : 'Off', ...tweakParts(r)].filter(Boolean).join(', ') || 'No changes'}</span>
                  </span>
                </button>
              </li>
            )
          })}
        </ul>
        {tweaks.rows !== null && rows.length === 0 ? <p className="clu-archive-empty">{query.trim() ? 'No matches' : 'No site tweaks yet'}</p> : null}
      </div>
    </section>
  )
}
