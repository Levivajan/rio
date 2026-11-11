import { resolve } from 'node:path'
import { describe, expect, it } from 'vitest'
import { z } from 'zod'
import { contract } from '../../src/shared/ipc'
import { commandTable, parseChord } from '../../src/shared/commands'
import { encodeWire, WireError, type Wire } from '../../src/shared/mojoWire'
import { COMMANDS_DIR, contractByLane, describe as describeIr, generate, IDC_CLU_FIRST, MOJOM_DIR, snake, vkeyOf, WIRE_FILE, zodToIr } from './gen-mojom'

const files = generate()
const text = (path: string) => {
  const t = files.get(path)
  if (t === undefined) throw new Error(`not generated: ${path}`)
  return t
}

describe('gen-mojom output', () => {
  // The committed files are the snapshots: `npm run gen:chromium` (or vitest -u) rewrites them after a contract change.
  for (const [path, content] of files) {
    it(`matches the committed ${path}`, async () => {
      await expect(content).toMatchFileSnapshot(resolve(path))
    })
  }

  it('writes one mojom per contract module plus bridge and types, and the BUILD.gn lists them all', () => {
    const mojoms = [...files.keys()].filter((p) => p.startsWith(MOJOM_DIR) && p.endsWith('.mojom'))
    const gn = text(`${MOJOM_DIR}/BUILD.gn`)
    for (const p of mojoms) expect(gn).toContain(`"${p.slice(MOJOM_DIR.length + 1)}",`)
    expect(mojoms).toContain(`${MOJOM_DIR}/bridge.mojom`)
    expect(mojoms).toContain(`${MOJOM_DIR}/types.mojom`)
  })

  it('declares every struct, union, enum and interface name once across the module', () => {
    const names = [...files].filter(([p]) => p.endsWith('.mojom')).flatMap(([, t]) => [...t.matchAll(/^(?:struct|union|enum|interface) (\w+)/gm)].map((m) => m[1]))
    expect(names.length).toBeGreaterThan(100)
    expect(new Set(names).size).toBe(names.length)
  })

  it('gives every contract command a method and a wire entry, with its allow list noted for the C++ guard', () => {
    const all = [...files].filter(([p]) => p.endsWith('.mojom')).map(([, t]) => t).join('\n')
    const wire = text(WIRE_FILE)
    for (const [name, def] of Object.entries(contract)) {
      expect(all).toContain(`// ${name}; allow: ${def.allow.join(', ')}.`)
      expect(wire).toContain(`  ${JSON.stringify(name)}: {`)
    }
    expect(contractByLane().flatMap((l) => l.commands).length).toBe(Object.keys(contract).length)
  })

  it('maps the contract shapes the way the TS bindings read them', () => {
    const tabs = text(`${MOJOM_DIR}/tabs.mojom`)
    expect(tabs).toContain('struct TabsMoveInput {\n  string node_id;  // UUID v4\n  DropTarget target;\n  bool? confirmed;\n  bool? ask;\n  bool? drop;\n};')
    expect(tabs).toContain('  Move(TabsMoveInput input) => (CallResult result);')
    expect(text(`${MOJOM_DIR}/types.mojom`)).toContain('union DropTarget {\n  DropTargetFavorite favorite;\n  DropTargetPinned pinned;\n  DropTargetToday today;\n};')
    expect(text(`${MOJOM_DIR}/spaces.mojom`)).toContain('union SpacesSwitchInput {\n  SpacesSwitchInputSpaceId space_id;\n  SpacesSwitchInputIndex index;\n  SpacesSwitchInputStep step;\n};')
    expect(text(`${MOJOM_DIR}/data.mojom`)).toContain('  string value;  // JSON text (the TS Json type), parse with caps')
    // Revs and cursors reach 2^53: double, not int64 (the TS bindings would hand out bigint).
    expect(text(`${MOJOM_DIR}/command.mojom`)).toContain('  double since_rev;  // integer [0, 9007199254740991]')
    expect(text(`${MOJOM_DIR}/core.mojom`)).toContain('  Get() => (CallResult result);')
    // window.* spans two contract modules and still makes one interface.
    expect([...files.values()].join('\n').match(/^interface WindowHandler \{/gm)).toHaveLength(1)
  })

  it('stamps the same contract version into bridge.mojom and the wire table', () => {
    const v = /kContractVersion = "([0-9a-f]{16})"/.exec(text(`${MOJOM_DIR}/bridge.mojom`))?.[1]
    expect(v).toBeTruthy()
    expect(text(WIRE_FILE)).toContain(`export const CONTRACT_VERSION = '${v}'`)
  })
})

describe('zodToIr and naming', () => {
  it('reads caps, ranges, patterns and optional fields', () => {
    const ir = zodToIr(z.object({ a: z.string().max(5), b: z.number().int().min(0).max(9).optional(), c: z.string().regex(/^x$/) }).strict())
    expect(ir).toEqual({
      k: 'object',
      refined: false,
      fields: [
        { name: 'a', optional: false, ty: { k: 'string', max: 5, pattern: null } },
        { name: 'b', optional: true, ty: { k: 'number', int: true, min: 0, max: 9, minOpen: false, maxOpen: false } },
        { name: 'c', optional: false, ty: { k: 'string', max: null, pattern: '^x$' } }
      ]
    })
    expect(describeIr({ k: 'number', int: false, min: 0, max: 360, minOpen: false, maxOpen: true })).toBe('finite [0, 360)')
  })

  it('rejects schemas mojom cannot carry', () => {
    expect(() => zodToIr(z.object({ a: z.string() }))).toThrow(/strict/)
    expect(() => zodToIr(z.string().default('x'))).toThrow(/unsupported/)
    expect(() => zodToIr(z.string().optional())).toThrow(/only supported on object fields/)
    expect(() => zodToIr(z.lazy(() => z.string()))).toThrow(/lazy/)
  })

  it('round-trips field names through mojom snake_case and refuses keywords', () => {
    expect(snake('tabId')).toBe('tab_id')
    expect(snake('copyUrlsMarkdown')).toBe('copy_urls_markdown')
    expect(() => snake('map')).toThrow(/keyword/)
    expect(() => snake('tabID')).toThrow(/round trip/)
  })
})

describe('command table for C++', () => {
  it('has a ui::KeyboardCode for every chord in the table and one IDC per entry', () => {
    for (const e of commandTable) for (const keys of [e.accelerator, ...(e.alt ?? []).map((a) => a.keys)]) if (keys) expect(vkeyOf(parseChord(keys).code)).toMatch(/^VKEY_/)
    const cc = text(`${COMMANDS_DIR}/command_table.cc`)
    expect(cc.match(/^ {4}\{\d+, "/gm)).toHaveLength(commandTable.length)
    expect(cc).toContain(`{${IDC_CLU_FIRST}, "${commandTable[0]?.id}"`)
    expect(cc).toContain('{ui::VKEY_T, ui::EF_COMMAND_DOWN}')
    expect(() => vkeyOf('F13')).toThrow(/KeyboardCode/)
  })
})

describe('encodeWire', () => {
  const wire = (name: keyof typeof contract): Wire => {
    const m = new RegExp(`^  ${JSON.stringify(name).replace('.', '\\.')}: (.*),$`, 'm').exec(text(WIRE_FILE))
    const entry = m?.[1] ? (JSON.parse(m[1]) as { input: Wire | null }) : null
    if (!entry?.input) throw new Error(`no input wire for ${name}`)
    return entry.input
  }

  it('encodes unions by tag, enums by number, Json as text and absent optionals as null', () => {
    expect(encodeWire(wire('tabs.move'), { nodeId: 'n', target: { section: 'pinned', spaceId: 's', parentId: null, index: 2 } })).toEqual({
      nodeId: 'n',
      target: { pinned: { section: 'pinned', spaceId: 's', parentId: null, index: 2 } },
      confirmed: null,
      ask: null,
      drop: null
    })
    expect(encodeWire(wire('theme.setMode'), { mode: 'dark' })).toEqual({ mode: 2 })
    expect(encodeWire(wire('settings.set'), { key: 'k', scope: 'global', value: { a: [1, null] } })).toEqual({ key: 'k', scope: 'global', value: '{"a":[1,null]}' })
    expect(encodeWire(wire('spaces.switch'), { step: -1 })).toEqual({ step: { step: -1 } })
    expect(encodeWire(wire('tabs.bulk'), { tabIds: ['a'], action: 'move', target: { section: 'favorite', index: 0 } })).toEqual({
      move: { tabIds: ['a'], action: 'move', target: { favorite: { section: 'favorite', index: 0 } }, confirmed: null, ask: null, drop: null }
    })
    expect(encodeWire(wire('tabs.bulk'), { tabIds: ['a'], action: 'close' })).toEqual({ action: { tabIds: ['a'], action: 1 } })
  })

  it('throws on shapes the bindings could not encode', () => {
    const bad: [keyof typeof contract, unknown][] = [
      ['theme.setMode', { mode: 'sepia' }],
      ['tabs.activate', {}],
      ['tabs.activate', { tabId: 1 }],
      ['tabs.activate', { tabId: 'a', extra: true }],
      ['spaces.switch', { index: 1.5 }],
      ['spaces.switch', { nope: 1 }],
      ['shell.setSidebarWidth', { width: Number.NaN }],
      ['prompt.respond', { promptId: 'p', action: 'a', values: JSON.parse('{"__proto__":"x"}') as unknown }],
      ['tabs.bulk', { tabIds: 'a', action: 'close' }]
    ]
    for (const [name, payload] of bad) expect(() => encodeWire(wire(name), payload), name).toThrow(WireError)
  })
})
