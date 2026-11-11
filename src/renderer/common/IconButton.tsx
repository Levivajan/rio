// Icon-only button: aria-label is required and doubles as the tooltip.
import type { ButtonHTMLAttributes, ReactNode } from 'react'
import { Tooltip } from './Tooltip'

export interface IconButtonProps extends Omit<ButtonHTMLAttributes<HTMLButtonElement>, 'aria-label' | 'title'> {
  'aria-label': string
  /** The command it runs, whose shortcut the tooltip shows as keycaps. */
  command?: string
  children: ReactNode
}

export function IconButton({ children, className, command, ...rest }: IconButtonProps) {
  return (
    <Tooltip label={rest['aria-label']} command={command}>
      <button type="button" className={`clu-icon-button ${className ?? ''}`} {...rest}>
        {children}
      </button>
    </Tooltip>
  )
}
