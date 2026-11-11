// Wire descriptors for the Mojo bridge (Chromium build, src/renderer/common/bridge/mojo.ts). scripts/chromium/gen-mojom.ts
// writes one per command into src/renderer/common/bridge/mojo-wire.gen.ts from the same zod schemas it turns into
// .mojom, so the TS payload and the generated Mojo WebUI bindings agree: enums travel as their numeric value, unions as
// an object with one camelCase tag set, Json as a string, and an absent optional as null. encodeWire checks shape only;
// the C++ handlers enforce every cap again.

export type Wire =
  | { t: 's' }
  | { t: 'n' }
  | { t: 'i' }
  | { t: 'b' }
  | { t: 'lit'; v: string | number | boolean }
  | { t: 'json' }
  | { t: 'enum'; values: readonly string[] }
  | WireStruct
  | { t: 'union'; variants: readonly { tag: string; wire: WireStruct }[] }
  | { t: 'array'; of: Wire }
  | { t: 'map'; of: Wire }
  | { t: 'null'; of: Wire }
export interface WireStruct { t: 'struct'; fields: readonly WireField[] }
export interface WireField { name: string; wire: Wire; optional?: true }

export interface MojoWireEntry {
  /** Contract module in src/shared/ipc, also the .mojom file name. */
  lane: string
  /** Mojo interface, `TabsHandler` for `tabs.*`. */
  iface: string
  /** Method name as the TS bindings spell it (lowerCamelCase). */
  method: string
  /** null: the method takes no parameter (empty input). */
  input: Wire | null
}

export class WireError extends Error {}

const INT32_MIN = -2147483648
const INT32_MAX = 2147483647

const isPlain = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null && !Array.isArray(v)
const FORBIDDEN = new Set(['__proto__', 'prototype', 'constructor'])

function matches(w: WireStruct, v: unknown): boolean {
  if (!isPlain(v)) return false
  const names = new Set(w.fields.map((f) => f.name))
  if (Object.keys(v).some((k) => !names.has(k))) return false
  return w.fields.every((f) => {
    const x = v[f.name]
    if (f.wire.t === 'lit') return x === f.wire.v
    return x !== undefined || f.optional === true
  })
}

/** Encodes a payload for the generated TS bindings. Throws WireError when the value cannot have the mojom shape. */
export function encodeWire(w: Wire, v: unknown, at = 'payload'): unknown {
  switch (w.t) {
    case 's':
      if (typeof v !== 'string') throw new WireError(`${at}: expected a string`)
      return v
    case 'n':
      if (typeof v !== 'number' || !Number.isFinite(v)) throw new WireError(`${at}: expected a finite number`)
      return v
    case 'i':
      if (typeof v !== 'number' || !Number.isInteger(v) || v < INT32_MIN || v > INT32_MAX) throw new WireError(`${at}: expected an int32`)
      return v
    case 'b':
      if (typeof v !== 'boolean') throw new WireError(`${at}: expected a boolean`)
      return v
    case 'lit':
      if (v !== w.v) throw new WireError(`${at}: expected ${JSON.stringify(w.v)}`)
      return v
    case 'json': {
      const s = JSON.stringify(v ?? null)
      if (s === undefined) throw new WireError(`${at}: not JSON`)
      return s
    }
    case 'enum': {
      const i = typeof v === 'string' ? w.values.indexOf(v) : -1
      if (i < 0) throw new WireError(`${at}: not one of ${w.values.join(', ')}`)
      return i
    }
    case 'struct': {
      if (!isPlain(v)) throw new WireError(`${at}: expected an object`)
      const names = new Set(w.fields.map((f) => f.name))
      for (const k of Object.keys(v)) if (!names.has(k)) throw new WireError(`${at}: unknown key ${k}`)
      const out: Record<string, unknown> = {}
      for (const f of w.fields) {
        const x = v[f.name]
        if (x === undefined && f.optional) out[f.name] = null
        else if (x === undefined) throw new WireError(`${at}.${f.name}: missing`)
        else out[f.name] = encodeWire(f.wire, x, `${at}.${f.name}`)
      }
      return out
    }
    case 'union': {
      const hit = w.variants.find((variant) => matches(variant.wire, v))
      if (!hit) throw new WireError(`${at}: matches no variant`)
      return { [hit.tag]: encodeWire(hit.wire, v, `${at}.${hit.tag}`) }
    }
    case 'array':
      if (!Array.isArray(v)) throw new WireError(`${at}: expected an array`)
      return v.map((x, i) => encodeWire(w.of, x, `${at}[${i}]`))
    case 'map': {
      if (!isPlain(v)) throw new WireError(`${at}: expected a map`)
      const out: Record<string, unknown> = {}
      for (const [k, x] of Object.entries(v)) {
        if (FORBIDDEN.has(k)) throw new WireError(`${at}: forbidden key ${k}`)
        out[k] = encodeWire(w.of, x, `${at}.${k}`)
      }
      return out
    }
    case 'null':
      return v === null || v === undefined ? null : encodeWire(w.of, v, at)
  }
}
