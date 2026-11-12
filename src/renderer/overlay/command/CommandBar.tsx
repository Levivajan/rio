// Command bar (04-command b): a combobox input over a listbox of the top row (what Enter does), Tabs,
// Actions, History and Suggestions, or Recent tabs for an empty input. State and keys live in useCommandBar; this view
// places the bar on the page frame, tints it with the space accent, plays the open and close animations (instant with
// reduced motion), keeps the inline completion selected and records the keystroke-to-paint measure.
import { Fragment, useCallback, useEffect, useId, useLayoutEffect, useMemo, useRef, useState, type ChangeEvent, type CSSProperties } from 'react'
import { ASSISTANTS } from '../../../shared/command/ask'
import { MAX_INPUT } from '../../../shared/command/classify'
import { markAfterPaint } from '../../common/perf'
import { sameItems, useCluState } from '../../common/useCluState'
import type { SurfaceProps } from '../core/types'
import { AssistantIcon } from './AssistantIcon'
import { historyFavicon, ResultRow, RowIcon, safeFavicon } from './ResultRow'
import { useCommandBar, type BarGroup, type Disposition } from './useCommandBar'
import './command.css'

const GROUP_LABEL: Record<BarGroup, string> = { top: '', recent: 'Recent tabs', tabs: 'Tabs', actions: 'Actions', history: 'History', suggestions: 'Suggestions', ask: 'Ask', help: 'Help' }
/** The assistant menu's width and its gap under the Ask row's label. */
const PICKER_W = 176
const PICKER_GAP = 6
/** Keystroke measures kept for the perf test; older ones are dropped. */
const MAX_MEASURES = 500
const COLOR = /^(#[0-9a-f]{3,8}|(rgb|hsl)a?\([0-9.,%\s/]+\))$/i

/** Marks the input event; the measure ends on the next frame after React commits (04-command F13). */
function markInput(): void {
  performance.clearMarks('cmd:input')
  performance.mark('cmd:input')
}
function measureKeystroke(): void {
  requestAnimationFrame(() => {
    if (performance.getEntriesByName('cmd:input', 'mark').length === 0) return
    if (performance.getEntriesByName('cmd:keystroke', 'measure').length >= MAX_MEASURES) performance.clearMeasures('cmd:keystroke')
    performance.measure('cmd:keystroke', 'cmd:input')
    performance.clearMarks('cmd:input')
  })
}

export function CommandBar({ windowId }: SurfaceProps) {
  const bar = useCommandBar(windowId)
  const { session, text, rows, active, completionRange, closing, resetSeq, finishClose } = bar
  const accent = useCluState((s) => s.spaces[s.windows[windowId]?.activeSpaceId ?? '']?.accent ?? null)
  // Only the listed tabs' favicons: a patch to any other tab leaves the bar alone.
  const rowTabIds = useMemo(() => rows.flatMap((r) => (r.result.kind === 'tab' || r.result.kind === 'reload' ? [r.result.tabId] : [])), [rows])
  const rowFavicons = useCluState((s) => rowTabIds.map((id) => s.tabs[id]?.favicon ?? null), sameItems)
  const pageRect = useCluState((s) => s.windows[windowId]?.shell.layout.pageRect ?? null)
  const pageRadius = useCluState((s) => s.windows[windowId]?.shell.layout.radius ?? 0)
  const [brokenFavicons, setBrokenFavicons] = useState<ReadonlySet<string>>(() => new Set())
  const inputRef = useRef<HTMLInputElement>(null)
  const barRef = useRef<HTMLDivElement>(null)
  const composing = useRef(false)
  const typedAt = useRef(false)
  /** The session whose first frame was reported; each open reports `cmd:open:painted` once (quality Q9). */
  const painted = useRef<string | null>(null)
  const listId = useId()
  const optionId = useId()
  const pickerId = useId()
  const [pickerAt, setPickerAt] = useState<{ top: number; left: number } | null>(null)
  const latest = useRef(bar)
  useLayoutEffect(() => {
    latest.current = bar
  })

  // Focus the input and select its text when a session starts or switches mode (edit-url selects the URL).
  useLayoutEffect(() => {
    const input = inputRef.current
    if (!input || !session) return
    input.focus()
    input.select()
    if (painted.current !== session.sessionId) {
      painted.current = session.sessionId
      markAfterPaint('cmd:open:painted')
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps -- runs once per reset, not per keystroke
  }, [resetSeq, session?.sessionId])

  // The inline completion is the selected tail of the input, so typing over it replaces it.
  useLayoutEffect(() => {
    const input = inputRef.current
    if (input && completionRange && document.activeElement === input) input.setSelectionRange(completionRange[0], completionRange[1], 'forward')
  }, [text, completionRange])

  useEffect(() => {
    if (!typedAt.current) return
    typedAt.current = false
    measureKeystroke()
  })

  // The close animation runs for the bar's computed duration (0 with reduced motion), then main hides the overlay.
  useLayoutEffect(() => {
    if (!closing) return
    const el = barRef.current
    const seconds = el ? Math.max(...getComputedStyle(el).animationDuration.split(',').map((d) => Number.parseFloat(d) || 0)) : 0
    const timer = setTimeout(finishClose, Math.min(Math.max(seconds, 0) * 1000, 1000))
    return () => clearTimeout(timer)
  }, [closing, finishClose])

  // The assistant menu hangs under the Ask row's label, right-aligned to it, or above it when the window ends first.
  useLayoutEffect(() => {
    if (!bar.picker.open) {
      setPickerAt(null)
      return
    }
    const label = barRef.current?.querySelector('[data-ask-picker]')
    if (!label) {
      setPickerAt(null)
      return
    }
    const r = label.getBoundingClientRect()
    const height = ASSISTANTS.length * 28 + 10
    const below = r.bottom + PICKER_GAP
    const top = below + height > window.innerHeight - 8 ? Math.max(8, r.top - PICKER_GAP - height) : below
    setPickerAt({ top, left: Math.max(8, r.right - PICKER_W) })
  }, [bar.picker.open, rows])

  const onHover = useCallback((i: number) => latest.current.hover(i), [])
  const onPicker = useCallback(() => latest.current.togglePicker(), [])
  const onRun = useCallback((i: number, d: Disposition) => latest.current.run(i, d), [])
  const onRemove = useCallback((i: number) => latest.current.remove(i), [])
  const onFaviconError = useCallback((src: string) => setBrokenFavicons((s) => new Set([...s, src])), [])

  if (!session) return null
  const faviconOf = (tabId: string): string | null => {
    const src = safeFavicon(rowFavicons[rowTabIds.indexOf(tabId)])
    return src && !brokenFavicons.has(src) ? src : null
  }
  const historyIcon = (url: string): string | null => {
    const src = historyFavicon(url)
    return src && !brokenFavicons.has(src) ? src : null
  }
  const askMode = bar.askMode
  const rowFavicon = (row: (typeof rows)[number]): string | null =>
    row.result.kind === 'tab' || row.result.kind === 'reload' ? faviconOf(row.result.tabId) : row.result.kind === 'history' ? historyIcon(row.result.url) : null
  // The field's icon follows the selected row once something is typed (the page's favicon, an action's compass,
  // the magnifier for a search); an empty field keeps the magnifier.
  const selectedRow = text && !bar.actionsOnly && !askMode ? rows[active] : undefined
  const pickerOpen = bar.picker.open && pickerAt !== null

  const onChange = (e: ChangeEvent<HTMLInputElement>) => {
    markInput()
    typedAt.current = true
    const el = e.target
    const native = e.nativeEvent as InputEvent
    const isComposing = composing.current || native.isComposing === true
    const atEnd = el.selectionStart === el.value.length && el.selectionEnd === el.value.length
    const one = native.inputType === 'insertText' && typeof native.data === 'string' && Array.from(native.data).length === 1
    bar.input(el.value, { insertedAtEnd: atEnd && one, composing: isComposing })
  }

  // Centered on the window, not the page card, in every mode (M11).
  const frame: CSSProperties = { left: 0, width: '100%' }
  const tint = accent && COLOR.test(accent) ? ({ '--accent': accent } as CSSProperties) : undefined
  const listOpen = rows.length > 0
  // The page card dims behind the bar, the sidebar does not; the Mini window's bar is
  // the whole window.
  const dim = session.mode !== 'mini' && pageRect && pageRect.width > 0 && pageRect.height > 0
    ? ({ left: pageRect.x, top: pageRect.y, width: pageRect.width, height: pageRect.height, borderRadius: pageRadius } as CSSProperties)
    : null

  return (
    <>
      <div className="clu-cmd-scrim" aria-hidden="true" onPointerDown={bar.close}>
        {dim ? <div className="clu-cmd-dim" data-command-dim="" style={dim} /> : null}
      </div>
      <div className="clu-cmd-frame" style={frame}>
        <div
          ref={barRef}
          className="clu-cmd"
          style={tint}
          data-command-mode={session.mode}
          data-closing={closing || undefined}
          data-actions-only={bar.actionsOnly || undefined}
          onPointerDown={(e) => e.stopPropagation()}
        >
          <div className="clu-cmd-field">
            <span className="clu-cmd-field-icon" aria-hidden="true" data-follows={selectedRow ? selectedRow.result.kind : undefined}>
              {selectedRow ? (
                <RowIcon row={selectedRow} favicon={rowFavicon(selectedRow)} onFaviconError={() => {
                  const src = rowFavicon(selectedRow)
                  if (src) onFaviconError(src)
                }} />
              ) : (
                <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round">
                  <circle cx="7" cy="7" r="4.75" />
                  <path d="m10.5 10.5 3.25 3.25" />
                </svg>
              )}
            </span>
            {bar.actionsOnly ? <span className="clu-cmd-chip">Actions</span> : null}
            {askMode ? (
              <span className="clu-cmd-chip" data-assistant={askMode.id}>
                <AssistantIcon id={askMode.id} />
                {askMode.name}
              </span>
            ) : null}
            <input
              ref={inputRef}
              role="combobox"
              aria-label={bar.actionsOnly ? 'Search actions' : askMode ? `Ask ${askMode.name}` : 'Search or enter URL'}
              aria-expanded={listOpen}
              aria-controls={pickerOpen ? pickerId : listOpen ? listId : undefined}
              aria-activedescendant={pickerOpen ? `${pickerId}-${bar.picker.index}` : active >= 0 ? `${optionId}-${active}` : undefined}
              aria-autocomplete="both"
              autoComplete="off"
              spellCheck={false}
              placeholder={bar.actionsOnly ? 'Search actions' : askMode ? 'Ask anything…' : 'Search or Enter URL...'}
              value={text}
              maxLength={MAX_INPUT}
              onChange={onChange}
              onCompositionStart={() => {
                composing.current = true
              }}
              onCompositionEnd={() => {
                composing.current = false
              }}
              onKeyDown={(e) => {
                if (e.nativeEvent.isComposing || composing.current) return
                bar.keyDown(e)
              }}
            />
            {bar.tabAssistant ? (
              // The right-side hint: a label and a "Tab" keycap.
              <span className="clu-cmd-hint" data-hint="ask">
                Ask {bar.tabAssistant.name}
                <kbd>Tab</kbd>
              </span>
            ) : null}
            {session.mode === 'mini' ? (
              // The Mini window's prompt closes from its own button too (a 24 pt grey square).
              <button type="button" className="clu-cmd-close" aria-label="Close" onClick={bar.close}>
                <svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
                  <path d="m2.5 2.5 7 7m0-7-7 7" />
                </svg>
              </button>
            ) : null}
          </div>
          {listOpen ? (
            <div role="listbox" id={listId} aria-label="Results" className="clu-cmd-list">
              {groups(rows).map(({ group, key, start, end }) => {
                const items = rows.slice(start, end).map((row, k) => {
                  const i = start + k
                  return (
                    <ResultRow
                      key={row.key}
                      row={row}
                      index={i}
                      id={`${optionId}-${i}`}
                      selected={i === active}
                      favicon={rowFavicon(row)}
                      onHover={onHover}
                      onRun={onRun}
                      onRemove={onRemove}
                      onFaviconError={onFaviconError}
                      {...(row.ask ? { pickerOpen, onPicker } : {})}
                    />
                  )
                })
                if (group === 'top') return <Fragment key={key}>{items}</Fragment>
                const headerId = `${listId}-${key}`
                return (
                  <div key={key} role="group" aria-labelledby={headerId} className="clu-cmd-group">
                    <div id={headerId} role="presentation" className="clu-cmd-group-label">
                      {GROUP_LABEL[group]}
                    </div>
                    {items}
                  </div>
                )
              })}
            </div>
          ) : null}
        </div>
      </div>
      {pickerOpen ? (
        <div
          role="menu"
          id={pickerId}
          aria-label="Assistant"
          className="clu-cmd-picker"
          style={{ top: pickerAt.top, left: pickerAt.left, width: PICKER_W }}
          onPointerDown={(e) => {
            // The input keeps the focus, so typing goes on after a pick.
            e.preventDefault()
            e.stopPropagation()
          }}
        >
          {ASSISTANTS.map((a, i) => (
            <div
              key={a.id}
              id={`${pickerId}-${i}`}
              role="menuitemradio"
              aria-checked={a.id === bar.assistant.id}
              data-active={i === bar.picker.index || undefined}
              className="clu-cmd-picker-item"
              onPointerMove={() => i !== bar.picker.index && bar.highlightPicker(i)}
              onClick={() => bar.pick(a.id)}
            >
              <span className="clu-cmd-picker-icon" data-assistant={a.id}>
                <AssistantIcon id={a.id} />
              </span>
              <span className="clu-cmd-picker-name">{a.name}</span>
              <span className="clu-cmd-picker-check" aria-hidden="true">
                {a.id === bar.assistant.id ? (
                  <svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round">
                    <path d="m2.5 6.25 2.5 2.5 4.5-5" />
                  </svg>
                ) : null}
              </span>
            </div>
          ))}
        </div>
      ) : null}
    </>
  )
}

/** Consecutive runs of one group, in list order. A group can run twice (the Ask row sits third, inside a run of
 *  tabs), so the second run's key is `<group>-2`: React keys and header ids stay unique. */
export function groups(rows: readonly { group: BarGroup }[]): { group: BarGroup; key: string; start: number; end: number }[] {
  const out: { group: BarGroup; key: string; start: number; end: number }[] = []
  const runs = new Map<BarGroup, number>()
  rows.forEach((row, i) => {
    const last = out.at(-1)
    if (last && last.group === row.group) {
      last.end = i + 1
      return
    }
    const n = (runs.get(row.group) ?? 0) + 1
    runs.set(row.group, n)
    out.push({ group: row.group, key: n === 1 ? row.group : `${row.group}-${n}`, start: i, end: i + 1 })
  })
  return out
}
