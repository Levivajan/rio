// Grain tile (docs/plan/01-shell.md F9): a 64 px gray noise tile from a seeded PRNG, drawn once into a
// canvas data URL. The window layers it over the theme at grainAlpha (normal blend, so it reads the same on a light
// tint as on a dark one); the empty card takes it as overlay at CARD_GRAIN times that alpha.
export const GRAIN_TILE = 64
/** The empty card's overlay grain against the window's alpha: overlay moves a bright card less than the tint. */
export const CARD_GRAIN = 4.5
/** The window noise: never zero, sd 0.83 levels at the least grain up to
 * about 4.76 at the most, growing a little faster toward the top of the scale. */
export const GRAIN_SD = { min: 0.83, span: 4.18, curve: 1.15 } as const
/** Standard deviation of the tile's uniform 0 to 255 noise, in levels. */
export const TILE_SD = Math.sqrt((256 * 256 - 1) / 12)

/** The window grain layer's opacity for a grain value: the tile at this alpha moves the tint by GRAIN_SD's sd. */
export function grainAlpha(grain: number): number {
  const g = Number.isFinite(grain) ? Math.min(1, Math.max(0, grain)) : 0
  return Math.round(((GRAIN_SD.min + GRAIN_SD.span * g ** GRAIN_SD.curve) / TILE_SD) * 10000) / 10000
}

/** mulberry32: a small seeded PRNG, so every launch draws the same tile. */
export function seededRandom(seed: number): () => number {
  let a = seed >>> 0
  return () => {
    a = (a + 0x6d2b79f5) >>> 0
    let t = a
    t = Math.imul(t ^ (t >>> 15), t | 1)
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61)
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

/** RGBA bytes of the tile: gray noise, opaque. */
export function grainPixels(size = GRAIN_TILE, seed = 0xc10): Uint8ClampedArray<ArrayBuffer> {
  const rand = seededRandom(seed)
  const data = new Uint8ClampedArray(size * size * 4)
  for (let i = 0; i < data.length; i += 4) {
    const v = Math.floor(rand() * 256)
    data[i] = v
    data[i + 1] = v
    data[i + 2] = v
    data[i + 3] = 255
  }
  return data
}

let tile: string | null | undefined

/** The tile if it was made already (the layer stays mounted to fade grain out). */
export const cachedGrainTile = (): string | null => tile ?? null

/** The tile as a data URL, made on first use; null where no 2D canvas exists. */
export function grainTileUrl(): string | null {
  if (tile !== undefined) return tile
  const canvas = document.createElement('canvas')
  canvas.width = GRAIN_TILE
  canvas.height = GRAIN_TILE
  let ctx: CanvasRenderingContext2D | null
  try {
    ctx = canvas.getContext('2d')
  } catch {
    ctx = null
  }
  if (!ctx) return (tile = null)
  ctx.putImageData(new ImageData(grainPixels(), GRAIN_TILE, GRAIN_TILE), 0, 0)
  tile = canvas.toDataURL('image/png')
  return tile
}
