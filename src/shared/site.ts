// siteKey (seeded by the scaffold; docs/plan/08-extensions.md).

/** Lowercase hostname with one leading `www.` removed, for http and https only; null otherwise. */
export function siteKey(input: unknown): string | null {
  if (typeof input !== 'string' || input.length > 32 * 1024) return null
  let parsed: URL
  try {
    parsed = new URL(input)
  } catch {
    return null
  }
  if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') return null
  const host = parsed.hostname.toLowerCase()
  if (!host) return null
  return (host.startsWith('www.') && host.slice(4)) || host
}
