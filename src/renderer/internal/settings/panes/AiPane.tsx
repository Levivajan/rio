// AI features (docs/chromium/parity-settings.md section 1): one row per feature with Rio's own picture,
// its name, a line on what it does, and a switch for its global setting. The command writer builds the features; this
// tab owns the switches and the keys they read.
import type { ReactNode } from 'react'
import { Toggle } from '../../../common/controls'
import { useSetting } from '../../../settings-sections/data/parts/useSetting'
import { Intro } from './parts'

const W = 94
const H = 62
const art = (bg: string, children: ReactNode) => (
  <svg width={W} height={H} viewBox={`0 0 ${W} ${H}`} aria-hidden="true">
    <rect width={W} height={H} fill={bg} />
    {children}
  </svg>
)

/** Rio's pictures: small scenes of each feature in Rio's own shapes, no other app's art. */
const ART: Record<string, ReactNode> = {
  // A sidebar's loose rows folding into one tidy group.
  'ai.tidyTabs': art(
    '#d9efe3',
    <>
      <rect x="12" y="10" width="30" height="7" rx="3.5" fill="#fff" />
      <rect x="12" y="22" width="24" height="7" rx="3.5" fill="#fff" opacity="0.8" />
      <rect x="12" y="34" width="28" height="7" rx="3.5" fill="#fff" opacity="0.6" />
      <rect x="12" y="46" width="20" height="7" rx="3.5" fill="#fff" opacity="0.4" />
      <path d="M48 31h10m-3-3 3 3-3 3" stroke="#3b8d62" strokeWidth="1.6" fill="none" strokeLinecap="round" strokeLinejoin="round" />
      <rect x="62" y="20" width="22" height="22" rx="6" fill="#3b8d62" />
      <path d="M67 27h12M67 31h12M67 35h8" stroke="#fff" strokeWidth="1.6" strokeLinecap="round" />
    </>
  ),
  // The command bar with Shift and Return keycaps.
  'ai.instantLinks': art(
    '#dce7fb',
    <>
      <rect x="9" y="14" width="76" height="16" rx="8" fill="#fff" />
      <circle cx="18" cy="22" r="3" fill="none" stroke="#4d7ee8" strokeWidth="1.4" />
      <rect x="25" y="20" width="30" height="4" rx="2" fill="#c3d3f4" />
      <rect x="27" y="38" width="18" height="14" rx="3.5" fill="#fff" stroke="#9fb7ea" />
      <text x="36" y="48.5" fontSize="9" textAnchor="middle" fill="#4d7ee8" fontFamily="-apple-system, system-ui">⇧</text>
      <rect x="49" y="38" width="18" height="14" rx="3.5" fill="#fff" stroke="#9fb7ea" />
      <text x="58" y="48.5" fontSize="9" textAnchor="middle" fill="#4d7ee8" fontFamily="-apple-system, system-ui">↩</text>
    </>
  ),
  // A link with a small preview card rising from it.
  'ai.hoverPreviews': art(
    '#fbe3d4',
    <>
      <rect x="10" y="44" width="30" height="3" rx="1.5" fill="#e4885a" />
      <rect x="44" y="44" width="38" height="3" rx="1.5" fill="#f2c4ab" />
      <rect x="20" y="9" width="56" height="28" rx="5" fill="#fff" />
      <rect x="26" y="15" width="30" height="4" rx="2" fill="#e4885a" />
      <rect x="26" y="23" width="44" height="3" rx="1.5" fill="#f2d6c7" />
      <rect x="26" y="29" width="36" height="3" rx="1.5" fill="#f2d6c7" />
      <path d="M30 37l4 5 4-5z" fill="#fff" />
    </>
  ),
  // A pinned row whose long title becomes a short one.
  'ai.tidyTitles': art(
    '#e7defa',
    <>
      <rect x="10" y="12" width="74" height="14" rx="5" fill="#fff" opacity="0.65" />
      <rect x="16" y="17" width="4" height="4" rx="1" fill="#9a83e0" opacity="0.6" />
      <rect x="24" y="17.5" width="54" height="3" rx="1.5" fill="#b7a6ec" />
      <path d="M22 19h58" stroke="#9a83e0" strokeWidth="1" opacity="0.7" />
      <rect x="10" y="36" width="50" height="14" rx="5" fill="#fff" />
      <rect x="16" y="41" width="4" height="4" rx="1" fill="#7a5fd6" />
      <rect x="24" y="41.5" width="28" height="3" rx="1.5" fill="#7a5fd6" />
      <path d="M68 37l1.5 3.5L73 42l-3.5 1.5L68 47l-1.5-3.5L63 42l3.5-1.5z" fill="#7a5fd6" />
    </>
  ),
  // A download chip whose camera-roll name becomes a readable one.
  'ai.tidyDownloads': art(
    '#d5eef0',
    <>
      <rect x="12" y="11" width="70" height="40" rx="6" fill="#fff" />
      <rect x="19" y="18" width="10" height="13" rx="2" fill="#3a9aa5" />
      <rect x="34" y="19" width="40" height="4" rx="2" fill="#2f7f88" />
      <rect x="34" y="27" width="28" height="3" rx="1.5" fill="#b9dfe3" />
      <path d="M33 28.5h30" stroke="#8cc8ce" strokeWidth="1" />
      <rect x="19" y="38" width="55" height="6" rx="3" fill="#e2f3f4" />
      <rect x="19" y="38" width="38" height="6" rx="3" fill="#3a9aa5" />
    </>
  ),
  // The command bar's Ask row with the picked assistant's chip.
  'ai.askRow': art(
    '#dff1d9',
    <>
      <rect x="9" y="12" width="76" height="16" rx="8" fill="#fff" />
      <rect x="15" y="16" width="22" height="8" rx="4" fill="#4f9d45" />
      <rect x="41" y="18.5" width="34" height="3" rx="1.5" fill="#c8e2c2" />
      <rect x="9" y="34" width="76" height="16" rx="8" fill="#fff" opacity="0.7" />
      <circle cx="19" cy="42" r="3" fill="#4f9d45" opacity="0.6" />
      <circle cx="27" cy="42" r="3" fill="#4f9d45" opacity="0.4" />
      <circle cx="35" cy="42" r="3" fill="#4f9d45" opacity="0.25" />
    </>
  )
}

interface Feature { key: string; title: string; text: string }

export const AI_FEATURES: readonly Feature[] = [
  { key: 'ai.tidyTabs', title: 'Tidy Tabs', text: 'When more than six Today tabs pile up, a broom above them sorts the sidebar into groups.' },
  { key: 'ai.instantLinks', title: 'Instant Links', text: 'Press Shift and Return on a search in the command bar to go straight to the top result.' },
  { key: 'ai.hoverPreviews', title: 'Hover Previews', text: 'Hold Shift over a link for a moment to see the page in Peek, with a short summary, before you open it.' },
  { key: 'ai.tidyTitles', title: 'Tidy Titles', text: 'Tabs you pin get shorter, clearer titles. Double-click a title to change it yourself.' },
  { key: 'ai.tidyDownloads', title: 'Tidy Downloads', text: 'Downloads get names that say what they are, so you can find them again later.' },
  { key: 'ai.askRow', title: 'Ask in the Command Bar', text: 'An Ask row sends what you typed to ChatGPT, Claude or Grok. Pick one with the menu on the row.' }
]

function FeatureRow({ feature }: { feature: Feature }) {
  const { value, error, set } = useSetting(feature.key, 'global')
  const id = feature.key.replace('.', '-')
  return (
    <li className="clu-sw-feature" data-feature={feature.key}>
      <span className="clu-sw-feature-art">{ART[feature.key]}</span>
      <div className="clu-sw-feature-text">
        <span id={`${id}-title`} className="clu-sw-feature-title">
          {feature.title}
        </span>
        <p id={`${id}-text`} className="clu-sw-feature-line">
          {feature.text}
        </p>
        {error && (
          <p className="clu-settings-error" role="alert">
            {error}
          </p>
        )}
      </div>
      <Toggle label={feature.title} checked={value === true} onChange={(next) => set(next)} aria-describedby={`${id}-text`} />
    </li>
  )
}

/** The tab's banner: a 684 x 128 band with a wavy foot and sparkles. */
function Banner() {
  const scallops = Array.from({ length: 58 }, () => 'a6 6 0 0 1 12 0').join(' ')
  const sparkle = (x: number, y: number, r: number, fill: string) => (
    <path d={`M${x} ${y - r}C${x + r * 0.18} ${y - r * 0.18} ${x + r * 0.18} ${y - r * 0.18} ${x + r} ${y}C${x + r * 0.18} ${y + r * 0.18} ${x + r * 0.18} ${y + r * 0.18} ${x} ${y + r}C${x - r * 0.18} ${y + r * 0.18} ${x - r * 0.18} ${y + r * 0.18} ${x - r} ${y}C${x - r * 0.18} ${y - r * 0.18} ${x - r * 0.18} ${y - r * 0.18} ${x} ${y - r}Z`} fill={fill} />
  )
  return (
    <div className="clu-sw-hero" aria-hidden="true">
      <svg width="684" height="128" viewBox="0 0 684 128">
        <rect width="684" height="128" fill="#e7defa" />
        {sparkle(342, 58, 34, '#7a5fd6')}
        {sparkle(392, 34, 13, '#9a83e0')}
        {sparkle(296, 86, 10, '#b7a6ec')}
        {sparkle(120, 44, 9, '#c9bcf2')}
        {sparkle(560, 78, 11, '#c9bcf2')}
        {sparkle(470, 26, 6, '#b7a6ec')}
        {sparkle(212, 98, 6, '#b7a6ec')}
        <path d={`M0 128 V123 ${scallops} V128 Z`} className="clu-sw-hero-edge" />
      </svg>
    </div>
  )
}

export function AiPane() {
  return (
    <>
      <Banner />
      <Intro>Tidy Tabs, Tidy Titles, Tidy Downloads and the summaries in Hover Previews come from Apple’s on-device model on this Mac, so what you browse stays on it. Without it (before macOS 26, or with Apple Intelligence off), Rio tidies with its own rules and previews show no summary. The Ask row opens the assistant you pick with what you typed.</Intro>
      <ul className="clu-group clu-sw-features" aria-label="AI features">
        {AI_FEATURES.map((f) => (
          <FeatureRow key={f.key} feature={f} />
        ))}
      </ul>
    </>
  )
}
