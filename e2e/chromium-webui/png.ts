// A small PNG reader for the look spec: Playwright's screenshots are 8-bit RGB or RGBA, non-interlaced, which is all
// this decodes. It answers the color at a point in CSS pixels, averaged over a few device pixels.
import { inflateSync } from 'node:zlib'

export interface Png { width: number; height: number; channels: number; data: Uint8Array }

export function readPng(buf: Buffer): Png {
  if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error('not a PNG')
  let off = 8
  let width = 0
  let height = 0
  let channels = 0
  const idat: Buffer[] = []
  while (off < buf.length) {
    const len = buf.readUInt32BE(off)
    const type = buf.toString('latin1', off + 4, off + 8)
    const body = buf.subarray(off + 8, off + 8 + len)
    if (type === 'IHDR') {
      width = body.readUInt32BE(0)
      height = body.readUInt32BE(4)
      const depth = body[8]
      const color = body[9]
      if (depth !== 8 || (color !== 2 && color !== 6) || body[12] !== 0) throw new Error(`unsupported PNG (depth ${depth}, color ${color})`)
      channels = color === 6 ? 4 : 3
    } else if (type === 'IDAT') idat.push(body)
    else if (type === 'IEND') break
    off += 12 + len
  }
  const raw = inflateSync(Buffer.concat(idat))
  const stride = width * channels
  const data = new Uint8Array(height * stride)
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)]
    const src = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1))
    const row = data.subarray(y * stride, (y + 1) * stride)
    const prev = y > 0 ? data.subarray((y - 1) * stride, y * stride) : new Uint8Array(stride)
    for (let i = 0; i < stride; i++) {
      const a = i >= channels ? (row[i - channels] as number) : 0
      const b = prev[i] as number
      const c = i >= channels ? (prev[i - channels] as number) : 0
      const x = src[i] as number
      let v: number
      if (filter === 0) v = x
      else if (filter === 1) v = x + a
      else if (filter === 2) v = x + b
      else if (filter === 3) v = x + ((a + b) >> 1)
      else {
        const p = a + b - c
        const pa = Math.abs(p - a)
        const pb = Math.abs(p - b)
        const pc = Math.abs(p - c)
        v = x + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c)
      }
      row[i] = v & 255
    }
  }
  return { width, height, channels, data }
}

/** The mean color of a 3 x 3 device-pixel patch around the CSS point (x, y). */
export function colorAt(png: Png, x: number, y: number, scale: number): [number, number, number] | null {
  const cx = Math.round(x * scale)
  const cy = Math.round(y * scale)
  const sum = [0, 0, 0]
  let n = 0
  for (let dy = -1; dy <= 1; dy++) {
    for (let dx = -1; dx <= 1; dx++) {
      const px = cx + dx
      const py = cy + dy
      if (px < 0 || py < 0 || px >= png.width || py >= png.height) continue
      const i = (py * png.width + px) * png.channels
      for (let k = 0; k < 3; k++) sum[k] = (sum[k] as number) + (png.data[i + k] as number)
      n++
    }
  }
  return n ? (sum.map((s) => Math.round(s / n)) as [number, number, number]) : null
}
