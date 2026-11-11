// Shared controls (docs/chromium/ui-polish.md): the one Button, Select, Input, Toggle, Card and Row every surface Rio
// draws itself uses, so a control has one height, one hairline and one color family per mode. Styles are in
// controls.css and their values in tokens.css (the control and page tokens).
import type { ButtonHTMLAttributes, HTMLAttributes, InputHTMLAttributes, ReactNode, Ref, SelectHTMLAttributes } from 'react'

const cx = (...names: (string | false | null | undefined)[]) => names.filter(Boolean).join(' ')

export type ButtonVariant = 'default' | 'primary' | 'danger' | 'plain'

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant
  ref?: Ref<HTMLButtonElement>
}

export function Button({ variant = 'default', className, type = 'button', ...rest }: ButtonProps) {
  return <button type={type} className={cx('clu-btn', className)} data-variant={variant} {...rest} />
}

export interface SelectProps extends SelectHTMLAttributes<HTMLSelectElement> {
  ref?: Ref<HTMLSelectElement>
}

/** A native select (the system menu opens on click) drawn as a control: the value and an up-down chevron. */
export function Select({ className, children, ...rest }: SelectProps) {
  return (
    <span className={cx('clu-select', className)}>
      <select {...rest}>{children}</select>
      <svg width="8" height="12" viewBox="0 0 8 12" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
        <path d="M1 4.5 4 1.5l3 3M1 7.5l3 3 3-3" />
      </svg>
    </span>
  )
}

export interface InputProps extends InputHTMLAttributes<HTMLInputElement> {
  ref?: Ref<HTMLInputElement>
}

export function Input({ className, type = 'text', ...rest }: InputProps) {
  return <input type={type} className={cx('clu-input', className)} {...rest} />
}

export interface ToggleProps extends Omit<ButtonHTMLAttributes<HTMLButtonElement>, 'onChange' | 'role' | 'aria-checked'> {
  /** The accessible name; a toggle shows no text of its own. */
  label: string
  checked: boolean
  onChange: (next: boolean) => void
}

/** An on/off switch: a button with role="switch". */
export function Toggle({ label, checked, onChange, className, ...rest }: ToggleProps) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={label}
      className={cx('clu-toggle', className)}
      data-on={checked}
      onClick={() => onChange(!checked)}
      {...rest}
    >
      <span aria-hidden="true" />
    </button>
  )
}

/** A grouped list of rows on the page or dialog: one fill, one hairline, rows split by hairlines. */
export function Card({ className, ...rest }: HTMLAttributes<HTMLDivElement>) {
  return <div className={cx('clu-group', className)} {...rest} />
}

export interface RowProps extends Omit<HTMLAttributes<HTMLDivElement>, 'title'> {
  label?: ReactNode
  /** Makes the label a <label> for this control id. */
  htmlFor?: string
  labelId?: string
  hint?: ReactNode
  hintId?: string
  hintRole?: 'status' | 'alert'
  /** The control column (selects, buttons, toggles, values), right-aligned. */
  children?: ReactNode
}

/** One row of a card: the label and its hint on the left, the control on the right, centered on one line. */
export function Row({ label, htmlFor, labelId, hint, hintId, hintRole, children, className, ...rest }: RowProps) {
  return (
    <div className={cx('clu-group-row', className)} {...rest}>
      {label !== undefined || hint !== undefined ? (
        <div className="clu-group-row-text">
          {label === undefined ? null : htmlFor ? (
            <label htmlFor={htmlFor} id={labelId} className="clu-group-row-label">
              {label}
            </label>
          ) : (
            <span id={labelId} className="clu-group-row-label">
              {label}
            </span>
          )}
          {hint ? (
            <p id={hintId} className="clu-group-row-hint" role={hintRole}>
              {hint}
            </p>
          ) : null}
        </div>
      ) : null}
      {children === undefined || children === null ? null : <div className="clu-group-row-control">{children}</div>}
    </div>
  )
}
