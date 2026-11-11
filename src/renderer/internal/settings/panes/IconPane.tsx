// Icon (docs/chromium/parity-settings.md section 3, docs/chromium/icons.md): the App icon picker, laid out as the
// Icon tab: one card of rows 81 apart, each a radio, a 64 pt preview, the icon's
// name, and "Current icon" under the one in use. A pick saves appearance.appIcon; the browser sets the Dock icon from it
// right away and at every launch. A missing or junk value reads as the default.
import { useId } from 'react'
import { APP_ICONS, DEFAULT_APP_ICON, type AppIconId } from '../../../../shared/appIcons'
import flatWarm from '../../../../../assets/brand/clu-icon.svg'
import airySun from '../../../../../assets/brand/app-icons/rio-airySun.svg'
import ocean from '../../../../../assets/brand/app-icons/rio-ocean.svg'
import flatBlue from '../../../../../assets/brand/app-icons/rio-flatBlue.svg'
import rioSunset from '../../../../../assets/brand/app-icons/rio-sunset.svg'
import warm from '../../../../../assets/brand/app-icons/rio-warm.svg'
import dusk from '../../../../../assets/brand/app-icons/rio-dusk.svg'
import { useSetting } from '../../../settings-sections/data/parts/useSetting'

const ART: Record<AppIconId, string> = { flatWarm, airySun, ocean, flatBlue, sunset: rioSunset, warm, dusk }

export function IconPane() {
  const { value, error, set } = useSetting('appearance.appIcon', 'global')
  const name = useId()
  const current = APP_ICONS.some((i) => i.id === value) ? value : DEFAULT_APP_ICON
  return (
    <>
      <ul className="clu-group clu-sw-icons" role="radiogroup" aria-label="App icon" aria-describedby={error ? `${name}-error` : undefined}>
        {APP_ICONS.map((icon) => (
          <li key={icon.id}>
            <label className="clu-sw-icon-row" data-app-icon={icon.id}>
              <input type="radio" name={name} value={icon.id} aria-label={icon.label} checked={current === icon.id} onChange={() => set(icon.id)} />
              <img className="clu-sw-app-icon" src={ART[icon.id]} alt="" width={64} height={64} draggable={false} />
              <span className="clu-sw-icon-name">
                <span>{icon.label}</span>
                {current === icon.id && <span className="clu-sw-icon-current">Current icon</span>}
              </span>
            </label>
          </li>
        ))}
      </ul>
      {error && (
        <p id={`${name}-error`} className="clu-settings-error" role="alert">
          {error}
        </p>
      )}
    </>
  )
}
