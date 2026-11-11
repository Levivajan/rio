// A tidied title's arrival: the new title writes itself in letter
// by letter over about 340 ms while a few sparkles twinkle over it. CSS animations only; the spans keep their keys, so
// later renders of the row never restart it.
import { TIDY_MORPH_MS } from './tidied'

/** Letters past this share the last delay, so a long title still lands in time. */
const MAX_STAGGERED = 40
const LETTER_MS = 100
const SPARKLES = [12, 46, 78]

export function MorphTitle({ text }: { text: string }) {
  const letters = Array.from(text)
  const step = (TIDY_MORPH_MS - LETTER_MS) / Math.max(1, Math.min(letters.length, MAX_STAGGERED))
  return (
    <span className="clu-row-title clu-morph-title" data-morph="true" aria-label={text}>
      {letters.map((ch, i) => (
        <span key={i} className="clu-morph-letter" aria-hidden="true" style={{ animationDelay: `${Math.round(Math.min(i, MAX_STAGGERED) * step)}ms` }}>
          {ch}
        </span>
      ))}
      {SPARKLES.map((x, i) => (
        <span key={`s${i}`} className="clu-morph-sparkle" aria-hidden="true" style={{ left: `${x}%`, animationDelay: `${i * 70}ms` }} />
      ))}
    </span>
  )
}
