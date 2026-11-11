// The Library's Filter button: beside the search field, 72.5 wide,
// a menu of what the list shows. The button names the filter while one is on; "All" turns it off.
import { useRef, useState } from 'react'
import type { MenuItem } from '../../../shared/types'
import { MenuList } from '../../common/MenuList'

export interface FilterOption { id: string; label: string }

const FilterGlyph = () => (
  <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" aria-hidden="true">
    <circle cx="8" cy="8" r="6.75" />
    <path d="M4.75 6h6.5M5.75 8.25h4.5M7 10.5h2" />
  </svg>
)

export function LibraryFilter({ options, value, onChange }: { options: readonly FilterOption[]; value: string; onChange: (id: string) => void }) {
  const button = useRef<HTMLButtonElement>(null)
  const [at, setAt] = useState<{ x: number; y: number } | null>(null)
  const chosen = options.find((o) => o.id === value)
  const close = () => {
    setAt(null)
    button.current?.focus()
  }
  const items: MenuItem[] = options.map((o) => ({ kind: 'item', id: o.id, label: o.label, checked: o.id === value }))
  return (
    <>
      <button
        ref={button}
        type="button"
        className="clu-library-filter"
        aria-haspopup="menu"
        aria-expanded={at !== null}
        data-active={value !== (options[0]?.id ?? '') || undefined}
        onClick={(e) => {
          const r = e.currentTarget.getBoundingClientRect()
          setAt(at ? null : { x: Math.max(0, r.right - 180), y: r.bottom + 4 })
        }}
      >
        <FilterGlyph />
        <span>{value !== options[0]?.id && chosen ? chosen.label : 'Filter'}</span>
      </button>
      {at ? (
        <div className="clu-library-filter-layer" onPointerDown={(e) => e.target === e.currentTarget && close()}>
          <MenuList
            label="Filter"
            style={{ position: 'fixed', left: at.x, top: at.y, minWidth: 180 }}
            items={items}
            onClose={close}
            onPick={(id) => {
              close()
              onChange(id)
            }}
          />
        </div>
      ) : null}
    </>
  )
}
