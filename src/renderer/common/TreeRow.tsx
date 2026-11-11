// Sidebar tree row: role treeitem with aria-selected and aria-level.
import type { HTMLAttributes, ReactNode } from 'react'

export interface TreeRowProps extends HTMLAttributes<HTMLDivElement> {
  selected: boolean
  level?: number
  expanded?: boolean
  children: ReactNode
}

export function TreeRow({ selected, level = 1, expanded, children, className, ...rest }: TreeRowProps) {
  return (
    <div
      role="treeitem"
      aria-selected={selected}
      aria-level={level}
      {...(expanded === undefined ? {} : { 'aria-expanded': expanded })}
      tabIndex={selected ? 0 : -1}
      className={`clu-row ${className ?? ''}`}
      {...rest}
    >
      {children}
    </div>
  )
}
