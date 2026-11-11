// Scans string literals in src/renderer and src/shared for banned product names (PLAN 1, Brand), and the UI source for
// the code name Clu: the product is Rio (docs/chromium/rio-rename.md).
import { readdirSync, readFileSync, statSync } from 'node:fs'
import { join, relative } from 'node:path'
import { describe, expect, it } from 'vitest'

const ROOTS = ['src/renderer', 'src/shared']
const BANNED = /\b(Arc|Browser Company|Little Arc|Boosts?|Easel|Arc Max)\b/
const LITERAL = /(['"`])((?:\\.|(?!\1)[^\\\n])*)\1/g
/** Naming the browser an import reads from is a true statement about it, as "Google Chrome" is. */
const ALLOWED = new Set(['Import from Arc'])

function files(dir: string): string[] {
  return readdirSync(dir).flatMap((name) => {
    const p = join(dir, name)
    return statSync(p).isDirectory() ? files(p) : /\.(tsx?|css|html)$/.test(name) ? [p] : []
  })
}

function stripComments(text: string): string {
  return text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/<!--[\s\S]*?-->/g, '').replace(/(^|[^:'"`])\/\/.*$/gm, '$1')
}

describe('brand', () => {
  it('ships no banned names in string literals', () => {
    const hits: string[] = []
    for (const root of ROOTS) {
      for (const file of files(root)) {
        const rel = relative(process.cwd(), file)
        if (rel.endsWith('banned-words.test.ts') || rel.endsWith('commands.test.ts')) continue
        for (const m of stripComments(readFileSync(file, 'utf8')).matchAll(LITERAL)) if (!ALLOWED.has(m[2] ?? '') && BANNED.test(m[2] ?? '')) hits.push(`${rel}: ${m[0].slice(0, 80)}`)
      }
    }
    expect(hits).toEqual([])
  })

  it('shows the product name Rio, never the code name Clu, in UI copy', () => {
    const hits: string[] = []
    for (const root of ROOTS) {
      for (const file of files(root)) {
        const rel = relative(process.cwd(), file)
        if (/\.test\.tsx?$/.test(rel)) continue
        // Whole word and case-sensitive: identifiers (CluBridge, useCluState), clu:// hosts and clu-* classes pass.
        stripComments(readFileSync(file, 'utf8')).split('\n').forEach((line, i) => {
          if (/\bClu\b/.test(line)) hits.push(`${rel}:${i + 1}: ${line.trim().slice(0, 80)}`)
        })
      }
    }
    expect(hits).toEqual([])
  })

  it('never calls the product Chrome in UI copy: only other products keep the name', () => {
    const hits: string[] = []
    for (const root of ROOTS) {
      for (const file of files(root)) {
        const rel = relative(process.cwd(), file)
        if (/\.test\.tsx?$/.test(rel)) continue
        // Whole word and case-sensitive, as scripts/chromium/brand_scan.py: chrome:// URLs, chrome.* APIs and ChromeOS
        // pass, and so do the Chrome Web Store and Google Chrome, the browser Rio imports from.
        stripComments(readFileSync(file, 'utf8')).split('\n').forEach((line, i) => {
          if (/\bChrome\b/.test(line.replace(/Chrome Web Store|Google Chrome/g, ''))) hits.push(`${rel}:${i + 1}: ${line.trim().slice(0, 80)}`)
        })
      }
    }
    expect(hits).toEqual([])
  })
})
