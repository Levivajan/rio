// Divider drag math: pointer travel in DIP to new group sizes. Main clamps to the minimum pane size.
const MIN_SHARE = 0.001

/** Moves the divider between child `index` and `index + 1` by `delta` DIP; the two neighbors trade share. */
export function dragSizes(sizes: readonly number[], index: number, delta: number, avail: number): number[] {
  const a = sizes[index]
  const b = sizes[index + 1]
  if (a === undefined || b === undefined || !Number.isFinite(delta) || !(avail > 0)) return [...sizes]
  const pair = a + b
  const left = Math.min(pair - MIN_SHARE, Math.max(MIN_SHARE, a + delta / avail))
  const out = [...sizes]
  out[index] = left
  out[index + 1] = pair - left
  return out
}
