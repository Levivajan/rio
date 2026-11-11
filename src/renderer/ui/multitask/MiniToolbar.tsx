// Mini window toolbar (05 Mini window, decision 37): 36 px, a drag region. Back (no Forward), favicon and host, Site
// controls, "Open in ⟨Space⟩" and Close; a 2 px progress bar while the page loads. No extensions row:
// extensions live in the Site controls panel.
// Everything fits from the 480 minimum width up: the host truncates to
// 64 px, then the "Open in" label, which never clips, so a tooltip beside its buttons shows whole.
import { displayHost } from '../../../shared/url'
import { MINI_TOOLBAR_H } from '../../../shared/multitask'
import type { MiniWindowState } from '../../../shared/state/multitask'
import { BackIcon, CloseIcon } from '../../common/icons'
import { IconButton } from '../../common/IconButton'
import { LetterTile } from '../../common/LetterTile'
import { Tooltip } from '../../common/Tooltip'
import { SiteControlsButton } from '../extensions/SiteControlsButton'
import { useProgressBar } from './useProgressBar'
import { clu } from '../../common/clu'

/** Tooltips open beside the button: below it the page view would cover them. */
const TIP_RIGHT = '[&_.clu-tooltip]:top-1/2! [&_.clu-tooltip]:left-[calc(100%+6px)]! [&_.clu-tooltip]:-translate-y-1/2! [&_.clu-tooltip]:translate-x-0!'
const PILL = 'transition-colors duration-[var(--t-fast)] hover:bg-[color-mix(in_oklab,var(--ink)_8%,transparent)]'
const TIP_LEFT = '[&_.clu-tooltip]:top-1/2! [&_.clu-tooltip]:left-auto! [&_.clu-tooltip]:right-[calc(100%+6px)]! [&_.clu-tooltip]:-translate-y-1/2! [&_.clu-tooltip]:translate-x-0! [&_.clu-tooltip]:z-10'

export function MiniToolbar({ mini }: { mini: MiniWindowState | null }) {
  const url = mini?.url ?? ''
  const host = url && url !== 'about:blank' ? displayHost(url) : ''
  const label = host || mini?.title || 'New page'
  const bar = useProgressBar(mini?.loading ?? false, mini?.progress ?? 0)
  return (
    <header
      data-mini-toolbar
      className="relative flex items-center gap-1 pr-2"
      style={{ height: MINI_TOOLBAR_H, color: 'var(--ink)', fontSize: 'var(--fs-row)' }}
    >
      {/* The lights sit over the spacer, so it never shrinks here. */}
      <div className="clu-traffic-spacer" style={{ flex: 'none' }} />
      <div className={`flex items-center ${TIP_RIGHT}`}>
        <IconButton aria-label="Back" disabled={!mini?.canGoBack} onClick={() => void clu().mini.back()}>
          <BackIcon />
        </IconButton>
      </div>
      <div data-mini-site className="flex min-w-16 flex-1 items-center gap-2 overflow-hidden pl-1">
        {mini?.favicon ? (
          <img data-mini-favicon src={mini.favicon} alt="" width={16} height={16} draggable={false} className="flex-none" style={{ width: 'var(--fav)', height: 'var(--fav)' }} />
        ) : (
          <span data-mini-favicon className="inline-flex flex-none">
            <LetterTile text={host || label} />
          </span>
        )}
        <span data-mini-host className="truncate" style={{ color: 'var(--ink-2)' }}>
          {label}
        </span>
      </div>
      <div className={`flex min-w-0 flex-[0_1_auto] items-center gap-1 ${TIP_LEFT}`}>
        <div data-slot="site-controls" className="flex flex-none items-center">
          <SiteControlsButton />
        </div>
        <div data-mini-open-group className="flex min-w-0 items-center rounded-md bg-[var(--hover-ink)] [-webkit-app-region:no-drag]" style={{ height: 24 }}>
          <button
            type="button"
            data-mini-open-in
            className={`max-w-56 min-w-0 truncate rounded-l-md pr-2 pl-2.5 ${PILL}`}
            style={{ height: 24, fontSize: 'var(--fs-2)', color: 'var(--ink)' }}
            onClick={() => void clu().mini.moveToSpace({})}
          >
            Open in {mini?.targetSpaceName || 'Space'}
          </button>
          <span aria-hidden="true" className="h-3.5 w-px flex-none" style={{ background: 'color-mix(in oklab, var(--ink) 14%, transparent)' }} />
          <Tooltip label="Choose a space">
            <button
              type="button"
              data-mini-pick-space
              aria-label="Choose a space"
              className={`flex items-center justify-center rounded-r-md px-1.5 ${PILL}`}
              style={{ height: 24, color: 'var(--ink-2)' }}
              onClick={() => void clu().mini.pickSpace()}
            >
              <svg width="10" height="10" viewBox="0 0 10 10" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
                <path d="M2.5 4l2.5 2.5L7.5 4" />
              </svg>
            </button>
          </Tooltip>
        </div>
        <IconButton aria-label="Close" onClick={() => void clu().window.close()}>
          <CloseIcon />
        </IconButton>
      </div>
      {bar.visible ? (
        <div
          data-mini-progress
          role="progressbar"
          aria-label="Page loading"
          aria-valuemin={0}
          aria-valuemax={100}
          aria-valuenow={Math.round(bar.value * 100)}
          className="absolute bottom-0 left-0 h-[2px]"
          style={{
            width: `${bar.value * 100}%`,
            background: 'var(--accent)',
            opacity: bar.done ? 0 : 1,
            transition: `width var(--t-base) var(--ease-out), opacity var(--t-base) var(--ease-out)`
          }}
        />
      ) : null}
    </header>
  )
}
