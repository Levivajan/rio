// Profiles (docs/chromium/parity-settings.md sections 2 and 3): the
// profile list on the left, full height, each with how many spaces use it and a bar of +, − and edit under it; on the
// right the picked profile's search engine with Search Settings…, its archive time and download location, then "Your
// data and settings", which opens Chromium's own pages for that data and Rio's Clear browsing data dialog.
import { useRef, useState, type ReactNode } from 'react'
import type { IpcResult } from '../../../../shared/ipc'
import type { BROWSER_SETTINGS_PAGES } from '../../../../shared/ipc/data'
import { normalizeName } from '../../../../shared/spaces'
import type { CluState } from '../../../../shared/state'
import { clu } from '../../../common/clu'
import { Button, Card, Input, Row } from '../../../common/controls'
import { useCluState } from '../../../common/useCluState'
import { profileDeleteBlock } from '../../../overlay/spaces/editorModel'
import { SettingSelect } from '../../../settings-sections/data/parts/fields'
import { useSettingsProfile } from '../../../settings-sections/data/parts/profile'
import { Heading, Intro } from './parts'

const ENGINES = [
  { value: 'google', label: 'Google' },
  { value: 'duckduckgo', label: 'DuckDuckGo' },
  { value: 'bing', label: 'Bing' },
  { value: 'brave', label: 'Brave Search' },
  { value: 'ecosia', label: 'Ecosia' }
] as const
const AFTER = [
  { value: 12, label: '12 hours (default)' },
  { value: 24, label: '24 hours' },
  { value: 168, label: '7 days' },
  { value: 720, label: '30 days' }
] as const

/** How many saved spaces each profile has: "1 Space", "3 Spaces" or "Not used". */
export function spaceCounts(s: CluState): Record<string, string> {
  const n: Record<string, number> = {}
  for (const sp of Object.values(s.spaces)) if (sp && !sp.ephemeral && typeof sp.profileId === 'string') n[sp.profileId] = (n[sp.profileId] ?? 0) + 1
  return Object.fromEntries(Object.keys(s.profiles).map((id) => [id, n[id] ? `${n[id]} ${n[id] === 1 ? 'Space' : 'Spaces'}` : 'Not used']))
}
const sameCounts = (a: Record<string, string>, b: Record<string, string>) => JSON.stringify(a) === JSON.stringify(b)

type Page = (typeof BROWSER_SETTINGS_PAGES)[number]
interface DataLink { label: string; tint: string; glyph: ReactNode; open: (profileId: string | null) => void }

const glyph = (d: ReactNode) => (
  <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="#fff" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    {d}
  </svg>
)
const browser = (page: Page) => () => void clu().settings.openBrowser({ page })

const DATA_LINKS: DataLink[] = [
  { label: 'Privacy and Security', tint: 'blue', glyph: glyph(<><rect x="3.5" y="7" width="9" height="6.5" rx="1.5" /><path d="M5.5 7V5.2a2.5 2.5 0 0 1 5 0V7" /></>), open: browser('privacy') },
  { label: 'Passwords', tint: 'green', glyph: glyph(<><circle cx="8" cy="5.6" r="2.6" /><path d="M8 8.2v5.3M8 11h1.8" /></>), open: browser('passwords') },
  { label: 'Credit Cards', tint: 'yellow', glyph: glyph(<><rect x="2.5" y="4" width="11" height="8" rx="1.5" /><path d="M2.5 6.8h11" /></>), open: browser('payments') },
  { label: 'Notifications', tint: 'orange', glyph: glyph(<path d="M4.5 11V7.4a3.5 3.5 0 0 1 7 0V11l1 1.2h-9zM6.8 13.6h2.4" />), open: browser('notifications') },
  {
    label: 'Clear Browsing Data',
    tint: 'red',
    glyph: glyph(<path d="M3.5 4.5h9M6.5 4.5V3h3v1.5M4.8 4.5l.7 8.5h5l.7-8.5" />),
    open: (profileId) => void clu().browsingData.open(profileId ? { profileId } : {})
  }
]

const failure = (r: Extract<IpcResult<unknown>, { ok: false }>) =>
  r.error.reason === 'max-profiles' ? 'Rio holds at most 20 profiles. Delete one to add another.' : 'The profile could not be changed.'

const barIcon = (d: ReactNode) => (
  <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    {d}
  </svg>
)

export function ProfilesPane() {
  const { options, profileId, choose } = useSettingsProfile()
  const counts = useCluState(spaceCounts, sameCounts)
  const block = useCluState((s) => (profileId ? profileDeleteBlock(s, profileId) : 'No profile is picked.'))
  const [renaming, setRenaming] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const picked = options.find((o) => o.id === profileId)
  const add = async () => {
    const r = await clu().profiles.create({ name: 'New Profile' })
    setError(r.ok ? null : failure(r))
    if (r.ok) choose(r.value.profileId)
  }
  const remove = async () => {
    if (!profileId || block) return
    const r = await clu().profiles.delete({ profileId })
    setError(r.ok ? null : failure(r))
  }
  // Enter commits and unmounts the field, which may blur it too: the ref keeps that to one write.
  const draft = useRef<string | null>(null)
  draft.current = renaming
  const rename = async () => {
    const clean = normalizeName(draft.current)
    draft.current = null
    setRenaming(null)
    if (!profileId || !clean || clean === picked?.name) return
    const r = await clu().profiles.rename({ profileId, name: clean })
    setError(r.ok ? null : failure(r))
  }
  return (
    <>
      <Intro>Profiles help keep your data separate across Spaces, like history, logins, cookies, and extensions. You can use any Profile across one or more Spaces.</Intro>
      <div className="clu-sw-profiles">
        <section className="clu-group clu-sw-profile-list" aria-labelledby="clu-sw-your-profiles">
          <h3 id="clu-sw-your-profiles">Your Profiles</h3>
          <ul role="listbox" aria-labelledby="clu-sw-your-profiles">
            {options.map((o) =>
              o.id === profileId && renaming !== null ? (
                <li key={o.id} role="option" aria-selected>
                  <Input
                    aria-label={`New name for ${o.name}`}
                    value={renaming}
                    maxLength={240}
                    autoFocus
                    onChange={(e) => setRenaming(e.target.value)}
                    onBlur={() => void rename()}
                    onKeyDown={(e) => {
                      if (e.key === 'Enter') void rename()
                      if (e.key === 'Escape') setRenaming(null)
                    }}
                  />
                </li>
              ) : (
              <li
                key={o.id}
                role="option"
                aria-selected={o.id === profileId}
                tabIndex={o.id === profileId ? 0 : -1}
                onClick={() => choose(o.id)}
                onKeyDown={(e) => {
                  const i = options.findIndex((p) => p.id === o.id)
                  const next = e.key === 'ArrowDown' ? options[i + 1] : e.key === 'ArrowUp' ? options[i - 1] : undefined
                  if (!next) return
                  e.preventDefault()
                  choose(next.id)
                  ;(e.currentTarget.parentElement?.children[i + (e.key === 'ArrowDown' ? 1 : -1)] as HTMLElement | undefined)?.focus()
                }}
              >
                <span className="clu-sw-profile-name">{o.name}</span>
                <span className="clu-sw-profile-count">{counts[o.id] ?? 'Not used'}</span>
              </li>
              )
            )}
          </ul>
          {error && (
            <p className="clu-settings-error clu-sw-profile-error" role="alert">
              {error}
            </p>
          )}
          <div className="clu-sw-profile-bar">
            <button type="button" aria-label="Add" onClick={() => void add()}>
              {barIcon(<path d="M8 3v10M3 8h10" />)}
            </button>
            <button type="button" aria-label="Remove" title={block ?? undefined} disabled={block !== null} onClick={() => void remove()}>
              {barIcon(<path d="M3 8h10" />)}
            </button>
            <button type="button" aria-label="Rename" disabled={!picked} onClick={() => picked && setRenaming(picked.name)}>
              {barIcon(<><rect x="1.5" y="5" width="11" height="6.5" rx="1.5" /><path d="M4 8.3h.01M6 8.3h.01M8 8.3h.01M10.5 6.5l4-4" /></>)}
            </button>
          </div>
        </section>
        <div className="clu-sw-profile-settings">
          <Card>
            <SettingSelect settingKey="search.engineId" scope={profileId} label="Search engine" choices={ENGINES} />
            <Row label="Manage search">
              <Button onClick={() => void clu().settings.openBrowser({ page: 'search' })}>Search Settings…</Button>
            </Row>
          </Card>
          <Card>
            <SettingSelect
              settingKey="tabs.archiveAfterHours"
              scope={profileId}
              label={
                <span className="clu-sw-info-label">
                  Archive tabs after
                  <span className="clu-sw-info" aria-hidden="true" title="Today tabs you haven’t opened for this long move to the archive. Pinned tabs and favorites stay.">
                    {barIcon(<><circle cx="8" cy="8" r="5.8" /><path d="M8 7.3v3.6M8 5.2h.01" /></>)}
                  </span>
                </span>
              }
              choices={AFTER}
            />
            <Row label="Download location">
              <Button onClick={() => void clu().settings.openBrowser({ page: 'downloads' })}>Downloads…</Button>
            </Row>
          </Card>
          <Heading>Your Data and Settings</Heading>
          <ul className="clu-group clu-sw-links" aria-label="Your data and settings">
            {DATA_LINKS.map((l) => (
              <li key={l.label}>
                <button type="button" className="clu-sw-link" onClick={() => l.open(profileId)}>
                  <span className="clu-sw-link-tile" data-tint={l.tint}>
                    {l.glyph}
                  </span>
                  <span className="clu-sw-link-label">{l.label}</span>
                  <svg className="clu-sw-link-arrow" width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
                    <path d="M2.5 8h11M9.5 4l4 4-4 4" />
                  </svg>
                </button>
              </li>
            ))}
          </ul>
        </div>
      </div>
    </>
  )
}
