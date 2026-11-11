// zod-to-mojom (docs/chromium/PLAN.md 1.3 "Bridge"): turns the zod contract in src/shared/ipc/*.ts into
// chromium/clu/common/mojom/<lane>.mojom (one interface per area), the TS wire table the MojoBridge encodes with
// (src/renderer/common/bridge/mojo-wire.gen.ts), and the command table for C++ (chromium/clu/browser/commands).
// Zod stays the single source of truth. Run `npm run gen:chromium`; `-- --check` fails when a file is stale.
import { createHash } from 'node:crypto'
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { ERROR_CODES, SURFACES, type CommandDef } from '../../src/shared/ipc/define'
import { contract } from '../../src/shared/ipc'
import { anchor, commandResult, dropTarget, spaceIcon } from '../../src/shared/ipc/common'
import { point, rect } from '../../src/shared/schema'
import { spaceThemeSchema, themeColorSchema } from '../../src/shared/theme/schema'
import { tweakControls } from '../../src/shared/tweaks'
import { commandTable, parseChord, type CommandEntry } from '../../src/shared/commands'
import type { Wire, WireField, WireStruct } from '../../src/shared/mojoWire'
import { lookTokenFiles } from './gen-look'

// ---- zod to IR -------------------------------------------------------------------------------

export type Ir =
  | { k: 'string'; max: number | null; pattern: string | null }
  | { k: 'number'; int: boolean; min: number | null; max: number | null; minOpen: boolean; maxOpen: boolean }
  | { k: 'bool' }
  | { k: 'literal'; value: string | number | boolean }
  | { k: 'enum'; values: string[] }
  | { k: 'object'; fields: IrField[]; refined: boolean }
  | { k: 'union'; options: Ir[] }
  | { k: 'array'; of: Ir; min: number | null; max: number | null }
  | { k: 'record'; key: Ir; value: Ir }
  | { k: 'nullable'; of: Ir }
  | { k: 'json' }
export interface IrField { name: string; ty: Ir; optional: boolean }

interface ZCheckDef { check: string; format?: string; pattern?: RegExp; maximum?: number; minimum?: number; value?: number | bigint; inclusive?: boolean }
interface ZDef {
  type: string
  checks?: { _zod: { def: ZCheckDef } }[]
  shape?: Record<string, ZLike>
  catchall?: ZLike
  options?: ZLike[]
  values?: unknown[]
  entries?: Record<string, string | number>
  innerType?: ZLike
  element?: ZLike
  keyType?: ZLike
  valueType?: ZLike
  getter?: () => ZLike
  in?: ZLike
  out?: ZLike
}
interface ZLike { _zod: { def: ZDef } }

const checksOf = (d: ZDef) => (d.checks ?? []).map((c) => c._zod.def)

export function zodToIr(schema: unknown, at = 'schema'): Ir {
  const d = (schema as ZLike)._zod?.def
  if (!d) throw new Error(`${at}: not a zod schema`)
  switch (d.type) {
    case 'string': {
      let max: number | null = null
      let pattern: string | null = null
      for (const c of checksOf(d)) {
        if (c.check === 'max_length' && typeof c.maximum === 'number') max = c.maximum
        else if (c.check === 'string_format' && c.format === 'regex' && c.pattern) pattern = c.pattern.source
      }
      return { k: 'string', max, pattern }
    }
    case 'number': {
      const ir = { k: 'number' as const, int: false, min: null as number | null, max: null as number | null, minOpen: false, maxOpen: false }
      for (const c of checksOf(d)) {
        if (c.check === 'number_format' && (c.format === 'safeint' || c.format === 'int32')) ir.int = true
        else if (c.check === 'greater_than') {
          ir.min = Number(c.value)
          ir.minOpen = c.inclusive === false
        } else if (c.check === 'less_than') {
          ir.max = Number(c.value)
          ir.maxOpen = c.inclusive === false
        }
      }
      return ir
    }
    case 'boolean':
      return { k: 'bool' }
    case 'literal': {
      const values = d.values ?? []
      const one = (v: unknown): Ir => {
        if (typeof v === 'string' || typeof v === 'number' || typeof v === 'boolean') return { k: 'literal', value: v }
        throw new Error(`${at}: unsupported literal ${String(v)}`)
      }
      return values.length === 1 ? one(values[0]) : { k: 'union', options: values.map(one) }
    }
    case 'enum': {
      const values = Object.values(d.entries ?? {})
      if (!values.every((v): v is string => typeof v === 'string')) throw new Error(`${at}: numeric enums are not supported`)
      return { k: 'enum', values }
    }
    case 'object': {
      if (d.catchall?._zod.def.type !== 'never') throw new Error(`${at}: objects must be .strict()`)
      const fields = Object.entries(d.shape ?? {}).map(([name, s]) => {
        const inner = (s as ZLike)._zod.def
        const optional = inner.type === 'optional'
        return { name, optional, ty: zodToIr(optional ? inner.innerType : s, `${at}.${name}`) }
      })
      return { k: 'object', fields, refined: checksOf(d).some((c) => c.check === 'custom') }
    }
    case 'union':
      return { k: 'union', options: (d.options ?? []).map((o, i) => zodToIr(o, `${at}|${i}`)) }
    case 'array': {
      let min: number | null = null
      let max: number | null = null
      for (const c of checksOf(d)) {
        if (c.check === 'min_length' && typeof c.minimum === 'number') min = c.minimum
        else if (c.check === 'max_length' && typeof c.maximum === 'number') max = c.maximum
      }
      return { k: 'array', of: zodToIr(d.element, `${at}[]`), min, max }
    }
    case 'record':
      return { k: 'record', key: zodToIr(d.keyType, `${at}{key}`), value: zodToIr(d.valueType, `${at}{}`) }
    case 'nullable':
      return { k: 'nullable', of: zodToIr(d.innerType, at) }
    case 'pipe':
      // finite().pipe(z.number().min(0).max(1)): the output side carries the range.
      return zodToIr(d.out, at)
    case 'lazy': {
      // The one recursive schema in the contract is json() (src/shared/schema.ts): a union with null in it.
      const inner = d.getter?.()._zod.def
      if (inner?.type === 'union' && inner.options?.some((o) => o._zod.def.type === 'null')) return { k: 'json' }
      throw new Error(`${at}: unsupported lazy schema`)
    }
    case 'optional':
      throw new Error(`${at}: optional is only supported on object fields`)
    default:
      throw new Error(`${at}: unsupported zod type ${d.type}`)
  }
}

// ---- naming ----------------------------------------------------------------------------------

const MOJOM_KEYWORDS = new Set(['import', 'module', 'struct', 'union', 'interface', 'enum', 'const', 'true', 'false', 'default', 'array', 'map', 'associated', 'handle', 'pending_remote', 'pending_receiver', 'pending_associated_remote', 'pending_associated_receiver', 'feature'])

export const pascal = (s: string) =>
  s
    .split(/[^A-Za-z0-9]+/)
    .filter(Boolean)
    .map((w) => w[0]?.toUpperCase() + w.slice(1))
    .join('')

/** `tabId` to `tab_id`. The TS bindings camelCase it back (TypeScriptStylizer.StylizeField), checked here. */
export function snake(name: string): string {
  const out = name.replace(/([a-z0-9])([A-Z])/g, '$1_$2').toLowerCase()
  if (camel(out) !== name) throw new Error(`field ${name} does not survive the mojom round trip (${out})`)
  if (MOJOM_KEYWORDS.has(out)) throw new Error(`field ${name} is a mojom keyword`)
  return out
}
const camel = (s: string) => s.replace(/_([a-z0-9])/g, (_m, c: string) => c.toUpperCase())

const enumValueName = (v: string) => {
  const p = pascal(v)
  if (!/^[A-Za-z][A-Za-z0-9]*$/.test(p)) throw new Error(`enum value ${v} has no mojom name`)
  return `k${p}`
}

/** Tag names for unions whose variants have neither a literal discriminator nor a key of their own. */
const VARIANT_NAMES: Record<string, string[]> = { TabsBulkInput: ['action', 'move'] }

// ---- shared named types ----------------------------------------------------------------------

const sig = (ir: Ir) => JSON.stringify(ir)

/** Types several commands use, emitted once in types.mojom under these names. */
export const NAMED_TYPES: [name: string, schema: unknown][] = [
  ['Rect', rect()],
  ['Point', point()],
  ['Anchor', anchor()],
  ['DropTarget', dropTarget()],
  ['SpaceIcon', spaceIcon()],
  ['ThemeColor', themeColorSchema()],
  ['SpaceTheme', spaceThemeSchema()],
  ['CommandResult', commandResult()],
  ['TweakControls', tweakControls()]
]

// ---- IR to mojom and wire ----------------------------------------------------------------------

interface Decl { name: string; file: string; text: string; sig: string }

class Emitter {
  readonly decls = new Map<string, Decl>()
  private readonly named = new Map<string, string>()
  constructor(named: [string, Ir][]) {
    for (const [name, ir] of named) this.named.set(sig(ir), name)
    for (const [name, ir] of named) this.declare(ir, name, 'types', true)
  }

  /** Mojom type expression for `ir`, declaring structs, unions and enums it needs in `file`. */
  type(ir: Ir, hint: string, file: string): string {
    const shared = this.named.get(sig(ir))
    if (shared) return shared
    switch (ir.k) {
      case 'string':
      case 'json':
        return 'string'
      case 'number':
        return numberType(ir)
      case 'bool':
        return 'bool'
      case 'literal':
        return typeof ir.value === 'string' ? 'string' : typeof ir.value === 'boolean' ? 'bool' : Number.isInteger(ir.value) ? 'int32' : 'double'
      case 'enum':
      case 'object':
        return this.declare(ir, hint, file)
      case 'union': {
        const kind = unionKind(ir)
        if (kind === 'string') return 'string'
        if (kind === 'int32' || kind === 'double') return kind
        return this.declare(kind === 'enum' ? { k: 'enum', values: ir.options.map((o) => String((o as { value: string }).value)) } : ir, hint, file)
      }
      case 'array':
        return `array<${this.type(ir.of, `${hint}Item`, file)}>`
      case 'record':
        if (this.type(ir.key, `${hint}Key`, file) !== 'string') throw new Error(`${hint}: map keys must be strings`)
        return `map<string, ${this.type(ir.value, `${hint}Value`, file)}>`
      case 'nullable': {
        const inner = this.type(ir.of, hint, file)
        if (inner.endsWith('?')) throw new Error(`${hint}: nested nullable`)
        return `${inner}?`
      }
    }
  }

  private declare(ir: Ir, name: string, file: string, force = false): string {
    const s = sig(ir)
    const prev = this.decls.get(name)
    if (prev) {
      if (prev.sig !== s) throw new Error(`type name ${name} is used for two shapes`)
      return name
    }
    if (!force) {
      const shared = this.named.get(s)
      if (shared) return shared
    }
    const slot: Decl = { name, file, text: '', sig: s }
    this.decls.set(name, slot)
    if (ir.k === 'enum') {
      const names = ir.values.map(enumValueName)
      if (new Set(names).size !== names.length) throw new Error(`${name}: enum values collide`)
      slot.text = `enum ${name} {\n${ir.values.map((v, i) => `  ${names[i]} = ${i},  // "${v}"`).join('\n')}\n};`
    } else if (ir.k === 'object') {
      const lines = ir.fields.map((f) => {
        const t = this.type(f.ty, name + pascal(f.name), file)
        const ty = f.optional && !t.endsWith('?') ? `${t}?` : t
        const note = describe(f.ty)
        return `  ${ty} ${snake(f.name)};${note ? `  // ${note}` : ''}`
      })
      const head = ir.refined ? '// Refined in TS (a cross-field rule): port it to the C++ handler.\n' : ''
      slot.text = `${head}struct ${name} {\n${lines.join('\n')}\n};`
    } else if (ir.k === 'union') {
      const tags = variantTags(ir, name, (o) => this.named.get(sig(o)))
      const lines = ir.options.map((o, i) => `  ${this.type(o, name + pascal(tags[i] ?? ''), file)} ${snake(tags[i] ?? '')};`)
      slot.text = `union ${name} {\n${lines.join('\n')}\n};`
    } else throw new Error(`${name}: only enums, structs and unions are declared`)
    // Move to the end so a type follows the types it uses.
    this.decls.delete(name)
    this.decls.set(name, slot)
    return name
  }

  /** C++ IsValid() overloads for every declared struct and union: the zod caps that mojom types cannot carry. */
  cppValidators(): { decls: string[]; defs: string[]; patterns: string[] } {
    const declCount = this.decls.size
    const patterns: string[] = []
    const patternFn = (p: string) => {
      if (!patterns.includes(p)) patterns.push(p)
      return `MatchesPattern${patterns.indexOf(p)}`
    }
    const decls: string[] = []
    const defs: string[] = []
    for (const d of [...this.decls.values()]) {
      const ir = JSON.parse(d.sig) as Ir
      if (ir.k === 'enum') continue
      const body: string[] = []
      if (ir.k === 'object') {
        if (ir.refined) body.push('  // Refined in TS (a cross-field rule): the handler checks it.')
        for (const f of ir.fields) {
          const inner = f.ty.k === 'nullable' ? f.ty.of : f.ty
          const at = `value.${snake(f.name)}`
          body.push(...(f.optional || f.ty.k === 'nullable' ? this.cppNullable(inner, at, 1, patternFn) : this.cppChecks(inner, at, 1, patternFn)))
        }
      } else if (ir.k === 'union') {
        const tags = variantTags(ir, d.name, (o) => this.named.get(sig(o)))
        for (const tag of tags) {
          const t = snake(tag)
          body.push(`  if (value.is_${t}()) {`, `    return value.get_${t}() && IsValid(*value.get_${t}());`, '  }')
        }
        body.push('  return false;')
      } else throw new Error(`${d.name}: only structs, unions and enums are declared`)
      if (ir.k === 'object') body.push('  return true;')
      decls.push(`bool IsValid(const mojom::${d.name}& value);`)
      defs.push(`bool IsValid(const mojom::${d.name}& value) {\n${body.join('\n')}\n}`)
    }
    if (this.decls.size !== declCount) throw new Error('cppValidators declared a new mojom type')
    return { decls, defs, patterns }
  }

  /** Statements checking a nullable value: absent passes, present passes the same checks as a required one. */
  private cppNullable(ir: Ir, at: string, depth: number, patternFn: (p: string) => string): string[] {
    const pad = '  '.repeat(depth)
    if (ir.k === 'object' || (ir.k === 'union' && unionKind(ir) === 'union')) {
      return [`${pad}if (${at} && !IsValid(*${at})) {`, `${pad}  return false;`, `${pad}}`]
    }
    const inner = this.cppChecks(ir, `*${at}`, depth + 1, patternFn)
    return inner.length ? [`${pad}if (${at}) {`, ...inner, `${pad}}`] : []
  }

  /** Statements checking a required value `at` of the C++ type mojo uses for `ir`; empty when mojo already checks it. */
  private cppChecks(ir: Ir, at: string, depth: number, patternFn: (p: string) => string): string[] {
    const pad = '  '.repeat(depth)
    const negate = (cond: string) => (/ (&&|\|\||==|>=|<=|>|<) /.test(cond) ? `!(${cond})` : `!${cond}`)
    const fail = (cond: string) => [`${pad}if (${negate(cond)}) {`, `${pad}  return false;`, `${pad}}`]
    if (ir.k === 'object' || (ir.k === 'union' && unionKind(ir) === 'union')) {
      return fail(`${at} && IsValid(*${at})`)
    }
    if (ir.k === 'array') {
      const size = at.startsWith('*') ? `${at.slice(1)}->size()` : `${at}.size()`
      const conds = [ir.min ? `${size} >= ${ir.min}u` : '', ir.max !== null ? `${size} <= ${ir.max}u` : ''].filter(Boolean)
      const item = `item${depth}`
      const each = ir.of.k === 'nullable' ? this.cppNullable(ir.of.of, item, depth + 1, patternFn) : this.cppChecks(ir.of, item, depth + 1, patternFn)
      return [...(conds.length ? fail(conds.join(' && ')) : []), ...(each.length ? [`${pad}for (const auto& ${item} : ${at}) {`, ...each, `${pad}}`] : [])]
    }
    if (ir.k === 'record') {
      const key = `key${depth}`
      const item = `item${depth}`
      const keyChecks = this.cppChecks(ir.key, key, depth + 1, patternFn)
      const each = ir.value.k === 'nullable' ? this.cppNullable(ir.value.of, item, depth + 1, patternFn) : this.cppChecks(ir.value, item, depth + 1, patternFn)
      if (!keyChecks.length && !each.length) return []
      return [`${pad}for (const auto& [${key}, ${item}] : ${at}) {`, ...keyChecks, ...each, `${pad}}`]
    }
    if (ir.k === 'nullable') throw new Error('nested nullable')
    const cond = this.cppCondition(ir, at, patternFn)
    return cond ? fail(cond) : []
  }

  /** A C++ condition on a scalar (string, number, bool, enum) value, or '' when there is nothing to check. */
  private cppCondition(ir: Ir, at: string, patternFn: (p: string) => string): string {
    switch (ir.k) {
      case 'string': {
        if (ir.pattern === UUID_SOURCE) return `IsUuidV4(${at})`
        const conds = [ir.max !== null ? `IsUtf16LengthAtMost(${at}, ${ir.max}u)` : `IsValidUtf8(${at})`]
        if (ir.pattern) conds.push(`${patternFn(ir.pattern)}(${at})`)
        return conds.join(' && ')
      }
      case 'json':
        return `IsJsonText(${at})`
      case 'number': {
        const double = numberType(ir) === 'double'
        const conds = double ? [`std::isfinite(${at})`, ...(ir.int ? [`IsWholeNumber(${at})`] : [])] : []
        if (ir.min !== null) conds.push(`${at} ${ir.minOpen ? '>' : '>='} ${ir.min}`)
        if (ir.max !== null) conds.push(`${at} ${ir.maxOpen ? '<' : '<='} ${ir.max}`)
        return conds.join(' && ')
      }
      case 'literal':
        return `${at} == ${typeof ir.value === 'string' ? JSON.stringify(ir.value) : String(ir.value)}`
      case 'union': {
        const kind = unionKind(ir)
        if (kind === 'enum') return ''
        const options = ir.options.map((o) => this.cppCondition(o, at, patternFn))
        return options.some((c) => !c) ? '' : options.map((c) => `(${c})`).join(' || ')
      }
      default:
        return ''
    }
  }

  wire(ir: Ir, hint: string): Wire {
    switch (ir.k) {
      case 'string':
        return { t: 's' }
      case 'number':
        return { t: numberType(ir) === 'int32' ? 'i' : 'n' }
      case 'bool':
        return { t: 'b' }
      case 'json':
        return { t: 'json' }
      case 'literal':
        return { t: 'lit', v: ir.value }
      case 'enum':
        return { t: 'enum', values: ir.values }
      case 'object':
        return this.wireStruct(ir, hint)
      case 'union': {
        const kind = unionKind(ir)
        if (kind === 'string') return { t: 's' }
        if (kind === 'int32') return { t: 'i' }
        if (kind === 'double') return { t: 'n' }
        if (kind === 'enum') return { t: 'enum', values: ir.options.map((o) => String((o as { value: string }).value)) }
        const name = this.named.get(sig(ir)) ?? hint
        const tags = variantTags(ir, name, (o) => this.named.get(sig(o)))
        return {
          t: 'union',
          variants: ir.options.map((o, i) => {
            const w = this.wire(o, name + pascal(tags[i] ?? ''))
            if (w.t !== 'struct') throw new Error(`${name}: union variants must be objects`)
            return { tag: camel(snake(tags[i] ?? '')), wire: w }
          })
        }
      }
      case 'array':
        return { t: 'array', of: this.wire(ir.of, `${hint}Item`) }
      case 'record':
        return { t: 'map', of: this.wire(ir.value, `${hint}Value`) }
      case 'nullable':
        return { t: 'null', of: this.wire(ir.of, hint) }
    }
  }

  private wireStruct(ir: Extract<Ir, { k: 'object' }>, hint: string): WireStruct {
    const fields: WireField[] = ir.fields.map((f) => ({ name: f.name, wire: this.wire(f.ty, hint + pascal(f.name)), ...(f.optional ? { optional: true as const } : {}) }))
    return { t: 'struct', fields }
  }
}

const INT32 = 2 ** 31
/** Integers inside int32 become int32. Wider ones (cursors, revs up to 2^53) become double: the TS bindings map
 *  int64 to bigint, which JSON and the UI do not use. The C++ handler checks they are whole numbers. */
function numberType(ir: Extract<Ir, { k: 'number' }>): 'int32' | 'double' {
  return ir.int && ir.min !== null && ir.max !== null && ir.min >= -INT32 && ir.max < INT32 ? 'int32' : 'double'
}

function unionKind(ir: Extract<Ir, { k: 'union' }>): 'string' | 'int32' | 'double' | 'enum' | 'union' {
  const o = ir.options
  if (o.every((x) => x.k === 'literal' && typeof x.value === 'string')) return 'enum'
  if (o.every((x) => x.k === 'literal' && typeof x.value === 'number')) return o.every((x) => Number.isInteger((x as { value: number }).value)) ? 'int32' : 'double'
  if (o.every((x) => x.k === 'string' || (x.k === 'literal' && typeof x.value === 'string'))) return 'string'
  if (o.every((x) => x.k === 'object')) return 'union'
  throw new Error('unions must be all objects, all string-like or all number literals')
}

/** A variant's tag: its string literal discriminator, else the shared type it is, else a key only it has. */
function variantTags(ir: Extract<Ir, { k: 'union' }>, name: string, sharedName: (o: Ir) => string | undefined): string[] {
  const fixed = VARIANT_NAMES[name]
  if (fixed) {
    if (fixed.length !== ir.options.length) throw new Error(`${name}: VARIANT_NAMES has ${fixed.length} names for ${ir.options.length} variants`)
    return fixed
  }
  const keysOf = (o: Ir) => (o.k === 'object' ? o.fields.map((f) => f.name) : [])
  const tags = ir.options.map((o, i) => {
    const shared = sharedName(o)
    if (shared) return shared[0]?.toLowerCase() + shared.slice(1)
    const lit = o.k === 'object' ? o.fields.find((f) => f.ty.k === 'literal' && typeof f.ty.value === 'string') : undefined
    if (lit && lit.ty.k === 'literal') return String(lit.ty.value)
    const own = keysOf(o).find((k) => ir.options.every((p, j) => j === i || !keysOf(p).includes(k)))
    if (own) return own
    throw new Error(`${name}: variant ${i} needs a name in VARIANT_NAMES`)
  })
  if (new Set(tags).size !== tags.length) throw new Error(`${name}: variant tags collide (${tags.join(', ')})`)
  return tags
}

/** One-line constraint note the C++ handler must enforce (mojom types carry none of these). */
export function describe(ir: Ir): string {
  switch (ir.k) {
    case 'string':
      if (ir.pattern === UUID_SOURCE) return 'UUID v4'
      return [ir.max !== null ? `<= ${ir.max} chars` : '', ir.pattern ? `matches /${ir.pattern}/` : ''].filter(Boolean).join(', ')
    case 'number': {
      const kind = ir.int ? (numberType(ir) === 'double' ? 'integer' : '') : 'finite'
      const lo = ir.min === null ? '' : `${ir.minOpen ? '(' : '['}${ir.min}`
      const hi = ir.max === null ? '' : `${ir.max}${ir.maxOpen ? ')' : ']'}`
      return [kind, lo || hi ? `${lo || '(-inf'}, ${hi || 'inf)'}` : ''].filter(Boolean).join(' ')
    }
    case 'literal':
      return `== ${JSON.stringify(ir.value)}`
    case 'union': {
      const kind = unionKind(ir)
      if (kind === 'union' || kind === 'enum') return ''
      return `one of ${ir.options.map((o) => (o.k === 'literal' ? JSON.stringify(o.value) : describe(o) || o.k)).join(' | ')}`
    }
    case 'array': {
      const n = ir.min !== null || ir.max !== null ? `${ir.min ?? 0}..${ir.max ?? 'n'} items` : ''
      const each = describe(ir.of)
      return [n, each ? `each ${each}` : ''].filter(Boolean).join(', ')
    }
    case 'record': {
      const k = describe(ir.key)
      const v = describe(ir.value)
      return [k ? `keys ${k}` : '', v ? `values ${v}` : ''].filter(Boolean).join(', ')
    }
    case 'nullable':
      return describe(ir.of)
    case 'json':
      return 'JSON text (the TS Json type), parse with caps'
    default:
      return ''
  }
}
const UUID_SOURCE = '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$'

// ---- whole contract ----------------------------------------------------------------------------

export interface LaneCommands { lane: string; commands: [name: string, def: CommandDef][] }

/** The contract grouped by its src/shared/ipc/<lane>.ts module, in contract order. */
export function contractByLane(): LaneCommands[] {
  const modules = import.meta.glob<Record<string, unknown>>(['../../src/shared/ipc/*.ts', '!../../src/shared/ipc/*.test.ts'], { eager: true })
  const laneOf = new Map<string, string>()
  for (const [file, mod] of Object.entries(modules)) {
    const lane = file.replace(/^.*\/(\w+)\.ts$/, '$1')
    for (const [exportName, value] of Object.entries(mod)) {
      if (!exportName.endsWith('Ipc') || typeof value !== 'object' || value === null) continue
      for (const name of Object.keys(value)) {
        if (laneOf.has(name)) throw new Error(`${name} is defined in two contract modules`)
        laneOf.set(name, lane)
      }
    }
  }
  const lanes = new Map<string, LaneCommands>()
  for (const [name, def] of Object.entries(contract) as [string, CommandDef][]) {
    const lane = laneOf.get(name)
    if (!lane) throw new Error(`${name} is in the contract but in no src/shared/ipc module`)
    if (!lanes.has(lane)) lanes.set(lane, { lane, commands: [] })
    lanes.get(lane)?.commands.push([name, def])
  }
  return [...lanes.values()]
}

const HEADER = '// Generated by scripts/chromium/gen-mojom.ts from src/shared/ipc. Do not edit; run `npm run gen:chromium`.'
export const MOJOM_DIR = 'chromium/clu/common/mojom'
export const WIRE_FILE = 'src/renderer/common/bridge/mojo-wire.gen.ts'
export const COMMANDS_DIR = 'chromium/clu/browser/commands'
export const BRIDGE_DIR = 'chromium/clu/browser/ui/webui/bridge'
export const MOJOM_RESOURCES_DIR = 'chromium/clu/resources/mojom'

const isEmpty = (ir: Ir) => ir.k === 'object' && ir.fields.length === 0
const areaOf = (name: string) => name.slice(0, name.indexOf('.'))
const verbOf = (name: string) => name.slice(name.indexOf('.') + 1)
export const ifaceOf = (name: string) => `${pascal(areaOf(name))}Handler`

/** Every generated file, path relative to the repo root, to content. */
export function generate(): Map<string, string> {
  const named = NAMED_TYPES.map(([n, s]) => [n, zodToIr(s, n)] as [string, Ir])
  const em = new Emitter(named)
  const lanes = contractByLane()
  // One interface per area, in the file of the lane that defines the area first (window.* spans core and spaces).
  const homeOf = new Map<string, string>()
  for (const { lane, commands } of lanes) for (const [name] of commands) if (!homeOf.has(areaOf(name))) homeOf.set(areaOf(name), lane)
  const methods = new Map<string, string[]>()
  const cppMethods: CppMethod[] = []
  const wire: string[] = []
  for (const { commands } of lanes) {
    for (const [name, def] of commands) {
      const area = areaOf(name)
      const home = homeOf.get(area) ?? area
      const ir = zodToIr(def.input, name)
      const base = pascal(area) + pascal(verbOf(name))
      // The TS bindings lowercase the first letter, and a method named `new` in a TS interface is a construct
      // signature, so window.new becomes NewWindow.
      const method = verbOf(name) === 'new' ? `New${pascal(area)}` : pascal(verbOf(name))
      const inputType = isEmpty(ir) ? null : em.type(ir, `${base}Input`, home)
      const param = inputType ? `${inputType} input` : ''
      cppMethods.push({ name, iface: ifaceOf(name), lane: home, method, inputType, allow: def.allow })
      if (!methods.has(area)) methods.set(area, [])
      methods.get(area)?.push(`  // ${name}; allow: ${def.allow.join(', ')}.\n  ${method}(${param}) => (CallResult result);`)
      const input = isEmpty(ir) ? null : em.wire(ir, `${base}Input`)
      wire.push(`  ${JSON.stringify(name)}: ${JSON.stringify({ lane: home, iface: ifaceOf(name), method: method[0]?.toLowerCase() + method.slice(1), input })},`)
    }
  }
  const ifaceText = new Map<string, string[]>()
  for (const [area, lines] of methods) {
    const home = homeOf.get(area) ?? area
    ifaceText.set(home, [...(ifaceText.get(home) ?? []), `interface ${pascal(area)}Handler {\n${lines.join('\n\n')}\n};`])
  }

  const files = new Map<string, string>()
  const mojomFile = (body: string[], imports: string[]) =>
    [HEADER, '', 'module clu.mojom;', '', ...imports.map((i) => `import "clu/common/mojom/${i}.mojom";`), ...(imports.length ? [''] : []), body.join('\n\n'), ''].join('\n')
  const declsIn = (file: string) => [...em.decls.values()].filter((d) => d.file === file).map((d) => d.text)

  files.set(`${MOJOM_DIR}/types.mojom`, mojomFile(declsIn('types'), []))
  const laneFiles: string[] = []
  for (const { lane } of lanes) {
    const body = [...declsIn(lane), ...(ifaceText.get(lane) ?? [])]
    if (body.length === 0) continue
    laneFiles.push(lane)
    const usesTypes = body.some((t) => named.some(([n]) => new RegExp(`\\b${n}\\b`).test(t)))
    files.set(`${MOJOM_DIR}/${lane}.mojom`, mojomFile(body, ['bridge', ...(usesTypes ? ['types'] : [])]))
  }
  const version = createHash('sha256').update([...files.values()].join('\n')).digest('hex').slice(0, 16)
  files.set(`${MOJOM_DIR}/bridge.mojom`, bridgeMojom(version))
  const sources = ['bridge', 'types', ...laneFiles]
  files.set(`${MOJOM_DIR}/BUILD.gn`, buildGn(sources))
  files.set(`${MOJOM_RESOURCES_DIR}/BUILD.gn`, mojomResourcesGn(sources))
  files.set(
    WIRE_FILE,
    [
      HEADER,
      "import type { CommandName } from '../../../shared/ipc'",
      "import type { MojoWireEntry } from '../../../shared/mojoWire'",
      '',
      '/** Hash of the generated .mojom; the browser reports its own in SurfaceInfo and a mismatch refuses to connect. */',
      `export const CONTRACT_VERSION = '${version}'`,
      '',
      `/** The contract modules, each served as \`/mojom/<lane>.mojom-webui.js\` by both Clu WebUIs. */`,
      `export const MOJO_MODULES = ${JSON.stringify(sources)} as const`,
      '',
      'export const MOJO_WIRE: Record<CommandName, MojoWireEntry> = {',
      ...wire,
      '}',
      ''
    ].join('\n')
  )
  for (const [path, text] of commandTableFiles(commandTable)) files.set(path, text)
  for (const [path, text] of lookTokenFiles()) files.set(path, text)
  for (const [path, text] of bridgeCppFiles(cppMethods, laneFiles, em.cppValidators())) files.set(path, text)
  return files
}

function bridgeMojom(version: string): string {
  const codes = ERROR_CODES.map((c, i) => `  ${enumValueName(c)} = ${i},  // "${c}"`).join('\n')
  return `${HEADER}

// The envelope and state channel every Clu WebUI uses (docs/PLAN.md 5.1, docs/chromium/m2-ui-bridge.md).
module clu.mojom;

// Hash of the generated lane and type files. SurfaceInfo carries the browser's copy; the WebUI refuses a mismatch.
const string kContractVersion = "${version}";

enum ErrorCode {
${codes}
};

struct CallError {
  ErrorCode code;
  string? message;
  string? reason;  // set with kRejected: a lane-defined reason
};

// Every command answers CallResult. value_json is the JSON text of the TS output type (Output<name> in
// src/shared/ipc.ts), "null" for commands that return nothing.
union CallResult {
  string value_json;
  CallError error;
};

enum SurfaceLayer {
  kTop = 0,
  kUnderPeek = 1,
  kPassive = 2,
};

// Who this WebUI is, decided by the browser (never by the page). window_id is null for internal pages.
struct SurfaceInfo {
  string? window_id;
  SurfaceLayer? layer;  // overlay WebUIs only
  int32? slot;  // passive overlay slot 0..3
  string contract_version;
};

// Browser to WebUI. message_json is one StateMessage (src/shared/state.ts): a snapshot first, then patches with
// rev + 1 each, coalesced per 16 ms; patches of a rolled-back transaction are never sent.
interface StateObserver {
  OnState(string message_json);
};

// Bound for chrome://clu-frame and chrome://clu-overlay. Subscribe once per document; the snapshot follows.
interface StateSource {
  Subscribe(pending_remote<StateObserver> observer) => (SurfaceInfo info);
};
`
}

function buildGn(sources: string[]): string {
  return `${HEADER.replace(/^\/\//, '#')}

import("//mojo/public/tools/bindings/mojom.gni")

mojom("mojom") {
  sources = [
${sources.map((s) => `    "${s}.mojom",`).join('\n')}
  ]
  webui_module_path = "/mojom"
  generate_legacy_js_bindings = false
}
`
}

/** The JS of the generated TS bindings, served by both Clu WebUIs at /mojom/<file>.mojom-webui.js. */
function mojomResourcesGn(sources: string[]): string {
  return `${HEADER.replace(/^\/\//, '#')}

import("//ui/webui/resources/tools/build_webui.gni")

# clu.mojom's TS bindings compiled to JS, packed as clu_mojom_resources (kCluMojomResources) for the frame and
# overlay data sources.
build_webui("build") {
  grd_prefix = "clu_mojom"
  mojo_files_deps = [ "//clu/common/mojom:mojom_ts__generator" ]
  mojo_files = [
${sources.map((s) => `    "$root_gen_dir/clu/common/mojom/${s}.mojom-webui.ts",`).join('\n')}
  ]
  mojo_base_path = "mojom"
  ts_deps = [ "//ui/webui/resources/mojo:build_ts" ]
  webui_context_type = "trusted"
  grit_output_dir = "$root_gen_dir/clu"
}
`
}

// ---- command table for C++ (PLAN 1.3 "Keys") --------------------------------------------------

const VKEYS: Record<string, string> = {
  BracketLeft: 'VKEY_OEM_4', BracketRight: 'VKEY_OEM_6', Equal: 'VKEY_OEM_PLUS', Minus: 'VKEY_OEM_MINUS', Period: 'VKEY_OEM_PERIOD',
  Comma: 'VKEY_OEM_COMMA', Slash: 'VKEY_OEM_2', Semicolon: 'VKEY_OEM_1', Quote: 'VKEY_OEM_7', Backquote: 'VKEY_OEM_3', Backslash: 'VKEY_OEM_5',
  ArrowLeft: 'VKEY_LEFT', ArrowRight: 'VKEY_RIGHT', ArrowUp: 'VKEY_UP', ArrowDown: 'VKEY_DOWN', Tab: 'VKEY_TAB', Backspace: 'VKEY_BACK',
  Enter: 'VKEY_RETURN', Escape: 'VKEY_ESCAPE', Space: 'VKEY_SPACE'
}
export function vkeyOf(code: string): string {
  const m = /^(?:Key([A-Z])|Digit([0-9]))$/.exec(code)
  if (m) return `VKEY_${m[1] ?? m[2]}`
  const named = VKEYS[code]
  if (!named) throw new Error(`no ui::KeyboardCode for ${code}`)
  return named
}

/** First IDC_CLU_* value. Chromium's own IDC_* stay below 60000 in 158 (chrome/app/chrome_command_ids.h). */
export const IDC_CLU_FIRST = 61000

const cstr = (s: string) => JSON.stringify(s).replace(/[\u0080-￿]/g, (c) => `\\u${c.charCodeAt(0).toString(16).padStart(4, '0')}`)

function accel(keys: string): string {
  const c = parseChord(keys)
  const mods = [c.meta && 'ui::EF_COMMAND_DOWN', c.ctrl && 'ui::EF_CONTROL_DOWN', c.alt && 'ui::EF_ALT_DOWN', c.shift && 'ui::EF_SHIFT_DOWN'].filter(Boolean)
  return `{ui::${vkeyOf(c.code)}, ${mods.length ? mods.join(' | ') : 'ui::EF_NONE'}}`
}

export function commandTableFiles(table: readonly CommandEntry[]): [string, string][] {
  const head = `${HEADER}\n// The PLAN 8 key table: route kRouter chords are reserved (pages never see them), kMenu chords are main-menu key\n// equivalents (pages get them first). A gtest checks this table against the browser's accelerators.`
  const h = `${head}

#ifndef CLU_BROWSER_COMMANDS_COMMAND_TABLE_H_
#define CLU_BROWSER_COMMANDS_COMMAND_TABLE_H_

#include "base/containers/span.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace clu {

inline constexpr int IDC_CLU_FIRST = ${IDC_CLU_FIRST};
inline constexpr int IDC_CLU_LAST = ${IDC_CLU_FIRST + table.length - 1};

enum class CommandRoute { kRouter, kMenu };
enum class CommandVariant { kMain, kMini, kBoth };
enum class CommandMenu { kFile, kEdit, kView, kHistory, kTabs, kSpaces, kWindow, kHelp };

struct CommandAccelerator {
  ui::KeyboardCode key;
  int modifiers;  // ui::EF_* flags
};

struct CommandAlt {
  CommandAccelerator accelerator;
  CommandRoute route;
};

struct CommandSpec {
  int idc;  // IDC_CLU_FIRST + index
  const char* id;  // the TS id, sent back as commands.run({ id })
  const char* title;
  CommandMenu menu;
  CommandVariant variant;
  CommandRoute route;
  bool palette;
  bool has_accelerator;
  CommandAccelerator accelerator;
  // RAW_PTR_EXCLUSION: #global-scope, a constexpr array in command_table.cc.
  RAW_PTR_EXCLUSION base::span<const CommandAlt> alts;
};

base::span<const CommandSpec> CommandTable();

}  // namespace clu

#endif  // CLU_BROWSER_COMMANDS_COMMAND_TABLE_H_
`
  const route = (r: string) => (r === 'router' ? 'CommandRoute::kRouter' : 'CommandRoute::kMenu')
  const alts: string[] = []
  const rows = table.map((e, i) => {
    let altRef = '{}'
    if (e.alt?.length) {
      const name = `kAlts${i}`
      alts.push(`constexpr CommandAlt ${name}[] = {${e.alt.map((a) => `{${accel(a.keys)}, ${route(a.route ?? e.route)}}`).join(', ')}};`)
      altRef = name
    }
    return `    {${IDC_CLU_FIRST + i}, ${cstr(e.id)}, ${cstr(e.title)}, CommandMenu::k${e.menu}, CommandVariant::k${pascal(e.variant)}, ${route(e.route)}, ${e.palette}, ${!!e.accelerator}, ${e.accelerator ? accel(e.accelerator) : '{ui::VKEY_UNKNOWN, ui::EF_NONE}'}, ${altRef}},`
  })
  const cc = `${head}

#include "clu/browser/commands/command_table.h"

namespace clu {

namespace {

${alts.join('\n')}

}  // namespace

base::span<const CommandSpec> CommandTable() {
  static constexpr CommandSpec kTable[] = {
${rows.join('\n')}
  };
  return kTable;
}

}  // namespace clu
`
  return [
    [`${COMMANDS_DIR}/command_table.h`, h],
    [`${COMMANDS_DIR}/command_table.cc`, cc]
  ]
}

// ---- C++ guard and validators (docs/chromium/m2-shell.md) ------------------------------------

interface CppMethod { name: string; iface: string; lane: string; method: string; inputType: string | null; allow: readonly string[] }

const surfaceConst = (s: string) => `kSurface${pascal(s)}`
const cppGuard = (path: string) => `${path.replace(/^chromium\//, '').toUpperCase().replace(/[^A-Z0-9]/g, '_')}_`

/** bridge_validators.{h,cc} and bridge_handlers.{h,cc}: the per-call guard every Clu WebUI handler sits behind. */
export function bridgeCppFiles(methods: readonly CppMethod[], lanes: readonly string[], validators: { decls: string[]; defs: string[]; patterns: string[] }): [string, string][] {
  const ifaces: { iface: string; lane: string; methods: CppMethod[] }[] = []
  for (const m of methods) {
    let entry = ifaces.find((i) => i.iface === m.iface)
    if (!entry) ifaces.push((entry = { iface: m.iface, lane: m.lane, methods: [] }))
    entry.methods.push(m)
  }
  const surfaces = SURFACES.filter((s) => methods.some((m) => m.allow.includes(s)))
  if (methods.some((m) => m.allow.some((s) => !surfaces.includes(s as (typeof SURFACES)[number])))) throw new Error('an allow list names an unknown surface')
  const field = (iface: string) => snake(iface[0]?.toLowerCase() + iface.slice(1))
  const mojomIncludes = (suffix: string) => [...new Set(['bridge', 'types', ...lanes])].sort().map((l) => `#include "clu/common/mojom/${l}.mojom${suffix}.h"`)
  const sig = (m: CppMethod) => `${m.inputType ? `mojom::${m.inputType}Ptr input, ` : ''}${m.method}Callback callback`
  const valH = `${BRIDGE_DIR}/bridge_validators.h`
  const valCc = `${BRIDGE_DIR}/bridge_validators.cc`
  const hndH = `${BRIDGE_DIR}/bridge_handlers.h`
  const hndCc = `${BRIDGE_DIR}/bridge_handlers.cc`

  const validatorsH = `${HEADER}
// IsValid() for every struct and union in clu.mojom: the zod caps that mojom types cannot carry (string lengths,
// UUIDs, patterns, numeric ranges, item counts, JSON text). Enums need no check: mojo rejects unknown values.

#ifndef ${cppGuard(valH)}
#define ${cppGuard(valH)}

${mojomIncludes('-forward').join('\n')}

namespace clu {

${validators.decls.join('\n')}

}  // namespace clu

#endif  // ${cppGuard(valH)}
`
  const patternDefs = validators.patterns.map(
    (p, i) => `bool MatchesPattern${i}(std::string_view value) {
  static const base::NoDestructor<re2::RE2> kPattern(R"re(${p})re");
  return re2::RE2::FullMatch(value, *kPattern);
}`
  )
  const validatorsCc = `${HEADER}

#include "${valH.replace(/^chromium\//, '')}"

#include <cmath>
#include <string_view>

#include "base/no_destructor.h"
#include "clu/browser/ui/webui/bridge/validation.h"
${mojomIncludes('').join('\n')}
#include "third_party/re2/src/re2/re2.h"

namespace clu {
${patternDefs.length ? `\nnamespace {\n\n${patternDefs.join('\n\n')}\n\n}  // namespace\n` : ''}
${validators.defs.join('\n\n')}

}  // namespace clu
`

  const defaultClass = (i: (typeof ifaces)[number]) => `// Answers kNotImplemented for every ${i.iface} method. Implementations derive from it and override the
// commands they handle.
class Default${i.iface} : public mojom::${i.iface} {
 public:
  Default${i.iface}();
  Default${i.iface}(const Default${i.iface}&) = delete;
  Default${i.iface}& operator=(const Default${i.iface}&) = delete;
  ~Default${i.iface}() override;

  // mojom::${i.iface}:
${i.methods.map((m) => `  void ${m.method}(${sig(m)}) override;`).join('\n')}
};`
  const handlersH = `${HEADER}
// One WebUI document's ends of the ${ifaces.length} handler interfaces. Every call passes the guard first: the calling surface
// must be on the command's allow list (kNotAllowed), then the payload must pass bridge_validators.h (kBadPayload).
// Only then does it reach the implementation. An interface without one answers kNotImplemented.

#ifndef ${cppGuard(hndH)}
#define ${cppGuard(hndH)}

#include <cstdint>
#include <memory>

#include "base/memory/raw_ptr.h"
${mojomIncludes('').join('\n')}
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_ui_controller_interface_binder.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"

namespace clu {

// The surfaces allow lists name (src/shared/ipc/define.ts), as bits.
${surfaces.map((s, i) => `inline constexpr uint32_t ${surfaceConst(s)} = 1u << ${i};`).join('\n')}

${ifaces.map(defaultClass).join('\n\n')}

class BridgeReceivers {
 public:
  // The implementations one surface provides. A null entry answers kNotImplemented for the whole interface.
  struct Impls {
    Impls();
    Impls(const Impls&);
    Impls& operator=(const Impls&);
    ~Impls();

${ifaces.map((i) => `    raw_ptr<mojom::${i.iface}> ${field(i.iface)};`).join('\n')}
  };

  // \`surface\` is one kSurface* bit: who is calling, decided by the browser.
  BridgeReceivers(uint32_t surface, const Impls& impls);
  BridgeReceivers(const BridgeReceivers&) = delete;
  BridgeReceivers& operator=(const BridgeReceivers&) = delete;
  ~BridgeReceivers();

${ifaces.map((i) => `  void Bind(mojo::PendingReceiver<mojom::${i.iface}> receiver);`).join('\n')}

 private:
${ifaces.map((i) => `  std::unique_ptr<mojom::${i.iface}> ${field(i.iface)}_default_;\n  std::unique_ptr<mojom::${i.iface}> ${field(i.iface)}_guard_;\n  mojo::ReceiverSet<mojom::${i.iface}> ${field(i.iface)}_receivers_;`).join('\n')}
};

// Registers a binder for every handler interface. Each binds only in a document whose WebUIController is one of
// \`Controllers\`, which forward to their BridgeReceivers.
template <typename... Controllers>
void RegisterBridgeBinders(
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map) {
${ifaces.map((i) => `  content::RegisterWebUIControllerInterfaceBinder<mojom::${i.iface},\n                                                  Controllers...>(map);`).join('\n')}
}

}  // namespace clu

#endif  // ${cppGuard(hndH)}
`
  const allowExpr = (m: CppMethod) => m.allow.map((s) => surfaceConst(s)).join(' | ')
  const guardMethod = (i: (typeof ifaces)[number], m: CppMethod) => {
    const lines = [`void Guarded${i.iface}::${m.method}(${sig(m)}) {`, `  if (!(surface_ & (${allowExpr(m)}))) {`, `    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kNotAllowed, "${m.name}"));`, '    return;', '  }']
    if (m.inputType) lines.push('  if (!input || !IsValid(*input)) {', `    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kBadPayload, "${m.name}"));`, '    return;', '  }')
    lines.push(`  impl_->${m.method}(${m.inputType ? 'std::move(input), ' : ''}std::move(callback));`, '}')
    return lines.join('\n')
  }
  const guardedClass = (i: (typeof ifaces)[number]) => `class Guarded${i.iface} : public mojom::${i.iface} {
 public:
  Guarded${i.iface}(uint32_t surface, mojom::${i.iface}* impl)
      : surface_(surface), impl_(impl) {}
  Guarded${i.iface}(const Guarded${i.iface}&) = delete;
  Guarded${i.iface}& operator=(const Guarded${i.iface}&) = delete;
  ~Guarded${i.iface}() override = default;

  // mojom::${i.iface}:
${i.methods.map((m) => `  void ${m.method}(${sig(m)}) override;`).join('\n')}

 private:
  const uint32_t surface_;
  const raw_ptr<mojom::${i.iface}> impl_;
};

${i.methods.map((m) => guardMethod(i, m)).join('\n\n')}`
  const defaultDefs = (i: (typeof ifaces)[number]) =>
    [
      `Default${i.iface}::Default${i.iface}() = default;`,
      `Default${i.iface}::~Default${i.iface}() = default;`,
      ...i.methods.map((m) => `void Default${i.iface}::${m.method}(${sig(m)}) {\n  std::move(callback).Run(ErrorResult(mojom::ErrorCode::kNotImplemented, "${m.name}"));\n}`)
    ].join('\n\n')
  const ctorInit = ifaces.map((i) => {
    const f = field(i.iface)
    return `  if (!impls.${f}) {\n    ${f}_default_ = std::make_unique<Default${i.iface}>();\n  }\n  ${f}_guard_ = std::make_unique<Guarded${i.iface}>(\n      surface, impls.${f} ? impls.${f}.get() : ${f}_default_.get());`
  })
  const handlersCc = `${HEADER}

#include "${hndH.replace(/^chromium\//, '')}"

#include <utility>

#include "clu/browser/ui/webui/bridge/bridge_validators.h"
#include "clu/browser/ui/webui/bridge/call_result.h"

namespace clu {

namespace {

${ifaces.map(guardedClass).join('\n\n')}

}  // namespace

${ifaces.map(defaultDefs).join('\n\n')}

BridgeReceivers::Impls::Impls() = default;
BridgeReceivers::Impls::Impls(const Impls&) = default;
BridgeReceivers::Impls& BridgeReceivers::Impls::operator=(const Impls&) = default;
BridgeReceivers::Impls::~Impls() = default;

BridgeReceivers::BridgeReceivers(uint32_t surface, const Impls& impls) {
${ctorInit.join('\n')}
}

BridgeReceivers::~BridgeReceivers() = default;

${ifaces.map((i) => `void BridgeReceivers::Bind(mojo::PendingReceiver<mojom::${i.iface}> receiver) {\n  ${field(i.iface)}_receivers_.Add(${field(i.iface)}_guard_.get(), std::move(receiver));\n}`).join('\n\n')}

}  // namespace clu
`
  return [
    [valH, validatorsH],
    [valCc, validatorsCc],
    [hndH, handlersH],
    [hndCc, handlersCc]
  ]
}

// ---- CLI -------------------------------------------------------------------------------------

export function main(args: string[], root = process.cwd()): number {
  const check = args.includes('--check')
  const stale: string[] = []
  for (const [path, text] of generate()) {
    const abs = resolve(root, path)
    let current: string | null
    try {
      current = readFileSync(abs, 'utf8')
    } catch {
      current = null
    }
    if (current === text) continue
    if (check) stale.push(path)
    else {
      mkdirSync(dirname(abs), { recursive: true })
      writeFileSync(abs, text)
      console.log(`wrote ${path}`)
    }
  }
  if (stale.length) {
    console.error(`stale generated files (run npm run gen:chromium):\n  ${stale.join('\n  ')}`)
    return 1
  }
  return 0
}
