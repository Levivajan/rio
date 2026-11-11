// Tests the Chromium patch pipeline without a Chromium build. apply.sh, the string
// rewrite and the build and sync dry runs run against a small git fixture made from the pinned checkout's HEAD
// (read only, through git archive), so they skip where there is no checkout, as in CI.
import { execFileSync, spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { chmodSync, cpSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync, utimesSync, writeFileSync } from 'node:fs'
import { homedir, tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { pngSize } from '../make-icon.mjs'
import { LOGOS, MONO, VERSION_LOGOS, WORDMARKS, clipToCircle, monoSvg, vectorIcon } from './make-branding.mjs'

const REPO = resolve('.')
const SCRIPTS = join(REPO, 'scripts', 'chromium')
// One patch per upstream file (PLAN 2.2), so the counts below are the same number.
const PATCHES = readdirSync(join(REPO, 'chromium', 'patches')).filter((f) => f.endsWith('.patch')).length
const UPSTREAM = process.env.CLU_CHROMIUM_UPSTREAM ?? join(homedir(), 'Developer', 'clu-chromium.noindex', 'src')
const HAS_CHECKOUT = existsSync(join(UPSTREAM, '.git')) && existsSync(join(UPSTREAM, 'tools', 'grit'))
const MAC = process.platform === 'darwin'
const BRAND_SVG = readFileSync(join(REPO, 'assets', 'brand', 'clu-icon.svg'), 'utf8')
/** The brand icon with its wave fills (flat colors or multiplied gradients) and white wave lines taken out. */
const WITHOUT_WAVES = BRAND_SVG.replaceAll(/Z" fill="(?:#[\dA-Fa-f]{6}|url\(#[^)]+\)" style="mix-blend-mode:multiply)"/g, 'Z" fill="none"').replaceAll('stroke="#fff"', 'stroke="#eee"')
const shellcheck = [process.env.SHELLCHECK, ...(process.env.PATH ?? '').split(':').map((d) => join(d, 'shellcheck'))].find(
  (p) => p && existsSync(p)
)

/** Runs a script with a clean Clu env plus `env`; returns { status, out } with stdout and stderr together. */
function run(script, args, env = {}, cwd = REPO) {
  const base = { ...process.env, GIT_OPTIONAL_LOCKS: '0' }
  delete base.CLU_CHROMIUM_ROOT
  delete base.CLU_CHROMIUM_SRC
  // sign-dev.sh reads ~/.clu/package.env for what the environment leaves unset; the tests bring their own.
  base.CLU_PACKAGE_ENV = join(tmpdir(), 'clu-no-package.env')
  const r = spawnSync(script, args, { cwd, env: { ...base, ...env }, encoding: 'utf8', timeout: 120_000 })
  return { status: r.status, out: `${r.stdout}${r.stderr}` }
}
const git = (dir, ...args) => execFileSync('git', ['-C', dir, ...args], { encoding: 'utf8', env: { ...process.env, GIT_OPTIONAL_LOCKS: '0' } })
const status = (dir) => git(dir, 'status', '--porcelain', '--untracked-files=all').split('\n').filter(Boolean)
const sha = (path) => createHash('sha256').update(readFileSync(path)).digest('hex')
const mtime = (path) => statSync(path, { bigint: true }).mtimeNs
// A fixed past mtime for the files apply.sh recorded, so a rerun that rewrites a file shows up.
const OLD = new Date('2001-09-09T01:46:40.123Z')
const ageRecorded = (src) => {
  const paths = readFileSync(join(src, '.clu-applied'), 'utf8').split('\n').filter((l) => l && !l.startsWith('#')).map((l) => l.split('\t')[2])
  for (const p of paths) utimesSync(join(src, p), OLD, OLD)
  return new Map(paths.map((p) => [p, mtime(join(src, p))]))
}

/** A minimal .pak (version 5, UTF-8) holding `strings` as { id: text }. */
function pak(strings) {
  const ids = Object.keys(strings).map(Number).sort((a, b) => a - b)
  const bodies = ids.map((id) => Buffer.from(strings[id], 'utf8'))
  const head = Buffer.alloc(12 + 6 * (ids.length + 1))
  head.writeUInt32LE(5, 0)
  head.writeUInt32LE(1, 4)
  head.writeUInt16LE(ids.length, 8)
  head.writeUInt16LE(0, 10)
  let offset = head.length
  ids.forEach((id, i) => {
    head.writeUInt16LE(id, 12 + 6 * i)
    head.writeUInt32LE(offset, 14 + 6 * i)
    offset += bodies[i].length
  })
  head.writeUInt16LE(0, 12 + 6 * ids.length)
  head.writeUInt32LE(offset, 14 + 6 * ids.length)
  return Buffer.concat([head, ...bodies])
}

describe('chromium pipeline: repo checks', () => {
  it('keeps the patch set inside the budget (PLAN 2.2)', () => {
    const r = run(join(SCRIPTS, 'apply.sh'), ['--stats'])
    expect(r.out).toMatch(new RegExp(`${PATCHES} patches, ${PATCHES} patched upstream files \\(budget 60\\), \\d+ patched lines \\(budget 1500\\)`))
    expect(r.status).toBe(0)
  })

  it('names each patch after the one upstream file it changes', () => {
    for (const name of execFileSync('ls', [join(REPO, 'chromium', 'patches')], { encoding: 'utf8' }).split('\n').filter(Boolean)) {
      const targets = [...readFileSync(join(REPO, 'chromium', 'patches', name), 'utf8').matchAll(/^\+\+\+ b\/(.+)$/gm)].map((m) => m[1])
      expect(targets).toHaveLength(1)
      expect(name).toBe(`${targets[0].replaceAll('/', '-')}.patch`)
    }
  })

  it.skipIf(!shellcheck)('has shellcheck-clean scripts', () => {
    const files = ['apply.sh', 'build.sh', 'check-brand.sh', 'common.sh', 'sign-dev.sh', 'sync.sh'].map((f) => join(SCRIPTS, f))
    expect(run(shellcheck, ['-x', ...files]).out).toBe('')
  })

  it('refuses to sign without the identity, the profile and an app, and names each (docs/chromium/m9.md)', () => {
    const r = run(join(SCRIPTS, 'sign-dev.sh'), ['--dry-run', join(tmpdir(), 'no-such', 'Rio.app')], {
      CLU_SIGN_IDENTITY: '',
      CLU_SIGN_PROFILE: join(tmpdir(), 'no-such', 'Rio.provisionprofile')
    })
    expect(r.status).toBe(1)
    expect(r.out).toContain('no app bundle at')
    expect(r.out).toContain('set CLU_SIGN_IDENTITY')
    expect(r.out).toContain('no provisioning profile at')
    expect(r.out).toContain('--entitlements <entitlements>')
  })

  it.runIf(MAC)('refuses a provisioning profile made for another App ID', () => {
    const dir = mkdtempSync(join(tmpdir(), 'clu-sign-'))
    try {
      const app = join(dir, 'Rio.app')
      mkdirSync(join(app, 'Contents', 'MacOS'), { recursive: true })
      execFileSync('plutil', ['-create', 'xml1', join(app, 'Contents', 'Info.plist')])
      execFileSync('plutil', ['-insert', 'CFBundleIdentifier', '-string', 'org.example.rio', join(app, 'Contents', 'Info.plist')])
      // A stand-in for security(1): the profile is a plain plist here, and the identity is in the keychain.
      const bin = join(dir, 'bin')
      mkdirSync(bin)
      writeFileSync(
        join(bin, 'security'),
        '#!/bin/sh\nif [ "$1" = cms ]; then cat "$4"; else echo \'  1) ABC "Developer ID Application: Test (ABCDE12345)"\'; fi\n'
      )
      chmodSync(join(bin, 'security'), 0o755)
      const profile = (appId) => {
        const path = join(dir, `${appId}.provisionprofile`)
        execFileSync('plutil', ['-create', 'xml1', path])
        execFileSync('plutil', ['-insert', 'TeamIdentifier', '-array', path])
        execFileSync('plutil', ['-insert', 'TeamIdentifier.0', '-string', 'ABCDE12345', path])
        execFileSync('plutil', ['-insert', 'Entitlements', '-dictionary', path])
        execFileSync('plutil', ['-insert', 'Entitlements.com\\.apple\\.application-identifier', '-string', appId, path])
        execFileSync('plutil', ['-insert', 'Entitlements.com\\.apple\\.developer\\.web-browser\\.public-key-credential', '-bool', 'YES', path])
        return path
      }
      const sign = (appId) =>
        run(join(SCRIPTS, 'sign-dev.sh'), ['--dry-run', app], {
          CLU_SIGN_IDENTITY: 'Developer ID Application: Test (ABCDE12345)',
          CLU_SIGN_PROFILE: profile(appId),
          PATH: `${bin}:${process.env.PATH}`
        })
      const old = sign('ABCDE12345.org.example.clu')
      expect(old.status).toBe(1)
      expect(old.out).toContain("the profile is for the App ID 'ABCDE12345.org.example.clu', expected ABCDE12345.org.example.rio")
      const right = sign('ABCDE12345.org.example.rio')
      expect(right.out).not.toContain('would refuse')
      expect(right.status).toBe(0)
    } finally {
      rmSync(dir, { recursive: true, force: true })
    }
  })

  it('pins the tag and the branding source', () => {
    expect(readFileSync(join(REPO, 'chromium', 'VERSION'), 'utf8')).toBe('158.0.8127.18\n')
    expect(readFileSync(join(REPO, 'chromium', 'branding', 'clu-icon.sha256'), 'utf8').split(' ')[0]).toBe(
      createHash('sha256').update(BRAND_SVG).digest('hex')
    )
  })

  it('ships every generated logo at its upstream pixel size', () => {
    const out = join(REPO, 'chromium', 'branding', 'clu')
    for (const { path, size } of [...LOGOS, MONO]) expect(pngSize(readFileSync(join(out, path)))).toEqual({ width: size, height: size })
    for (const { path, scale } of WORDMARKS) expect(pngSize(readFileSync(join(out, path)))).toEqual({ width: 60 * scale, height: 22 * scale })
    for (const { path, scale } of VERSION_LOGOS) expect(pngSize(readFileSync(join(out, path)))).toEqual({ width: 171 * scale, height: 32 * scale })
    for (const f of ['app.icns', 'Assets.car']) expect(existsSync(join(out, 'chrome/app/theme/chromium/mac', f))).toBe(true)
  })

  it('converts the brand orb to a vector icon and rejects a file without one', () => {
    const icon = vectorIcon(BRAND_SVG)
    expect(icon).toContain('CANVAS_DIMENSIONS, 24,')
    // The rim, the core and the three wave bands.
    expect(icon.match(/PATH_COLOR_ARGB/g)).toHaveLength(5)
    expect(icon.match(/CIRCLE, /g)).toHaveLength(2)
    expect(icon.match(/MOVE_TO/g)).toHaveLength(3)
    // Every point stays on the 24 dp canvas.
    for (const [, x, y] of icon.matchAll(/(?:MOVE|LINE)_TO, ([\d.-]+), ([\d.-]+),/g)) {
      expect(Math.hypot(Number(x) - 12, Number(y) - 12)).toBeLessThanOrEqual(10.51)
    }
    expect(icon).toBe(readFileSync(join(REPO, 'chromium/branding/clu/components/vector_icons/chromium/product.icon'), 'utf8'))
    expect(() => vectorIcon(BRAND_SVG.replace(/<clipPath id="[^"]*o(?:rb)?"><circle/, '<clipPath id="x"><circle'))).toThrow(/no orb/)
    expect(() => vectorIcon(WITHOUT_WAVES)).toThrow(/no wave fills/)
  })

  it('clips a polygon to a circle', () => {
    const square = [[-2, -2], [2, -2], [2, 2], [-2, 2]]
    const clipped = clipToCircle(square, 0, 0, 1)
    expect(clipped.length).toBeGreaterThan(8)
    for (const [x, y] of clipped) expect(Math.hypot(x, y)).toBeLessThanOrEqual(1 + 1e-9)
    expect(clipToCircle([[5, 5], [6, 5], [6, 6]], 0, 0, 1)).toEqual([])
  })

  it('draws the status icon as the black orb with its wave lines cut out', () => {
    const mono = monoSvg(BRAND_SVG)
    expect(mono).not.toMatch(/(?:fill|stroke)="#(?!000000|FFFFFF)/i)
    expect(mono.match(/<mask id="m">.*?<\/mask>/s)[0].match(/stroke="#000000"/g)).toHaveLength(3)
    expect(mono).toMatch(/<circle [^>]*mask="url\(#m\)"/)
    expect(() => monoSvg(WITHOUT_WAVES)).toThrow(/no wave lines/)
  })

  it('reads the orb and waves of the gradient artwork too (Sunset, now an alternate)', () => {
    const airySun = readFileSync(join(REPO, 'assets', 'brand', 'app-icons', 'rio-airySun.svg'), 'utf8')
    expect(vectorIcon(airySun).match(/MOVE_TO/g)).toHaveLength(3)
    expect(monoSvg(airySun).match(/<mask id="m">.*?<\/mask>/s)[0].match(/stroke="#000000"/g)).toHaveLength(3)
  })
})

describe('chromium pipeline: brand scan', () => {
  let dir
  beforeAll(() => {
    dir = mkdtempSync(join(tmpdir(), 'clu-brand-'))
  })
  afterAll(() => rmSync(dir, { recursive: true, force: true }))

  const scan = (app) => run('python3', ['-B', join(SCRIPTS, 'brand_scan.py'), app], { CLU_BRAND_WORDS: 'Acme' })
  const app = (name, locales) => {
    const root = join(dir, name, 'Contents', 'Resources')
    for (const [lang, strings] of Object.entries(locales)) {
      mkdirSync(join(root, `${lang}.lproj`), { recursive: true })
      writeFileSync(join(root, `${lang}.lproj`, 'locale.pak'), pak(strings))
    }
    return join(dir, name)
  }

  it('passes Rio strings and the allowed open source line in every language', () => {
    const r = scan(app('ok.app', {
      en: { 1: 'About Rio', 2: 'Rio is made possible by the <a href="$1">Chromium</a> open source project.', 3: 'Boost productivity', 4: 'Cluster' },
      de: { 1: 'Über Rio', 2: 'Rio wird durch das Open-Source-Projekt Chromium möglich.', 3: 'Arc' }
    }))
    expect(r.out).toContain('0 problems')
    expect(r.status).toBe(0)
  })

  it('fails on upstream names in any language and banned names in English', () => {
    const r = scan(app('bad.app', {
      en: { 1: 'About Chromium', 2: 'Try Boosts', 3: 'Easel' },
      fr: { 1: 'À propos de Google Chrome', 4: 'Arc de triomphe' },
      ja: { 5: 'Clu について' },
      de: { 6: 'Copyright 2026 Acme. Alle Rechte vorbehalten.' }
    }))
    expect(r.status).toBe(1)
    expect(r.out).toContain("#1: upstream name: 'About Chromium'")
    expect(r.out).toContain('#2: banned name')
    expect(r.out).toContain('#3: banned name')
    expect(r.out).toContain("fr.lproj/locale.pak #1: upstream name")
    expect(r.out).not.toContain('#4')
    expect(r.out).toContain("ja.lproj/locale.pak #5: code name: 'Clu について'")
    expect(r.out).toContain('de.lproj/locale.pak #6: company name')
  })

  it('fails on the bare word Chrome in any language, and passes other products, URLs and code names', () => {
    const r = scan(app('chrome.app', {
      en: {
        1: 'Sign in to Chrome',
        2: "You have Chrome's strongest security",
        3: 'Open Chrome Web Store',
        4: 'Go to chrome://settings or call chrome.runtime.connect()',
        5: 'Chromebook and ChromeOS',
        6: 'View in Chrome Web Store',
        7: 'Remove it from Chrome and the Chrome Web Store'
      },
      de: { 1: 'In Chrome anmelden', 3: 'Chrome Web Store öffnen', 6: 'Im Chrome-Webshop ansehen', 8: 'Chrome-Farben' },
      ru: { 6: 'Открыть в Интернет-магазине Chrome' }
    }))
    expect(r.status).toBe(1)
    expect(r.out).toContain("en.lproj/locale.pak #1: Chrome: 'Sign in to Chrome'")
    expect(r.out).toContain('en.lproj/locale.pak #2: Chrome')
    expect(r.out).toContain('en.lproj/locale.pak #7: Chrome')
    expect(r.out).toContain("de.lproj/locale.pak #1: Chrome: 'In Chrome anmelden'")
    expect(r.out).toContain("de.lproj/locale.pak #8: Chrome: 'Chrome-Farben'")
    // A translation of an English string that names the Web Store words it freely.
    expect(r.out).not.toMatch(/#[3456]: /)
    expect(r.out).toContain('5 problems')
  })

  it('rewrites Chrome in English text and in translations that write the kept names as the English does', () => {
    const py = [
      'import importlib.util, json, sys',
      'sys.dont_write_bytecode = True',
      `sys.path.insert(0, ${JSON.stringify(SCRIPTS)})`,
      `spec = importlib.util.spec_from_file_location('rb', ${JSON.stringify(join(SCRIPTS, 'rebrand-strings.py'))})`,
      'rb = importlib.util.module_from_spec(spec); spec.loader.exec_module(rb)',
      'gb = "Chrome will keep running while Chrome apps are open."',
      'ru = "Открыть Интернет-магазин Chrome"',
      'print(json.dumps([rb.rebrand("Sign in to Chrome to use Gemini in Chrome and the Chrome Web Store"),',
      '  rb.rebrand_translation(gb, rb.keeps_names("Chrome will keep running while Chrome Apps are open.", gb)),',
      '  rb.rebrand_translation(ru, rb.keeps_names("Open Chrome Web Store", ru))]))'
    ].join('\n')
    const r = spawnSync('python3', ['-B', '-c', py], { encoding: 'utf8' })
    expect(JSON.parse(r.stdout)).toEqual([
      'Sign in to Rio to use Gemini in Chrome and the Chrome Web Store',
      'Rio will keep running while Chrome apps are open.',
      'Открыть Интернет-магазин Chrome'
    ])
  })

  it('fails on the code name Clu in a plist', () => {
    const named = app('named.app', { en: { 1: 'About Rio' } })
    writeFileSync(join(named, 'Contents/Info.plist'), `<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleName</key><string>Clu</string><key>NSCameraUsageDescription</key><string>Rio uses the camera</string></dict></plist>`)
    const r = scan(named)
    expect(r.status).toBe(1)
    expect(r.out).toContain("Contents/Info.plist: code name: 'Clu'")
    expect(r.out).toContain('1 problems')
  })

  it('fails without locale .pak files and on a corrupt .pak', () => {
    mkdirSync(join(dir, 'empty.app', 'Contents'), { recursive: true })
    expect(scan(join(dir, 'empty.app')).status).toBe(1)
    const broken = app('broken.app', { en: { 1: 'x' } })
    writeFileSync(join(broken, 'Contents/Resources/en.lproj/locale.pak'), Buffer.from([5, 0, 0, 0, 1, 0, 0, 0, 9, 0]))
    expect(scan(broken).status).not.toBe(0)
  })

  it.skipIf(!MAC)('check-brand.sh passes a correctly branded bundle and names each wrong value', () => {
    const good = app('Rio.app', { en: { 1: 'About Rio' } })
    const mac = join(REPO, 'chromium/branding/clu/chrome/app/theme/chromium/mac')
    for (const f of ['app.icns', 'Assets.car']) cpSync(join(mac, f), join(good, 'Contents/Resources', f))
    mkdirSync(join(good, 'Contents/MacOS'))
    writeFileSync(join(good, 'Contents/MacOS/Rio'), Buffer.from('\0\0Rio Safe Storage\0Rio\0'))
    const plist = (id) => `<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleIdentifier</key><string>${id}</string><key>CFBundleName</key><string>Rio</string>
<key>CFBundleDisplayName</key><string>Rio</string><key>CFBundleExecutable</key><string>Rio</string>
<key>CrProductDirName</key><string>Rio</string></dict></plist>`
    writeFileSync(join(good, 'Contents/Info.plist'), plist('org.example.rio'))
    const ok = run(join(SCRIPTS, 'check-brand.sh'), [good])
    expect(ok.out).not.toContain('FAIL')
    expect(ok.status).toBe(0)

    writeFileSync(join(good, 'Contents/Info.plist'), plist('org.chromium.Chromium'))
    writeFileSync(join(good, 'Contents/MacOS/Rio'), Buffer.from('Chromium Safe Storage\0Clu Safe Storage'))
    const bad = run(join(SCRIPTS, 'check-brand.sh'), [good])
    expect(bad.status).toBe(1)
    expect(bad.out).toContain("FAIL  CFBundleIdentifier is 'org.chromium.Chromium', expected 'org.example.rio'")
    expect(bad.out).toContain('FAIL  no "Rio Safe Storage" string in the bundle')
    expect(bad.out).toContain('FAIL  the bundle still contains "Chromium Safe Storage"')
    expect(bad.out).toContain('FAIL  the bundle still contains the code-name item "Clu Safe Storage"')

    // The old name: a bundle called Clu.app with Clu's plist values fails on each of them.
    const old = join(dir, 'Clu.app')
    cpSync(good, old, { recursive: true })
    writeFileSync(join(old, 'Contents/Info.plist'), plist('org.example.clu').replaceAll('>Rio<', '>Clu<').replace('>Clu</string></dict>', '>Example/Clu</string></dict>'))
    const renamed = run(join(SCRIPTS, 'check-brand.sh'), [old])
    expect(renamed.status).toBe(1)
    expect(renamed.out).toContain('FAIL  bundle is Clu.app, expected Rio.app')
    expect(renamed.out).toContain("FAIL  CFBundleName is 'Clu', expected 'Rio'")
    expect(renamed.out).toContain("FAIL  CrProductDirName is 'Example/Clu', expected 'Rio'")
    expect(renamed.out).toContain('code name')
    expect(run(join(SCRIPTS, 'check-brand.sh'), [join(dir, 'missing.app')]).out).toContain('no app bundle at')
  })
})

describe.skipIf(!HAS_CHECKOUT)('chromium pipeline: apply.sh on a fixture from the pinned checkout', () => {
  let tmp, src, repo, pristine
  const apply = (args = [], scripts = SCRIPTS) => run(join(scripts, 'apply.sh'), args, { CLU_CHROMIUM_SRC: src })

  beforeAll(() => {
    tmp = mkdtempSync(join(tmpdir(), 'clu-apply-'))
    src = join(tmp, 'src')
    mkdirSync(src)
    // Upstream files apply.sh touches, from HEAD so an applied checkout still gives pristine copies. Translations
    // other than German and the Chinese omnibox action words become empty bundles to keep the fixture small.
    const listed = run('python3', ['-B', join(SCRIPTS, 'rebrand-strings.py'), '--src', UPSTREAM, '--list']).out
      .split('\n').filter((l) => l && !l.startsWith('rebrand-strings'))
    const patched = execFileSync('sh', ['-c', "sed -n 's|^+++ b/||p' chromium/patches/*.patch"], { cwd: REPO, encoding: 'utf8' }).split('\n')
    const branded = execFileSync('find', ['.', '-type', 'f'], { cwd: join(REPO, 'chromium/branding/clu'), encoding: 'utf8' })
      .split('\n').map((l) => l.replace(/^\.\//, ''))
    const stubs = listed.filter((f) => f.endsWith('.xtb') && !/_de\.xtb$/.test(f) && !f.endsWith('omnibox_pedal_synonyms_zh-CN.xtb'))
    const keep = [...new Set([...listed, ...patched, ...branded].filter(Boolean))].filter((f) => !stubs.includes(f))
    execFileSync('sh', ['-c', 'u=$0 d=$1; shift; git -C "$u" archive HEAD -- tools/grit "$@" | tar -x -C "$d"', UPSTREAM, src, ...keep])
    for (const f of stubs) {
      mkdirSync(dirname(join(src, f)), { recursive: true })
      const lang = /_([^_/]+)\.xtb$/.exec(f)[1]
      writeFileSync(join(src, f), `<?xml version="1.0" ?>\n<!DOCTYPE translationbundle>\n<translationbundle lang="${lang}">\n</translationbundle>\n`)
    }
    git(src, 'init', '-q')
    git(src, 'add', '-A')
    git(src, '-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-qm', 'upstream')
    pristine = sha(join(src, 'chrome/app/theme/chromium/BRANDING'))

    // A copy of the repo's pipeline with new code in chromium/clu and a broken patch, for the copy and failure paths.
    repo = join(tmp, 'repo')
    cpSync(join(REPO, 'chromium'), join(repo, 'chromium'), { recursive: true })
    cpSync(SCRIPTS, join(repo, 'scripts', 'chromium'), { recursive: true })
    mkdirSync(join(repo, 'chromium', 'clu', 'browser'), { recursive: true })
    writeFileSync(join(repo, 'chromium', 'clu', 'BUILD.gn'), 'group("browser") {}\n')
    writeFileSync(join(repo, 'chromium', 'clu', 'browser', 'clu.cc'), '// Clu\n')
  }, 120_000)
  afterAll(() => rmSync(tmp, { recursive: true, force: true }))

  it('dry run checks every patch and writes nothing', { timeout: 180_000 }, () => {
    const r = apply(['--dry-run'])
    expect(r.out).toContain('ok      chrome-app-theme-chromium-BRANDING.patch')
    expect(r.out).toContain(`dry run passed: ${PATCHES} patches apply cleanly`)
    expect(r.status).toBe(0)
    expect(status(src)).toEqual([])
  })

  it('applies the branding, records every file, and reruns to the same result', { timeout: 180_000 }, () => {
    const r = apply()
    expect(r.out).toContain('0 failures')
    expect(r.status).toBe(0)
    const branding = readFileSync(join(src, 'chrome/app/theme/chromium/BRANDING'), 'utf8')
    expect(branding).toContain('PRODUCT_FULLNAME=Rio\n')
    expect(branding).toContain('PRODUCT_SHORTNAME=Rio\n')
    expect(branding).toContain('MAC_BUNDLE_ID=org.example.rio\n')
    expect(branding).toContain('COMPANY_FULLNAME=The Rio Authors\n')
    expect(readFileSync(join(src, 'chrome/app/app-Info.plist'), 'utf8')).toMatch(/<key>CrProductDirName<\/key>\s*<string>Rio<\/string>/)
    expect(readFileSync(join(src, 'components/os_crypt/common/keychain_password_mac.mm'), 'utf8')).toContain('kDefaultServiceName[] = "Rio Safe Storage"')
    const icns = 'chrome/app/theme/chromium/mac/app.icns'
    expect(sha(join(src, icns))).toBe(sha(join(REPO, 'chromium/branding/clu', icns)))
    const about = readFileSync(join(src, 'components/components_chromium_strings.grd'), 'utf8')
    expect(about).toContain('Rio is made possible by the <ph name="BEGIN_LINK_CHROMIUM">')
    expect(about).toMatch(/<\/ph>Chromium<ph name="END_LINK_CHROMIUM">/)
    expect(readFileSync(join(src, 'chrome/app/chromium_strings.grd'), 'utf8')).toMatch(/Relaunch Rio within a day/)

    const rows = (m) => m.split('\n').filter((l) => l && !l.startsWith('#'))
    const manifest = readFileSync(join(src, '.clu-applied'), 'utf8')
    expect(status(src).filter((l) => !l.endsWith('.clu-applied'))).toHaveLength(rows(manifest).length)
    const aged = ageRecorded(src)
    const rerun = apply()
    expect(rerun.status).toBe(0)
    expect(rerun.out).toContain(`kept the mtime of ${aged.size} files whose bytes did not change`)
    expect(rows(readFileSync(join(src, '.clu-applied'), 'utf8'))).toEqual(rows(manifest))
    // Every file came back byte for byte, so none may look newer to ninja.
    for (const [p, t] of aged) expect([p, mtime(join(src, p))]).toEqual([p, t])
  })

  it('moves German translations to the rebranded ids', () => {
    const de = readFileSync(join(src, 'chrome/app/resources/chromium_strings_de.xtb'), 'utf8')
    expect(de).toContain('>Rio neu starten?</translation>')
    expect(de).not.toMatch(/Chromium(?!\s?OS)/) // \s: German writes Chromium\u00a0OS
    expect(git(src, 'show', 'HEAD:chrome/app/resources/chromium_strings_de.xtb')).toContain('Chromium neu starten?')
  })

  it('rebrands "Google Chrome" in translated omnibox action words, whose English says "google chrome"', () => {
    const zh = readFileSync(join(src, 'components/omnibox/resources/translations/omnibox_pedal_synonyms_zh-CN.xtb'), 'utf8')
    expect(zh).toContain('>更新 Rio, 升级浏览器, 更新 Rio</translation>')
    expect(zh).not.toContain('Google Chrome')
  })

  it('rebrands the bare word Chrome in English and translations, and keeps other products named Chrome', () => {
    const settings = readFileSync(join(src, 'chrome/app/settings_strings.grdp'), 'utf8')
    expect(settings).toMatch(/>\s*Rio Colors\s*</)
    expect(settings).not.toMatch(/>\s*Chrome Colors\s*</)
    const de = readFileSync(join(src, 'chrome/app/resources/generated_resources_de.xtb'), 'utf8')
    expect(de).toContain('>Rio-Farben</translation>')
    expect(de).not.toContain('>Chrome-Farben</translation>')
    expect(de).toContain('>Im Chrome Web Store ansehen</translation>')
    // No English message is left naming Chrome outside another product's name.
    const english = ['chrome/app/generated_resources.grd', 'chrome/app/settings_strings.grdp', 'components/components_strings.grd']
      .map((f) => readFileSync(join(src, f), 'utf8'))
    for (const text of english) {
      const left = spawnSync('python3', ['-B', '-c', [
        'import re, sys',
        `sys.path.insert(0, ${JSON.stringify(SCRIPTS)})`,
        'from brand_scan import bare_chrome',
        'bodies = re.findall(r"<message\\b[^>]*>(.*?)</message>", sys.stdin.read(), re.S)',
        'print(sum(1 for b in bodies if bare_chrome(b)))'
      ].join('\n')], { input: text, encoding: 'utf8' })
      expect(left.stdout.trim()).toBe('0')
    }
  })

  it('refuses a tree with changes it did not make, and files edited after the apply', { timeout: 180_000 }, () => {
    writeFileSync(join(src, 'stray.txt'), 'x')
    const stray = apply()
    expect(stray.status).toBe(1)
    expect(stray.out).toContain('stray.txt (not made by apply.sh)')
    rmSync(join(src, 'stray.txt'))

    writeFileSync(join(src, 'chrome/app/theme/chromium/BRANDING'), 'hand edit\n', { flag: 'a' })
    expect(apply().out).toContain('BRANDING (edited after apply.sh')
    const revert = apply(['--revert'])
    expect(revert.status).toBe(1)
    expect(revert.out).toContain('rerun with --force')
    expect(apply(['--revert', '--force']).status).toBe(0)
    expect(status(src)).toEqual([])
    expect(sha(join(src, 'chrome/app/theme/chromium/BRANDING'))).toBe(pristine)
    expect(apply(['--revert']).out).toContain('nothing to revert')
  })

  it('refuses a corrupt manifest', () => {
    writeFileSync(join(src, '.clu-applied'), 'tracked\tnot-a-hash\tchrome/app/app-Info.plist\n')
    const r = apply()
    expect(r.status).toBe(1)
    expect(r.out).toContain('line 1 is corrupt')
    rmSync(join(src, '.clu-applied'))
  })

  it('copies chromium/clu into src/clu and removes it on revert', { timeout: 180_000 }, () => {
    const scripts = join(repo, 'scripts', 'chromium')
    expect(apply([], scripts).status).toBe(0)
    expect(readFileSync(join(src, 'clu/browser/clu.cc'), 'utf8')).toBe('// Clu\n')
    expect(readFileSync(join(src, '.clu-applied'), 'utf8')).toMatch(/^new\t[0-9a-f]{64}\tclu\/BUILD\.gn$/m)

    // A rerun after a one-file fix rewrites that file and leaves every other mtime alone.
    const aged = ageRecorded(src)
    const cc = join(repo, 'chromium', 'clu', 'browser', 'clu.cc')
    writeFileSync(cc, '// Clu, fixed\n')
    try {
      const r = apply([], scripts)
      expect(r.status).toBe(0)
      expect(r.out).toContain(`kept the mtime of ${aged.size - 1} files`)
    } finally {
      writeFileSync(cc, '// Clu\n')
    }
    expect(readFileSync(join(src, 'clu/browser/clu.cc'), 'utf8')).toBe('// Clu, fixed\n')
    expect(mtime(join(src, 'clu/browser/clu.cc'))).toBeGreaterThan(aged.get('clu/browser/clu.cc'))
    expect(mtime(join(src, 'clu/BUILD.gn'))).toBe(aged.get('clu/BUILD.gn'))
    expect(mtime(join(src, 'chrome/app/theme/chromium/BRANDING'))).toBe(aged.get('chrome/app/theme/chromium/BRANDING'))

    expect(apply(['--revert'], scripts).status).toBe(0)
    expect(existsSync(join(src, 'clu'))).toBe(false)
    expect(status(src)).toEqual([])
  })

  it('reports a patch that no longer applies, exits non-zero, and still reverts cleanly', { timeout: 180_000 }, () => {
    const scripts = join(repo, 'scripts', 'chromium')
    const bad = join(repo, 'chromium', 'patches', 'chrome-app-theme-chromium-BRANDING.patch')
    const good = readFileSync(bad, 'utf8')
    writeFileSync(bad, good.replace(' MAC_CREATOR_CODE=Cr24', ' MAC_CREATOR_CODE=Xx99'))
    try {
      const r = apply([], scripts)
      expect(r.status).toBe(1)
      expect(r.out).toContain('chrome-app-theme-chromium-BRANDING.patch:')
      expect(r.out).toContain('1 steps failed')
      expect(apply(['--dry-run'], scripts).out).toContain('FAILED  chrome-app-theme-chromium-BRANDING.patch')
    } finally {
      writeFileSync(bad, good)
    }
    expect(apply(['--revert', '--force'], scripts).status).toBe(0)
    expect(status(src)).toEqual([])
  })

  it('build.sh dry run needs an applied tree and prints the low-priority build', { timeout: 180_000 }, () => {
    const root = join(tmp, 'root')
    mkdirSync(join(root, 'depot_tools'), { recursive: true })
    for (const tool of ['autoninja', 'gclient']) {
      writeFileSync(join(root, 'depot_tools', tool), '#!/bin/sh\nexit 0\n')
      chmodSync(join(root, 'depot_tools', tool), 0o755)
    }
    const build = (args) => run(join(SCRIPTS, 'build.sh'), args, { CLU_CHROMIUM_ROOT: root, CLU_CHROMIUM_SRC: src })
    expect(build(['--dry-run', 'dev']).out).toContain('would refuse: Clu is not applied')
    expect(apply().status).toBe(0)
    const dev = build(['--dry-run', 'dev'])
    expect(dev.out).toContain('gn gen out/Dev --args=<')
    expect(dev.out).toContain('nice -n 10 autoninja -C out/Dev -j 10 chrome')
    expect(dev.out).not.toContain('would refuse: Clu is not applied')
    // Without configuration the app is the placeholder org.example.rio, out/Dev's id adds .test, BRANDING stays as patched.
    expect(dev.out).toContain('clu_bundle_id = "org.example.rio.test"')
    expect(dev.out).not.toContain('BRANDING copy')
    expect(build(['--dry-run', '--away', 'dev', 'chrome', 'clu_unittests']).out).toContain('caffeinate -i nice -n 5 autoninja -C out/Dev -j 14 chrome clu_unittests')
    // release.gn builds with PGO: the dry run refuses until the checkout has the profile, then goes ahead.
    expect(build(['--dry-run', 'release']).out).toContain('wants PGO profiles')
    mkdirSync(join(src, 'chrome', 'build', 'pgo_profiles'), { recursive: true })
    writeFileSync(join(src, 'chrome', 'build', 'pgo_profiles', 'chrome-mac-arm-test.profdata'), '')
    expect(build(['--dry-run', 'release']).out).not.toContain('wants PGO profiles')
    rmSync(join(src, 'chrome', 'build', 'pgo_profiles'), { recursive: true, force: true })
    const home = join(tmp, 'home')
    mkdirSync(join(home, '.clu'), { recursive: true })
    writeFileSync(join(home, '.clu', 'keys.gn'), 'google_api_key = "test"\n')
    const fast = run(join(SCRIPTS, 'build.sh'), ['--dry-run', '-j', '14', 'fast'], { CLU_CHROMIUM_ROOT: root, CLU_CHROMIUM_SRC: src, HOME: home })
    expect(fast.status).toBe(0)
    expect(fast.out).toContain(`gn gen out/Fast --args=<${join(REPO, 'chromium', 'args', 'fast.gn')}>`)
    expect(fast.out).toContain('nice -n 10 autoninja -C out/Fast -j 14 chrome')
    expect(fast.out).toContain(`adding the API keys from ${join(home, '.clu', 'keys.gn')}`)
    expect(fast.out).not.toContain('wants PGO profiles')
    expect(fast.out).toContain('clu_bundle_id = "org.example.rio"')
    // A configured id (~/.clu/package.env) names the app and goes into a copy of BRANDING for the helpers and framework.
    writeFileSync(join(home, '.clu', 'package.env'), 'CLU_BUNDLE_ID=org.example.official\n')
    const env = { CLU_CHROMIUM_ROOT: root, CLU_CHROMIUM_SRC: src, HOME: home, CLU_PACKAGE_ENV: join(home, '.clu', 'package.env') }
    const official = run(join(SCRIPTS, 'build.sh'), ['--dry-run', 'fast'], env)
    expect(official.status).toBe(0)
    expect(official.out).toContain(`clu_bundle_id = "org.example.official", BRANDING copy ${join(REPO, 'out/branding/Fast/BRANDING')} with MAC_BUNDLE_ID=org.example.official`)
    expect(run(join(SCRIPTS, 'build.sh'), ['--dry-run', 'dev'], env).out).toContain('clu_bundle_id = "org.example.official.test"')
    expect(run(join(SCRIPTS, 'build.sh'), ['--dry-run', 'dev'], { ...env, CLU_BUNDLE_ID: 'not an id' }).out).toContain("CLU_BUNDLE_ID 'not an id' is not a reverse-DNS bundle id")
    const fastArgs = readFileSync(join(REPO, 'chromium', 'args', 'fast.gn'), 'utf8')
    for (const line of ['is_debug = false', 'is_official_build = false', 'is_component_build = false', 'dcheck_always_on = false',
      'symbol_level = 0', 'use_lld = false', 'enable_widevine = true', 'ignore_missing_widevine_signing_cert = true', 'enable_updater = false']) {
      expect(fastArgs).toContain(line)
    }
    expect(fastArgs).not.toMatch(/^\s*(chrome_pgo_phase|use_thin_lto)\b/m)
    expect(build(['nightly']).out).toContain("unknown profile 'nightly'")
    expect(build(['-j', '0', 'dev']).out).toContain('-j needs a positive number')

    const sync = (args, env = {}) => run(join(SCRIPTS, 'sync.sh'), args, { CLU_CHROMIUM_ROOT: root, CLU_CHROMIUM_SRC: src, ...env })
    expect(sync(['--dry-run']).out).toContain('Clu is applied to')
    const fresh = sync(['--dry-run', '--nohooks'], { CLU_CHROMIUM_SRC: join(root, 'src') })
    expect(fresh.out).toContain('gclient sync --nohooks --no-history --shallow -j 4 --revision src@refs/tags/158.0.8127.18')
    expect(fresh.out).toContain('nice -n 15')
    expect(fresh.out).toContain('would write')
    expect(fresh.status).toBe(0)
    expect(existsSync(join(root, '.gclient'))).toBe(false)
    expect(apply(['--revert']).status).toBe(0)
  })
})
