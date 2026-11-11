// Settings rows: a labelled select bound to one setting, with its inline save error, and the profile picker.
import { useId, type ReactNode } from 'react'
import type { Json } from '../../../../shared/types'
import { Row, Select } from '../../../common/controls'
import { useSettingsProfile } from './profile'
import { useSetting } from './useSetting'

export interface Choice { value: string | number; label: string }

export function SettingSelect(props: { settingKey: string; scope: string | null; label: ReactNode; hint?: string; choices: readonly Choice[] }) {
  const { settingKey, scope, label, hint, choices } = props
  const { value, error, set } = useSetting(settingKey, scope)
  const id = useId()
  const describedBy = [hint ? `${id}-hint` : '', error ? `${id}-error` : ''].filter(Boolean).join(' ') || undefined
  // Option values are strings in the DOM; map back to the choice's own type (168, not '168').
  const parse = (raw: string): Json => {
    const hit = choices.find((c) => String(c.value) === raw)
    if (hit) return hit.value
    const n = Number(raw)
    return raw.trim() !== '' && Number.isFinite(n) ? n : raw
  }
  return (
    <Row label={label} htmlFor={id} hint={hint} hintId={`${id}-hint`}>
      <Select
        id={id}
        value={String(value)}
        disabled={!scope}
        aria-invalid={error ? true : undefined}
        aria-describedby={describedBy}
        onChange={(e) => set(parse(e.currentTarget.value))}
      >
        {choices.map((c) => (
          <option key={String(c.value)} value={String(c.value)}>
            {c.label}
          </option>
        ))}
      </Select>
      {error && (
        <p id={`${id}-error`} className="clu-settings-error" role="alert">
          {error}
        </p>
      )}
    </Row>
  )
}

/** Shown only when there is more than one profile to choose from. */
export function ProfilePicker({ hint }: { hint: string }) {
  const { options, profileId, choose } = useSettingsProfile()
  const id = useId()
  if (options.length < 2 || !profileId) return null
  return (
    <Row label="Profile" htmlFor={id} hint={hint} hintId={`${id}-hint`}>
      <Select id={id} value={profileId} aria-describedby={`${id}-hint`} onChange={(e) => choose(e.currentTarget.value)}>
        {options.map((o) => (
          <option key={o.id} value={o.id}>
            {o.name}
          </option>
        ))}
      </Select>
    </Row>
  )
}
