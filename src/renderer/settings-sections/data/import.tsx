// Import (the app settings' Advanced tab, and General at #import-chrome): pick a Chrome profile, what to bring over
// (bookmarks, history, saved passwords and sign-ins) and the space it lands in, then Import; or import the sidebar file from another browser (its profiles, spaces, pinned and Today tabs, favorites and
// history); or open Chromium's own password import from a CSV file. Detection
// runs when the section mounts and only checks what exists; progress comes from the `import` state field and the
// result shows inline (main also toasts it).
import { useEffect, useId, useRef, useState } from 'react'
import { importSummary, isSidebarResult, type ImportDetect } from '../../../shared/ipc/data'
import type { CluState } from '../../../shared/state'
import { useCluState } from '../../common/useCluState'
import { clu } from '../../common/clu'
import { Button, Card, Row, Select } from '../../common/controls'
import { progressText, SwitchChecklist, SwitchReplaceAsk, SwitchSummary, useSwitchImport } from '../../common/SwitchImport'

interface SpaceOption { id: string; name: string }

export function spaceOptions(s: CluState): SpaceOption[] {
  const order = new Map(s.spaceOrder.map((id, i) => [id, i]))
  return Object.values(s.spaces)
    .filter((sp) => sp && !sp.ephemeral && typeof sp.id === 'string')
    .sort((a, b) => (order.get(a.id) ?? Infinity) - (order.get(b.id) ?? Infinity) || a.id.localeCompare(b.id))
    .map((sp) => ({ id: sp.id, name: typeof sp.name === 'string' && sp.name.trim() ? sp.name : 'Untitled space' }))
}
const sameSpaces = (a: SpaceOption[], b: SpaceOption[]) => a.length === b.length && a.every((o, i) => o.id === b[i]?.id && o.name === b[i]?.name)

export { progressText }

/**
 * Keeps a finished import's result in view. The import adds profiles, which grows the sections above this one (their
 * profile pickers), and would push the result off screen.
 */
function useReveal<T extends HTMLElement>(shown: unknown) {
  const ref = useRef<T>(null)
  const profiles = useCluState((s) => Object.keys(s.profiles).length)
  useEffect(() => {
    if (shown) ref.current?.scrollIntoView?.({ block: 'nearest' })
  }, [shown, profiles])
  return ref
}

const START_ERRORS: Record<string, string> = {
  busy: 'An import is already running.',
  'private-space': 'Private spaces don’t keep imported data. Pick another space.'
}

/** import.detect once on mount: what it found, or 'failed'; null while it looks. */
function useImportDetect(): ImportDetect | 'failed' | null {
  const [found, setFound] = useState<ImportDetect | 'failed' | null>(null)
  useEffect(() => {
    let live = true
    void clu().import.detect().then((r) => {
      if (live) setFound(r.ok ? r.value : 'failed')
    })
    return () => {
      live = false
    }
  }, [])
  return found
}

function DetectFailed() {
  return (
    <Card>
      <Row label={<span className="clu-settings-error">Rio couldn’t look for other browsers.</span>} />
    </Card>
  )
}

export function ImportSection() {
  const found = useImportDetect()
  if (found === 'failed') return <DetectFailed />
  if (!found) return <Card aria-busy="true" />
  return (
    <>
      <ChromeImport found={found} />
      <SidebarImport found={found.arc.found} previous={found.arc.previous} />
      <PasswordImport />
    </>
  )
}

/** The command's and the section's name. */
export const IMPORT_CHROME = 'Import from Google Chrome'

/**
 * What Import from Google Chrome opens (the command bar, the File menu, onboarding's Import step load the
 * settings at #import-chrome): the Chrome import itself, first on the page and scrolled to. It brings the
 * saved passwords and sign-ins too; Chromium's own CSV import is the card under it.
 */
export function ChromeImportSection() {
  const found = useImportDetect()
  const box = useRef<HTMLElement>(null)
  useEffect(() => {
    if (found) box.current?.scrollIntoView?.({ block: 'start' })
  }, [found])
  return (
    <section ref={box} aria-label={IMPORT_CHROME} data-import-chrome="">
      <h2 className="clu-sw-heading">{IMPORT_CHROME}</h2>
      {found === 'failed' ? (
        <DetectFailed />
      ) : !found ? (
        <Card aria-busy="true" />
      ) : (
        <>
          <ChromeImport found={found} />
          <PasswordImport />
        </>
      )}
    </section>
  )
}

function ChromeImport({ found }: { found: ImportDetect }) {
  const [dir, setDir] = useState<string | null>(null)
  const [wantBookmarks, setWantBookmarks] = useState(true)
  const [wantHistory, setWantHistory] = useState(true)
  const [passwords, setPasswords] = useState(true)
  const [cookies, setCookies] = useState(true)
  const [autofill, setAutofill] = useState(true)
  const [searchEngine, setSearchEngine] = useState(true)
  const [spaceId, setSpaceId] = useState<string | null>(null)
  const [running, setRunning] = useState(false)
  const [result, setResult] = useState<{ text: string; error: boolean } | null>(null)
  const spaces = useCluState(spaceOptions, sameSpaces)
  const progress = useCluState((s) => progressText(s.app.import))
  const id = useId()
  const status = useReveal<HTMLParagraphElement>(result)

  const profiles = found.chrome.profiles
  if (profiles.length === 0) {
    return (
      <Card role="group" aria-label="Google Chrome">
        <Row label="Google Chrome" hint="Google Chrome isn’t installed on this Mac, or has no profiles to import." hintRole="status" />
      </Card>
    )
  }

  const profile = profiles.find((p) => p.dir === dir) ?? profiles[0]
  const target = spaces.find((s) => s.id === spaceId) ?? spaces.find((s) => s.id === found.currentSpaceId) ?? spaces[0]
  const bookmarks = wantBookmarks && !!profile?.hasBookmarks
  const history = wantHistory && !!profile?.hasHistory
  const dupNames = new Set(profiles.map((p) => p.name).filter((n, i, all) => all.indexOf(n) !== i))
  const canRun = !!profile && !!target && (bookmarks || history || passwords || cookies || autofill || searchEngine) && !running

  const run = async () => {
    if (!profile || !target) return
    setRunning(true)
    setResult(null)
    try {
      const r = await clu().import.run({ source: 'chrome', profileDir: profile.dir, bookmarks, history, passwords, cookies, autofill, searchEngine, targetSpaceId: target.id })
      if (r.ok && !isSidebarResult(r.value)) setResult({ text: importSummary(r.value), error: r.value.error !== null })
      else setResult({ text: START_ERRORS[r.ok ? '' : (r.error.reason ?? '')] ?? 'Rio couldn’t start the import.', error: true })
    } finally {
      setRunning(false)
    }
  }

  return (
    <Card role="group" aria-label="Google Chrome">
      <Row
        label="Google Chrome profile"
        htmlFor={`${id}-profile`}
        hint="Bookmarks become pinned tabs in an “Imported from Google Chrome” folder. Saved passwords and sign-ins need Google Chrome’s keychain key, which macOS asks you to allow. Google Chrome’s own files are only read."
        hintId={`${id}-profile-hint`}
      >
        <Select
          id={`${id}-profile`}
          value={profile?.dir ?? ''}
          aria-describedby={`${id}-profile-hint`}
          disabled={running}
          onChange={(e) => setDir(e.currentTarget.value)}
        >
          {profiles.map((p) => (
            <option key={p.dir} value={p.dir}>
              {dupNames.has(p.name) ? `${p.name} (${p.dir})` : p.name}
            </option>
          ))}
        </Select>
      </Row>
      <fieldset className="clu-group-row" disabled={running}>
        <legend className="clu-group-row-label">What to import</legend>
        <div className="clu-settings-checks">
          <label className="clu-settings-check">
            <input type="checkbox" checked={bookmarks} disabled={!profile?.hasBookmarks} onChange={(e) => setWantBookmarks(e.currentTarget.checked)} />
            <span>Bookmarks</span>
          </label>
          <label className="clu-settings-check">
            <input type="checkbox" checked={history} disabled={!profile?.hasHistory} onChange={(e) => setWantHistory(e.currentTarget.checked)} />
            <span>History (last 90 days)</span>
          </label>
          <label className="clu-settings-check">
            <input type="checkbox" checked={passwords} onChange={(e) => setPasswords(e.currentTarget.checked)} />
            <span>Saved passwords</span>
          </label>
          <label className="clu-settings-check">
            <input type="checkbox" checked={cookies} onChange={(e) => setCookies(e.currentTarget.checked)} />
            <span>Sign-ins (cookies, site storage, permissions), so sites stay signed in</span>
          </label>
          <label className="clu-settings-check">
            <input type="checkbox" checked={autofill} onChange={(e) => setAutofill(e.currentTarget.checked)} />
            <span>Saved addresses</span>
          </label>
          <label className="clu-settings-check">
            <input type="checkbox" checked={searchEngine} onChange={(e) => setSearchEngine(e.currentTarget.checked)} />
            <span>Default search engine</span>
          </label>
        </div>
      </fieldset>
      <Row label="Into space" htmlFor={`${id}-space`}>
        <Select id={`${id}-space`} value={target?.id ?? ''} disabled={running || spaces.length === 0} onChange={(e) => setSpaceId(e.currentTarget.value)}>
          {spaces.map((s) => (
            <option key={s.id} value={s.id}>
              {s.name}
            </option>
          ))}
        </Select>
      </Row>
      <div className="clu-group-row">
        <p ref={status} className={result?.error ? 'clu-settings-status clu-settings-error' : 'clu-settings-status'} role="status">
          {running ? (progress ?? 'Starting…') : (result?.text ?? '')}
        </p>
        <Button variant="primary" disabled={!canRun} onClick={() => void run()}>
          {running ? 'Importing…' : 'Import'}
        </Button>
      </div>
    </Card>
  )
}

/**
 * The sidebar file from another browser: what comes over, one button; the files are only read, and the result says what
 * isn't brought over. Passwords and sign-ins (cookies) need the other browser's keychain key, which macOS asks the user
 * to allow.
 */
function SidebarImport({ found, previous = 0, title = 'Sidebar file from another browser' }: { found: boolean; previous?: number; title?: string }) {
  const { items, toggle, running, progress, result, run, replacing, replace, keep } = useSwitchImport(previous)
  const id = useId()
  const outcome = useReveal<HTMLDivElement>(result)
  // The question sits under the button and is scrolled to, so Cancel and Replace show without scrolling.
  const ask = useReveal<HTMLDivElement>(replacing)

  return (
    <Card role="group" aria-labelledby={`${id}-title`} data-switch-import="">
      <Row
        label={title}
        labelId={`${id}-title`}
        hint={
          found
            ? 'Adds a profile for each profile in the file, with what you tick below. The other browser’s files are only read.'
            : 'No sidebar file from another browser was found on this Mac.'
        }
        hintRole={found ? undefined : 'status'}
      >
        {found ? (
          <Button disabled={running || replacing !== null} onClick={() => void run()}>
            {running ? 'Importing…' : 'Bring everything over'}
          </Button>
        ) : null}
      </Row>
      {found && replacing !== null ? (
        <div ref={ask} className="clu-group-row">
          <SwitchReplaceAsk previous={replacing} replace={replace} keep={keep} />
        </div>
      ) : null}
      {found ? (
        <div className="clu-group-row">
          <SwitchChecklist items={items} toggle={toggle} disabled={running} />
        </div>
      ) : null}
      {found && running ? (
        <div ref={outcome} className="clu-group-row">
          <p className="clu-settings-status" role="status">
            {progress ?? 'Starting…'}
          </p>
        </div>
      ) : null}
      {found && !running && result ? (
        <div ref={outcome} className="clu-group-row">
          <SwitchSummary result={result} />
        </div>
      ) : null}
    </Card>
  )
}

/** The command's and the card's name. */
export const IMPORT_ARC = 'Import from Arc'

/**
 * For a Rio past its first run: Settings, General, one row that opens the first run's switch in place (the
 * checklist, "Bring everything over", progress, the summary with counts). `open` opens it from outside (the command
 * bar's Import from Arc loads the settings at #import-arc). Collapsed, the pane keeps its two short cards' height.
 */
export function ArcImport({ open: opened }: { open: boolean }) {
  const [open, setOpen] = useState(opened)
  const [found, setFound] = useState<boolean | null>(null)
  const [previous, setPrevious] = useState(0)
  const box = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (opened) setOpen(true)
  }, [opened])
  useEffect(() => {
    if (!open) return
    let live = true
    void clu().import.detect().then((r) => {
      if (!live) return
      setFound(r.ok && r.value.arc.found)
      setPrevious(r.ok ? r.value.arc.previous : 0)
    })
    return () => {
      live = false
    }
  }, [open])
  useEffect(() => {
    if (open && found !== null) box.current?.scrollIntoView?.({ block: 'nearest' })
  }, [open, found])

  if (open && found !== null) {
    return (
      <div ref={box}>
        <SidebarImport found={found} previous={previous} title={IMPORT_ARC} />
      </div>
    )
  }
  return (
    <Card role="group" aria-label={IMPORT_ARC}>
      <Row label={IMPORT_ARC} hint="Spaces, pinned tabs, favorites, passwords and sign-ins. The other browser’s files are only read.">
        <Button disabled={open} onClick={() => setOpen(true)}>
          {open ? 'Looking…' : 'Import…'}
        </Button>
      </Row>
    </Card>
  )
}

/** Chromium's own password import from a CSV file: its page opens in this window's space and asks for the file. */
function PasswordImport() {
  const [state, setState] = useState<'idle' | 'opening' | 'failed'>('idle')
  const id = useId()

  const open = async () => {
    setState('opening')
    const r = await clu().import.openPasswords()
    setState(r.ok ? 'idle' : 'failed')
  }

  return (
    <Card role="group" aria-labelledby={`${id}-title`}>
      <Row
        label="Import passwords from a CSV file"
        labelId={`${id}-title`}
        hint={state === 'failed' ? <span className="clu-settings-error">The password import didn’t open.</span> : undefined}
        hintRole={state === 'failed' ? 'status' : undefined}
      >
        <Button disabled={state === 'opening'} onClick={() => void open()}>
          {state === 'opening' ? 'Opening…' : 'Import'}
        </Button>
      </Row>
    </Card>
  )
}
