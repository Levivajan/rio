// Links (docs/chromium/parity-settings.md section 1): where links open. Each switch is a global setting the browser
// reads where the link lands: OpenUrlsFromOtherApp() for links from other apps, InterceptNavigate() and CluPeekThrottle
// for Peek. Above them, a 135 pt band with a drawing of a Mini window and a wavy edge.
import { Card } from '../../../common/controls'
import { Intro, SwitchRow } from './parts'

/** Rio's Mini window over a coral band; the scalloped foot is the pane's own background, so it reads as a torn edge. */
function Hero() {
  const scallops = Array.from({ length: 58 }, () => "a6 6 0 0 1 12 0").join(' ')
  return (
    <div className="clu-sw-hero" aria-hidden="true">
      <svg width="684" height="135" viewBox="0 0 684 135">
        <rect width="684" height="135" fill="#d38384" />
        <g transform="translate(60 47)">
          <rect width="564" height="100" rx="7" fill="#c9c6d3" />
          <rect x="1" y="1" width="562" height="26" rx="6" fill="#d7d4de" />
          <circle cx="11" cy="14" r="3.5" fill="#b0adb9" />
          <circle cx="22" cy="14" r="3.5" fill="#b0adb9" />
          <circle cx="33" cy="14" r="3.5" fill="#b0adb9" />
          <rect x="44" y="5" width="416" height="18" rx="5" fill="#c4c1cc" />
          <rect x="49" y="8.5" width="34" height="11" rx="5.5" fill="#e4e2ea" />
          <rect x="234" y="12" width="56" height="4" rx="2" fill="#8f8c99" />
          <rect x="465" y="5" width="94" height="18" rx="5" fill="#eceaf0" />
          <rect x="472" y="12" width="52" height="4" rx="2" fill="#8f8c99" />
          <rect x="3" y="28" width="558" height="72" fill="#d9a6a6" />
        </g>
        <path d={`M0 135 V130 ${scallops} V135 Z`} className="clu-sw-hero-edge" />
      </svg>
    </div>
  )
}

export function LinksPane() {
  return (
    <>
      <Hero />
      <Intro>A Mini window is a small, simple Rio window for a quick look at a link, like one a friend sent you.</Intro>
      <Card>
        <SwitchRow settingKey="links.miniFromOtherApps" label="Links from other apps open in a Mini window" />
      </Card>
      <hr className="clu-sw-rule" />
      <Intro>Peek shows a link over the page without opening a new tab, for a quick look at a link from an email, an article or a post.</Intro>
      <Card>
        <SwitchRow settingKey="links.peekOnShiftClick" label="Open a Peek when clicking links with Shift held" />
        <SwitchRow settingKey="links.peekOtherSites" label="Open a Peek when clicking links to other sites" note={<em className="clu-sw-note">Note: This only affects Favorites and Pinned tabs</em>} />
      </Card>
    </>
  )
}
