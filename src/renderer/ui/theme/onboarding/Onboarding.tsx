// First-run onboarding (docs/plan/01-shell.md F12, PLAN decisions 34 and 48): a full-window takeover in
// the UI webContents while app.onboarding.show is set. No page view is attached; the frame behind it is inert and the
// traffic lights stay visible. Steps: welcome, look, moves, default browser, import, done; each has Back and
// "Skip setup". "Start browsing" opens the command bar in new-tab mode; "Skip setup" leaves the empty state.
import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties, type ReactNode, type RefObject } from 'react'
import iconUrl from '../../../../../assets/brand/clu-icon.svg'
import { invoke } from '../../../common/invoke'
import { useCluState, useWindowId } from '../../../common/useCluState'
import { useReducedMotion } from '../../../common/useReducedMotion'
import { defaultThemeFor, themeToCss } from '../../../../shared/theme/space'
import { ThemeEditor } from '../../../overlay/theme/ThemePicker'
import { defaultBrowserOutcome, isSidebarChord, lookZoom, nextStep, prevStep, stepsFor, type Step } from './steps'
import './onboarding.css'
import { clu } from '../../../common/clu'
import { Button } from '../../../common/controls'
import { SwitchChecklist, SwitchReplaceAsk, SwitchSummary, useSwitchImport } from '../../../common/SwitchImport'

type HeadingRef = RefObject<HTMLHeadingElement | null>

/** How long the moves step shows the sidebar preview toggling before it moves on. */
const TOGGLE_HOLD_MS = 600
/** What the look step's card keeps beside the editor: its 16 pt padding each side, the 20 pt gap, a 160 pt title column. */
const LOOK_SIDE = 16 * 2 + 20 + 160

export function Onboarding() {
  const show = useCluState((s) => s.app.onboarding.show)
  return show ? <Takeover /> : null
}

function Takeover() {
  const [step, setStep] = useState<Step>('welcome')
  // When another browser's sidebar file is on this Mac, setup opens on the switch screen first.
  const [switching, setSwitching] = useState(false)
  // Once the switch screen showed, imported or declined, the Import step does not ask again.
  const [switched, setSwitched] = useState(false)
  const [previous, setPrevious] = useState(0)
  useEffect(() => {
    let live = true
    void clu()
      .import.detect()
      .then((r) => {
        if (!live || !r.ok || !r.value.arc.found) return
        setPrevious(r.value.arc.previous)
        setSwitching(true)
      })
    return () => {
      live = false
    }
  }, [])
  const [toggled, setToggled] = useState(false)
  const reduced = useReducedMotion()
  const resolved = useCluState((s) => s.app.appearance.resolved)
  // The look edits the space this window shows (the sidebar behind the takeover), the first one only when
  // the window has none yet.
  const windowId = useWindowId()
  const spaceId = useCluState((s) => {
    const active = windowId ? s.windows[windowId]?.activeSpaceId : undefined
    return active && s.spaces[active] ? active : (s.spaceOrder[0] ?? null)
  })
  const theme = useCluState((s) => (spaceId ? (s.spaces[spaceId]?.theme ?? null) : null))
  const color = useCluState((s) => (spaceId ? (s.spaces[spaceId]?.color ?? 'blue') : 'blue'))
  const heading = useRef<HTMLHeadingElement>(null)
  const background = themeToCss(theme ?? defaultThemeFor(color), resolved).background

  const go = (s: Step) => {
    setToggled(false)
    setStep(s)
  }

  // The sidebar and page card behind the takeover take no pointer, focus or screen reader input.
  useEffect(() => {
    const frame = document.querySelector('.clu-frame')
    frame?.setAttribute('inert', '')
    return () => frame?.removeAttribute('inert')
  }, [])

  useEffect(() => heading.current?.focus(), [step, switching])

  // Cmd+S never toggles the hidden sidebar from here; on the moves step it plays the preview and moves on.
  useEffect(() => {
    let timer: ReturnType<typeof setTimeout> | null = null
    const onKey = (e: KeyboardEvent) => {
      if (!isSidebarChord(e)) return
      e.preventDefault()
      if (step !== 'moves' || timer) return
      setToggled(true)
      timer = setTimeout(() => go(nextStep('moves')), reduced ? 0 : TOGGLE_HOLD_MS)
    }
    window.addEventListener('keydown', onKey, true)
    return () => {
      window.removeEventListener('keydown', onKey, true)
      if (timer) clearTimeout(timer)
    }
  }, [step, reduced])

  const skip = () => void clu().onboarding.complete({ skipped: true })
  const steps = stepsFor(switched)
  const next = () => go(nextStep(step, switched))
  const back = step === 'welcome' ? undefined : () => go(prevStep(step, switched))

  return (
    <div className="clu-ob" data-step={step} style={{ background }} role="dialog" aria-modal="true" aria-labelledby="clu-ob-title">
      <div className="clu-ob-top">
        {step === 'done' ? null : (
          <Button className="clu-ob-link" onClick={skip}>
            Skip setup
          </Button>
        )}
      </div>
      <div className="clu-ob-center">
        <section className="clu-ob-card">
          {switching ? (
            <Switch
              heading={heading}
              previous={previous}
              next={(done) => {
                setSwitching(false)
                setSwitched(done)
              }}
            />
          ) : step === 'welcome' ? (
            <Panel heading={heading} title="Welcome to Rio" icon actions={<Primary onClick={next}>Get started</Primary>}>
              <p>Your tabs live in the sidebar, and everything else is one keystroke away.</p>
            </Panel>
          ) : step === 'look' ? (
            <Panel heading={heading} title="Pick a look" back={back} actions={<Primary onClick={next}>Continue</Primary>}>
              <Look spaceId={spaceId} />
            </Panel>
          ) : step === 'moves' ? (
            <Panel heading={heading} title="Three moves to know" back={back} actions={<Primary onClick={next}>Continue</Primary>}>
              <Moves toggled={toggled} />
            </Panel>
          ) : step === 'default-browser' ? (
            <DefaultBrowser heading={heading} back={back} next={next} />
          ) : step === 'import' ? (
            <Import heading={heading} back={back} next={next} />
          ) : (
            <Panel heading={heading} title="You're all set" back={back} actions={<Primary onClick={() => void start()}>Start browsing</Primary>}>
              <p>Type a site or a search in the command bar. Press ⌘T whenever you want a new page.</p>
            </Panel>
          )}
        </section>
        <ol className="clu-ob-dots" aria-label={`Step ${steps.indexOf(step) + 1} of ${steps.length}`}>
          {steps.map((s) => (
            <li key={s} data-current={s === step} />
          ))}
        </ol>
      </div>
    </div>
  )
}

/** "Start browsing": hide onboarding, make sure the sidebar shows, then the command bar in new-tab mode. */
async function start(): Promise<void> {
  await clu().onboarding.complete({ skipped: false })
  const r = await clu().state.get()
  const windowId = clu().windowId
  const w = r.ok && windowId ? r.value.windows[windowId] : undefined
  if (w?.shell.sidebar.hidden && !w.shell.focusMode) await clu().commands.run({ id: 'sidebar.toggle' })
  await clu().commands.run({ id: 'commandbar.newTab' })
}

function Panel(props: {
  heading: HeadingRef
  title: string
  icon?: boolean
  back?: () => void
  actions: ReactNode
  children: ReactNode
}) {
  return (
    <>
      {props.icon ? <img className="clu-ob-icon" src={iconUrl} alt="" width={72} height={72} /> : null}
      <h1 id="clu-ob-title" ref={props.heading} tabIndex={-1}>
        {props.title}
      </h1>
      <div className="clu-ob-body">{props.children}</div>
      <div className="clu-ob-actions">
        {props.back ? (
          <Button onClick={props.back}>Back</Button>
        ) : (
          <span />
        )}
        <div className="clu-ob-actions-end">{props.actions}</div>
      </div>
    </>
  )
}

function Primary({ onClick, children }: { onClick: () => void; children: ReactNode }) {
  return (
    <Button variant="primary" onClick={onClick}>
      {children}
    </Button>
  )
}

function Secondary({ onClick, children }: { onClick: () => void; children: ReactNode }) {
  return (
    <Button onClick={onClick}>{children}</Button>
  )
}

/** The space theme editor itself, the same one the space menu opens: the color canvas with − and +, every swatch
 * page, intensity, grain and the mode buttons, on the window's space, scaled down to fit the card at the smallest window. */
function Look({ spaceId }: { spaceId: string | null }) {
  const windowId = useWindowId()
  const slot = useRef<HTMLDivElement>(null)
  const [zoom, setZoom] = useState(1)
  useLayoutEffect(() => {
    const el = slot.current
    if (!el) return
    // The height is the slot's (the card's); the width is what the card leaves beside the title column, since the
    // slot's own width follows the zoom.
    const card = el.closest<HTMLElement>('.clu-ob-card')
    const fit = () => setZoom(lookZoom(card ? card.clientWidth - LOOK_SIDE : el.clientWidth, el.clientHeight))
    fit()
    if (typeof ResizeObserver === 'undefined') return
    const ro = new ResizeObserver(fit)
    ro.observe(el)
    if (card) ro.observe(card)
    return () => ro.disconnect()
  }, [])
  return (
    <div ref={slot} className="clu-ob-look" style={{ '--clu-ob-look-zoom': zoom } as CSSProperties}>
      {spaceId ? <ThemeEditor spaceId={spaceId} windowId={windowId ?? ''} inline /> : null}
    </div>
  )
}

function Keys({ keys, label }: { keys: string[]; label: string }) {
  return (
    <span className="clu-ob-keys" aria-label={label} role="img">
      {keys.map((k) => (
        <kbd key={k}>{k}</kbd>
      ))}
    </span>
  )
}

function Moves({ toggled }: { toggled: boolean }) {
  return (
    <>
      <ul className="clu-ob-moves">
        <li>
          <Keys keys={['⌘', 'T']} label="Command T" />
          <span>Open a site, search or run a command from the command bar.</span>
        </li>
        <li data-try="true">
          <Keys keys={['⌘', 'S']} label="Command S" />
          <span>Show or hide the sidebar.</span>
          <span className="clu-ob-mini" data-toggled={toggled} aria-hidden="true">
            <span className="clu-ob-mini-sidebar" />
            <span className="clu-ob-mini-page" />
          </span>
        </li>
        <li>
          <Keys keys={['⇧', '⌘', 'C']} label="Shift Command C" />
          <span>Copy the link of the page you are on.</span>
        </li>
      </ul>
      <p className="clu-ob-hint" role="status">
        {toggled ? 'Press ⌘S again any time to bring it back.' : 'Press ⌘S to try it.'}
      </p>
    </>
  )
}

function DefaultBrowser({ heading, back, next }: { heading: HeadingRef; back?: () => void; next: () => void }) {
  const isDefault = useCluState((s) => s.app.defaultBrowser === 'default')
  const [unavailable, setUnavailable] = useState(false)
  const [busy, setBusy] = useState(false)
  const request = async () => {
    setBusy(true)
    const outcome = defaultBrowserOutcome(await invoke(clu().platform.requestDefaultBrowser()))
    setBusy(false)
    if (outcome === 'advance') next()
    else setUnavailable(true)
  }
  if (isDefault) {
    return (
      <Panel heading={heading} title="Rio is your default browser" back={back} actions={<Primary onClick={next}>Continue</Primary>}>
        <p>Links you open in other apps already come here.</p>
      </Panel>
    )
  }
  return (
    <Panel
      heading={heading}
      title="Make Rio your default browser"
      back={back}
      actions={
        unavailable ? (
          <Primary onClick={next}>Skip</Primary>
        ) : (
          <>
            <Secondary onClick={next}>Skip</Secondary>
            <Button variant="primary" disabled={busy} onClick={() => void request()}>
              Set as default browser
            </Button>
          </>
        )
      }
    >
      <p>Links you click in Mail, Messages and other apps then open in a Mini window, so you can read them and get back to work.</p>
      {unavailable ? (
        <p className="clu-ob-note" role="status">
          Install Rio in Applications to make it your default browser.
        </p>
      ) : null}
    </Panel>
  )
}

/**
 * The switch screen: everything another browser holds, one checkbox each, one button, then progress and the
 * summary. Not now moves on from every step; an import already running finishes on its own, and finishing setup moves
 * the window to the imported main space.
 */
/** `next(done)`: done is true after an import that worked or a Not now, false after one that failed. */
function Switch({ heading, previous, next }: { heading: HeadingRef; previous: number; next: (done: boolean) => void }) {
  const { items, toggle, running, progress, result, run, replacing, replace, keep } = useSwitchImport(previous)
  const actions = result ? (
    <Primary onClick={() => next(!result.error && result.value !== null)}>Continue</Primary>
  ) : (
    <>
      <Button onClick={() => next(true)}>Not now</Button>
      <Button variant="primary" disabled={running || replacing !== null} onClick={() => void run()}>
        {running ? 'Bringing it over…' : 'Bring everything over'}
      </Button>
    </>
  )
  return (
    <Panel heading={heading} title="Bring everything over" icon actions={actions}>
      <p>Rio found another browser on this Mac. Bring your spaces, tabs and sign-ins over in one go; its files are only read.</p>
      {result ? (
        <SwitchSummary result={result} />
      ) : replacing !== null ? (
        <SwitchReplaceAsk previous={replacing} replace={replace} keep={keep} />
      ) : running ? (
        <p className="clu-ob-note" role="status">
          {progress ?? 'Starting…'}
        </p>
      ) : (
        <SwitchChecklist items={items} toggle={toggle} disabled={running} />
      )}
    </Panel>
  )
}

function Import({ heading, back, next }: { heading: HeadingRef; back?: () => void; next: () => void }) {
  const [failed, setFailed] = useState(false)
  const finish = async (opened: Promise<{ ok: boolean }>) => {
    if ((await opened).ok) await clu().onboarding.complete({ skipped: false })
    else setFailed(true)
  }
  // Each source opens its own import (never General alone): the Chrome import (#import-chrome) picks the
  // profile and what to bring, the Import from Arc card the sidebar file, Chromium's password page the CSV file;
  // opening one ends setup.
  const command = (id: 'settings.importChrome' | 'settings.importArc') => finish(invoke(clu().commands.run({ id })))
  const passwords = () => finish(invoke(clu().import.openPasswords()))
  return (
    <Panel heading={heading} title="Bring everything over" back={back} actions={<Primary onClick={next}>Skip</Primary>}>
      <p>
        Switching browsers? Bring your spaces, pinned tabs, favorites, history, saved passwords and sign-ins from another browser, or bookmarks and
        history from Google Chrome.
      </p>
      <div className="clu-ob-sources">
        <Button onClick={() => void command('settings.importArc')}>Switch from another browser</Button>
        <Button onClick={() => void command('settings.importChrome')}>From Google Chrome</Button>
        <Button onClick={() => void passwords()}>Import passwords from a CSV file</Button>
      </div>
      {failed ? (
        <p className="clu-ob-note" role="status">
          That did not open. You can import any time from Settings.
        </p>
      ) : null}
    </Panel>
  )
}
