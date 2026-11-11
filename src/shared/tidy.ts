// Tidy Today (docs/chromium/parity-command.md, the app settings AI tab's ai.tidyTabs): the sidebar's broom puts
// the Today tabs of one site together, the sites with the most tabs first, everything else in its own order. Today
// has no folders in Rio, so the groups have no headings. Pure, so the sidebar and its tests share it.

/** The broom shows when Today holds more than six tabs. */
export const TIDY_MIN_TODAY = 7

export interface TidyTab {
  id: string
  url: string
  splitId?: string | null
}

/** The site a URL belongs to: the host without www, cut to its last two labels (three for "co.uk"-style hosts);
 *  the scheme for anything without a host. */
export function siteKey(url: string): string {
  let u: URL
  try {
    u = new URL(url)
  } catch {
    return 'other:'
  }
  const host = u.hostname.toLowerCase().replace(/^www\./, '').replace(/\.$/, '')
  if (!host) return u.protocol
  if (/^[\d.]+$/.test(host) || host.includes(':')) return host
  const labels = host.split('.')
  const tld = labels.at(-1) ?? ''
  const second = labels.at(-2) ?? ''
  const keep = labels.length >= 3 && tld.length === 2 && second.length <= 3 ? 3 : 2
  return labels.slice(-keep).join('.')
}

/** Today's tabs in tidy order, or null when they already are. Tabs of one split stay together under the site of the
 *  split's first tab. */
export function tidyOrder(tabs: readonly TidyTab[]): string[] | null {
  const splitSite = new Map<string, string>()
  const groups = new Map<string, string[]>()
  for (const t of tabs) {
    let key = siteKey(t.url)
    if (t.splitId) {
      key = splitSite.get(t.splitId) ?? key
      splitSite.set(t.splitId, key)
    }
    const list = groups.get(key)
    if (list) list.push(t.id)
    else groups.set(key, [t.id])
  }
  // Map keeps first-appearance order; the sort is stable, so equal sizes keep it.
  const ordered = [...groups.values()].sort((a, b) => (b.length > 1 ? b.length : 1) - (a.length > 1 ? a.length : 1)).flat()
  return ordered.every((id, i) => id === tabs[i]?.id) ? null : ordered
}
