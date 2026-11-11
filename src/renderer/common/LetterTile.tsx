// Favicon fallback: the first letter of the host or title on a tinted square.
export function letterFor(text: string): string {
  const host = text.replace(/^[a-z]+:\/\//i, '').replace(/^www\./i, '')
  const first = Array.from(host.trim())[0] ?? '?'
  return /\p{L}|\p{N}/u.test(first) ? first.toUpperCase() : '?'
}

export function LetterTile({ text, size = 16 }: { text: string; size?: number }) {
  return (
    <span
      aria-hidden="true"
      className="clu-letter-tile"
      style={{ width: size, height: size, fontSize: Math.round(size * 0.62), lineHeight: `${size}px` }}
    >
      {letterFor(text)}
    </span>
  )
}
