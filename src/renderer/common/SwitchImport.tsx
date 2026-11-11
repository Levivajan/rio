// The switch from another browser, shared by the first-run screen and Settings, General: one checkbox per
// item, one "Bring everything over" button, progress while it runs, then a summary with counts and the extensions
// to reinstall from the Chrome Web Store. Spaces and profiles always come over; everything else is a checkbox.
import { useState } from 'react'
import type { SidebarImportResult } from '../../shared/ipc/data'
import { isSidebarResult, sidebarSummary } from '../../shared/import-summary'
import type { CluState } from '../../shared/state'
import { clu } from './clu'
import { useCluState } from './useCluState'
import './switch-import.css'

export type SwitchItem = 'pinned' | 'favorites' | 'today' | 'history' | 'bookmarks' | 'passwords' | 'cookies' | 'searchEngine' | 'extensions' | 'autofill'

/** Every row of the list, in the order it shows; `fixed` rows always come over. */
export const SWITCH_ROWS: readonly { id: SwitchItem | 'spaces' | 'profiles'; label: string; fixed?: true }[] = [
  { id: 'spaces', label: 'Spaces, with their names, icons and theme colors', fixed: true },
  { id: 'profiles', label: 'Profiles', fixed: true },
  { id: 'pinned', label: 'Pinned tabs and folders' },
  { id: 'favorites', label: 'Favorites' },
  { id: 'today', label: 'Today tabs' },
  { id: 'bookmarks', label: 'Bookmarks' },
  { id: 'history', label: 'History' },
  { id: 'passwords', label: 'Saved passwords' },
  { id: 'cookies', label: 'Sign-ins (cookies, site storage, permissions), so sites stay logged in' },
  { id: 'autofill', label: 'Saved addresses' },
  { id: 'searchEngine', label: 'Default search engine' },
  { id: 'extensions', label: 'Extensions, to reinstall from the Chrome Web Store' }
]

export const ALL_ITEMS: Record<SwitchItem, boolean> = {
  pinned: true,
  favorites: true,
  today: true,
  history: true,
  bookmarks: true,
  passwords: true,
  cookies: true,
  searchEngine: true,
  extensions: true,
  autofill: true
}

/** The progress line for the import state field, or null when no import runs. */
export function progressText(p: CluState['app']['import']): string | null {
  if (!p) return null
  // Both imports bring the saved passwords and sign-ins last (for Google Chrome).
  if (p.phase === 'keychain') return 'Waiting for keychain access… macOS asks you to allow it.'
  if (p.phase === 'secrets') return 'Importing passwords and sign-ins…'
  if (p.phase === 'site data') return 'Bringing over site data…'
  if (p.source === 'arc') {
    if (p.phase === 'history') return 'Importing history…'
    if (p.phase === 'extras') return 'Bringing over bookmarks, the search engine and extensions…'
    return p.phase === 'writing' ? 'Adding spaces and tabs…' : 'Reading the sidebar file…'
  }
  const what = p.phase === 'history' ? 'history' : 'bookmarks'
  return p.total > 0 ? `Importing ${what}… ${p.done.toLocaleString('en-US')} of ${p.total.toLocaleString('en-US')}` : `Reading ${what}…`
}

const START_ERRORS: Record<string, string> = { busy: 'An import is already running.' }

/** The checklist's state and the run. `previous` is detect's count of profiles an earlier import made. */
export function useSwitchImport(previous = 0) {
  const [items, setItems] = useState(ALL_ITEMS)
  const [running, setRunning] = useState(false)
  const [result, setResult] = useState<{ value: SidebarImportResult | null; text: string; error: boolean } | null>(null)
  // With an earlier import the run asks first, then replaces it. An import here counts as the earlier one.
  const [made, setMade] = useState(0)
  const [replacing, setReplacing] = useState<number | null>(null)
  const progress = useCluState((s) => (s.app.import?.source === 'arc' ? progressText(s.app.import) : null))
  const earlier = made > 0 ? made : previous

  const run = async (replace = false) => {
    if (!replace && earlier > 0) {
      setReplacing(earlier)
      return
    }
    setReplacing(null)
    setRunning(true)
    setResult(null)
    try {
      const r = await clu().import.run({ source: 'arc', ...items, ...(replace ? { replace: true } : {}) })
      if (r.ok && isSidebarResult(r.value)) {
        setResult({ value: r.value, text: sidebarSummary(r.value), error: r.value.error !== null })
        if (r.value.profiles > 0) setMade(r.value.profiles)
      } else setResult({ value: null, text: START_ERRORS[r.ok ? '' : (r.error.reason ?? '')] ?? 'Rio couldn’t start the import.', error: true })
    } finally {
      setRunning(false)
    }
  }
  const toggle = (id: SwitchItem, on: boolean) => setItems((was) => ({ ...was, [id]: on }))
  const replace = () => void run(true)
  const keep = () => setReplacing(null)
  return { items, toggle, running, progress, result, run, replacing, replace, keep }
}

/** The question before a re-import: the earlier import's profiles, spaces and tabs go and come over fresh. */
export function SwitchReplaceAsk({ previous, replace, keep }: { previous: number; replace: () => void; keep: () => void }) {
  const what = previous === 1 ? '1 profile' : `${previous.toLocaleString('en-US')} profiles`
  return (
    <div className="clu-switch-replace" role="alertdialog" aria-label="Replace the previous import?">
      <p className="clu-switch-status">
        You imported before. Replace it? The {what} it made, with their spaces and tabs, are removed and everything comes over again.
      </p>
      <div className="clu-switch-replace-actions">
        <button type="button" className="clu-switch-get" onClick={keep}>
          Cancel
        </button>
        <button type="button" className="clu-switch-get" onClick={replace}>
          Replace
        </button>
      </div>
    </div>
  )
}

export function SwitchChecklist({ items, toggle, disabled }: { items: Record<SwitchItem, boolean>; toggle: (id: SwitchItem, on: boolean) => void; disabled: boolean }) {
  return (
    <fieldset className="clu-switch-list" disabled={disabled}>
      <legend className="clu-switch-legend">What comes over</legend>
      {SWITCH_ROWS.map((row) => (
        <label key={row.id} className="clu-switch-item">
          <input
            type="checkbox"
            checked={row.fixed ? true : items[row.id as SwitchItem]}
            disabled={row.fixed}
            onChange={row.fixed ? undefined : (e) => toggle(row.id as SwitchItem, e.currentTarget.checked)}
          />
          <span>{row.label}</span>
        </label>
      ))}
      {items.passwords || items.cookies ? <p className="clu-switch-note">macOS asks you once to allow Rio to read the other browser’s keychain key.</p> : null}
    </fieldset>
  )
}

/** A Reinstall row's state: `failed` offers the button again as Try again. */
export type ReinstallState = 'idle' | 'installing' | 'installed' | 'failed'

/** Each extension's row state and the click: Chromium's Web Store install prompt over the window; the row
 * turns to Installed when it lands, back to Reinstall when the user says no, Try again when the store fails. */
export function useReinstall() {
  const [rows, setRows] = useState<Record<string, ReinstallState>>({})
  const reinstall = async (id: string) => {
    setRows((r) => ({ ...r, [id]: 'installing' }))
    const res = await clu()
      .extensions.installFromStore({ id })
      .catch(() => null)
    const next: ReinstallState = res?.ok ? 'installed' : res?.error.reason === 'cancelled' ? 'idle' : 'failed'
    setRows((r) => ({ ...r, [id]: next }))
  }
  return { rows, reinstall }
}

/** The summary with counts, then each extension with its Reinstall button. */
export function SwitchSummary({ result }: { result: { value: SidebarImportResult | null; text: string; error: boolean } }) {
  const extensions = result.value?.extensions ?? []
  const { rows, reinstall } = useReinstall()
  return (
    <div className="clu-switch-summary">
      <p className={result.error ? 'clu-switch-status clu-switch-error' : 'clu-switch-status'} role="status">
        {result.text}
      </p>
      {extensions.length > 0 ? (
        <ul className="clu-switch-extensions" aria-label="Extensions to reinstall">
          {extensions.map((e) => {
            const row = rows[e.id] ?? 'idle'
            return (
              <li key={e.id}>
                <span>{e.name}</span>
                {row === 'installed' ? (
                  <span className="clu-switch-done">Installed</span>
                ) : (
                  <button type="button" className="clu-switch-get" disabled={row === 'installing'} onClick={() => void reinstall(e.id)}>
                    {row === 'installing' ? 'Installing…' : row === 'failed' ? 'Try again' : 'Reinstall'}
                  </button>
                )}
              </li>
            )
          })}
        </ul>
      ) : null}
    </div>
  )
}
