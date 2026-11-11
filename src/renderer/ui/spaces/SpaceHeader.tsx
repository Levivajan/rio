// Space header (docs/plan/03-spaces.md "Create and edit sheet"): the space's icon in the
// favicon slot and its name; double-click renames inline. Enter or blur saves, Esc cancels, and an empty or unchanged
// name reverts without a call. The whole row toggles the pinned rows; on hover, and while they are hidden, a chevron shows where the icon is, or left of the name ("> name")
// when the space has none, and a three-dot button or a right-click opens the space's menu.
import { useEffect, useRef, useState, type SVGProps } from 'react'
import { normalizeName } from '../../../shared/spaces'
import { hasGlyph, SpaceIconGlyph } from '../../common/spaceSymbols'
import { useCluState } from '../../common/useCluState'
import { openSpaceMenu, RENAME_SPACE_EVENT } from '../tabs/renaming'
import './spaces.css'
import { clu } from '../../common/clu'

export function RenameField({ spaceId, name, onDone }: { spaceId: string; name: string; onDone: () => void }) {
  const [value, setValue] = useState(name)
  const [done, setDone] = useState(false)
  const finish = (save: boolean) => {
    if (done) return
    setDone(true)
    const clean = normalizeName(value)
    if (save && clean && clean !== name) void clu().spaces.update({ spaceId, name: clean })
    onDone()
  }
  return (
    <input
      className="clu-space-header-input"
      aria-label="Space name"
      value={value}
      maxLength={240}
      autoFocus
      spellCheck={false}
      onFocus={(e) => e.currentTarget.select()}
      onChange={(e) => setValue(e.currentTarget.value)}
      onBlur={() => finish(true)}
      onKeyDown={(e) => {
        if (e.key === 'Enter' && !e.nativeEvent.isComposing) finish(true)
        else if (e.key === 'Escape') finish(false)
      }}
    />
  )
}

/** A private window's header: its unsaved space has a fixed name and no rename. */
function PrivateHeader({ spaceId, name }: { spaceId: string; name: string }) {
  return (
    <div className="clu-space-header" data-space-header={spaceId} data-private="true">
      <svg className="clu-space-header-private" width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
        <path d="M2 7.5h12M4 7.5l1.2-3.6a1 1 0 0 1 1-.7h3.6a1 1 0 0 1 1 .7L12 7.5" />
        <circle cx="5" cy="11" r="1.8" />
        <circle cx="11" cy="11" r="1.8" />
        <path d="M6.8 11h2.4" />
      </svg>
      {name}
    </div>
  )
}

const glyph = { fill: 'none', stroke: 'currentColor', strokeWidth: 1.5, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const
/** The chevron: 11 x 6.5, pointing down while the pinned rows show. */
const ChevronIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={16} height={16} viewBox="0 0 16 16" {...glyph} {...p}><path d="M2.5 5.25 8 10.75l5.5-5.5" /></svg>
)
/** Left of a name with no icon: a small chevron, 4.5 x 9 pointing right, down while the rows show. */
const SmallChevronIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={8} height={10} viewBox="0 0 8 10" {...glyph} strokeWidth={1.6} {...p}><path d="M1.75 0.5 6.25 5l-4.5 4.5" /></svg>
)
/** Three dots, 12.5 x 2.5. */
const MoreIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={16} height={16} viewBox="0 0 16 16" fill="currentColor" aria-hidden="true" {...p}>
    <circle cx="2.95" cy="8" r="1.25" /><circle cx="8" cy="8" r="1.25" /><circle cx="13.05" cy="8" r="1.25" />
  </svg>
)

function HeaderBody({ spaceId }: { spaceId: string }) {
  const name = useCluState((s) => s.spaces[spaceId]?.name ?? '')
  const ephemeral = useCluState((s) => s.spaces[spaceId]?.ephemeral ?? false)
  const icon = useCluState((s) => s.spaces[spaceId]?.icon)
  // An emoji or a symbol draws before the name; the letter icon draws nothing here.
  const emoji = hasGlyph(icon)
  const iconKind = emoji ? icon?.kind : 'none'
  const hasPinned = useCluState((s) => (s.spaces[spaceId]?.pinned.length ?? 0) > 0)
  const collapsed = useCluState((s) => s.spaces[spaceId]?.pinnedCollapsed ?? false)
  const [editing, setEditing] = useState(false)
  useEffect(() => {
    const onRename = (e: Event) => (e as CustomEvent<string>).detail === spaceId && name && setEditing(true)
    window.addEventListener(RENAME_SPACE_EVENT, onRename)
    return () => window.removeEventListener(RENAME_SPACE_EVENT, onRename)
  }, [spaceId, name])
  // The state before a click, which a double-click puts back.
  const beforeClick = useRef(collapsed)
  if (ephemeral) return <PrivateHeader spaceId={spaceId} name={name} />
  const setCollapsed = (next: boolean) => void clu().sections.setPinnedCollapsed({ spaceId, collapsed: next })
  const toggle = hasPinned ? (
    <button
      type="button"
      className="clu-space-header-toggle clu-reveal"
      aria-label={collapsed ? 'Show pinned tabs' : 'Hide pinned tabs'}
      aria-expanded={!collapsed}
    >
      {emoji ? (
        <ChevronIcon style={collapsed ? { transform: 'rotate(-90deg)' } : undefined} />
      ) : (
        <SmallChevronIcon style={collapsed ? undefined : { transform: 'rotate(90deg)' }} />
      )}
    </button>
  ) : null
  return (
    <div
      className="clu-space-header"
      data-space-header={spaceId}
      data-icon={iconKind}
      data-collapsed={collapsed}
      data-toggles={hasPinned}
      onClick={(e) => {
        // The first click of a double-click toggles; the double-click puts it back and renames.
        if (!hasPinned || editing || e.detail > 1 || (e.target as Element).closest('.clu-space-header-more')) return
        beforeClick.current = collapsed
        setCollapsed(!collapsed)
      }}
      onDoubleClick={(e) => {
        if ((e.target as Element).closest('.clu-space-header-more') || !name) return
        if (hasPinned && !editing) setCollapsed(beforeClick.current)
        setEditing(true)
      }}
      // A right-click opens the space's menu at the pointer; the rename field keeps
      // its own text menu.
      onContextMenu={(e) => {
        if (editing) return
        e.preventDefault()
        void openSpaceMenu(spaceId, { x: Math.round(e.clientX), y: Math.round(e.clientY) }, () => name && setEditing(true))
      }}
    >
      {emoji ? (
        <span className="clu-space-header-icon">
          <SpaceIconGlyph icon={icon} size={14} className="clu-space-header-emoji" />
          {toggle}
        </span>
      ) : (
        toggle
      )}
      <span className="clu-space-header-name">{editing && name ? <RenameField spaceId={spaceId} name={name} onDone={() => setEditing(false)} /> : name}</span>
      <button
        type="button"
        className="clu-space-header-more clu-reveal"
        aria-label="Space options"
        aria-haspopup="menu"
        onDoubleClick={(e) => e.stopPropagation()}
        onClick={(e) => {
          const r = e.currentTarget.getBoundingClientRect()
          void openSpaceMenu(spaceId, { x: Math.round(r.x), y: Math.round(r.y + r.height) }, () => name && setEditing(true))
        }}
      >
        <MoreIcon />
      </button>
    </div>
  )
}

// Keyed by space: a switch drops an open rename field, and the name selector never serves the previous space's
// cached value (useCluState caches per store snapshot, not per selector).
export function SpaceHeader({ spaceId }: { spaceId: string }) {
  return <HeaderBody key={spaceId} spaceId={spaceId} />
}
