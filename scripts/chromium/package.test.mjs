// M12 packaging and updates without a Chromium build (docs/chromium/m12.md): the Sparkle pin, sparkle.mjs (keys,
// signatures, the appcast, the loopback feed server), package.sh's checks and plans, on macOS a real ad-hoc package of
// a stand-in app when the pinned Sparkle is already fetched, and the self-signed mode of sign-dev.sh and package.sh
// with a throwaway identity. The update itself from one package to a newer one is scripts/chromium/update-smoke.sh (it
// runs Sparkle's installer, so it stays out of the unit run).
import { execFileSync, spawnSync } from 'node:child_process'
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs'
import { request } from 'node:http'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { appcastItem, appcastItems, buildAppcast, createFeedServer, isAllowedFeedUrl, keygen, publicKeyOf, readSeed, signBytes, verifyBytes } from './sparkle.mjs'

const REPO = resolve('.')
const SCRIPTS = join(REPO, 'scripts', 'chromium')
const MAC = process.platform === 'darwin'
const PIN = Object.fromEntries(
  readFileSync(join(REPO, 'chromium', 'sparkle.pin'), 'utf8').split('\n').filter((l) => /^[a-z0-9]+=/.test(l)).map((l) => [l.slice(0, l.indexOf('=')), l.slice(l.indexOf('=') + 1)])
)
const SPARKLE_CACHED = existsSync(join(REPO, 'out', 'sparkle', PIN.version ?? '', '.verified'))
const shellcheck = [process.env.SHELLCHECK, ...(process.env.PATH ?? '').split(':').map((d) => join(d, 'shellcheck'))].find((p) => p && existsSync(p))

let tmp = ''
beforeAll(() => {
  tmp = mkdtempSync(join(tmpdir(), 'clu-package-test-'))
})
afterAll(() => {
  rmSync(tmp, { recursive: true, force: true })
})

/** A stand-in Rio.app: Apple's true(1) as an arm64 executable and Rio's Info.plist keys. */
function standIn(dir, { id = 'org.example.rio.sparkle-smoke', version = '8127.18', short = '158.0.8127.18', min = '12.0', argsGn } = {}) {
  const app = join(dir, 'Rio.app')
  mkdirSync(join(app, 'Contents', 'MacOS'), { recursive: true })
  copyFileSync('/usr/bin/true', join(app, 'Contents', 'MacOS', 'Rio'))
  const keys = { CFBundleExecutable: 'Rio', CFBundleIdentifier: id, CFBundleName: 'Rio', CFBundlePackageType: 'APPL', CFBundleShortVersionString: short, CFBundleVersion: version, LSMinimumSystemVersion: min }
  const body = Object.entries(keys).map(([k, v]) => `<key>${k}</key><string>${v}</string>`).join('')
  writeFileSync(join(app, 'Contents', 'Info.plist'), `<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict>${body}</dict></plist>`)
  if (argsGn) writeFileSync(join(dir, 'args.gn'), argsGn)
  return app
}

/** package.sh with no configuration but `env`; { status, out }. */
function pkg(args, env = {}) {
  const base = Object.fromEntries(Object.entries(process.env).filter(([k]) => !k.startsWith('CLU_')))
  const r = spawnSync(join(SCRIPTS, 'package.sh'), args, { cwd: REPO, env: { ...base, CLU_PACKAGE_ENV: join(tmp, 'none.env'), ...env }, encoding: 'utf8', timeout: 120_000 })
  return { status: r.status, out: `${r.stdout}${r.stderr}` }
}

describe('Sparkle pin', () => {
  it('names a release asset of Sparkle on GitHub with its published SHA-256', () => {
    expect(PIN.version).toMatch(/^\d+\.\d+\.\d+$/)
    expect(PIN.url).toBe(`https://github.com/sparkle-project/Sparkle/releases/download/${PIN.version}/Sparkle-${PIN.version}.tar.xz`)
    expect(PIN.sha256).toMatch(/^[0-9a-f]{64}$/)
    expect(PIN.commit).toMatch(/^[0-9a-f]{40}$/)
  })

  it.runIf(SPARKLE_CACHED)('keeps a fetched copy only when it matches the pin', () => {
    expect(readFileSync(join(REPO, 'out', 'sparkle', PIN.version, '.verified'), 'utf8').trim()).toBe(PIN.sha256)
    const r = spawnSync(join(SCRIPTS, 'fetch-sparkle.sh'), ['--offline'], { encoding: 'utf8' })
    expect(r.status).toBe(0)
    expect(existsSync(join(r.stdout.trim(), 'Sparkle.framework', 'Versions', 'B', 'Sparkle'))).toBe(true)
  })
})

describe('sparkle.mjs keys and signatures', () => {
  it('writes a 32-byte seed Sparkle reads, never over an existing key, and signs what Sparkle verifies', () => {
    const file = join(tmp, 'key')
    const pub = keygen(file)
    expect(statSync(file).mode & 0o777).toBe(0o600)
    expect(() => keygen(file)).toThrow(/never overwrites/)
    const seed = readSeed(file)
    expect(seed).toHaveLength(32)
    expect(publicKeyOf(seed)).toBe(pub)
    expect(Buffer.from(pub, 'base64')).toHaveLength(32)
    const bytes = Buffer.from('an update archive')
    const sig = signBytes(seed, bytes)
    expect(verifyBytes(pub, bytes, sig)).toBe(true)
    expect(verifyBytes(pub, Buffer.from('an update archivE'), sig)).toBe(false)
  })

  it('matches the Ed25519 test vector Sparkle and RFC 8032 use', () => {
    // RFC 8032 section 7.1, test 1: the empty message.
    const file = join(tmp, 'rfc')
    writeFileSync(file, Buffer.from('9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60', 'hex').toString('base64'))
    const seed = readSeed(file)
    expect(Buffer.from(publicKeyOf(seed), 'base64').toString('hex')).toBe('d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a')
    expect(Buffer.from(signBytes(seed, Buffer.alloc(0)), 'base64').toString('hex')).toBe(
      'e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b'
    )
  })

  it('refuses junk key files', () => {
    const write = (name, text) => {
      const f = join(tmp, name)
      writeFileSync(f, text)
      return f
    }
    expect(() => readSeed(join(tmp, 'missing'))).toThrow(/no key file/)
    expect(() => readSeed(write('notb64', 'not base64!'))).toThrow(/not base64/)
    expect(() => readSeed(write('short', Buffer.alloc(31).toString('base64')))).toThrow(/31 bytes/)
    expect(() => readSeed(write('legacy', Buffer.alloc(96).toString('base64')))).toThrow(/old 96-byte/)
    expect(verifyBytes('AAAA', Buffer.from('x'), 'AAAA')).toBe(false)
  })
})

describe('sparkle.mjs feed rules and appcast', () => {
  it.each([
    ['https://updates.example/clu/appcast.xml', true],
    ['http://127.0.0.1:47811/appcast.xml', true],
    ['http://localhost:8000/appcast.xml', true],
    ['http://[::1]:8000/appcast.xml', true],
    ['http://updates.example/appcast.xml', false],
    ['http://127.0.0.2/appcast.xml', false],
    ['http://foo.localhost/appcast.xml', false],
    ['file:///tmp/appcast.xml', false],
    ['javascript:alert(1)', false],
    ['https://user:pw@updates.example/a.xml', false],
    ['https://updates.example/a.xml#x', false],
    ['not a url', false],
    [`https://updates.example/${'a'.repeat(2048)}`, false]
  ])('%s is allowed: %s', (url, allowed) => {
    expect(isAllowedFeedUrl(url)).toBe(allowed)
  })

  const sig = Buffer.alloc(64, 7).toString('base64')
  const item = (over = {}) =>
    appcastItem({ version: '8127.19', short: '158.0.8127.19', minSystem: '12.0', url: 'https://updates.example/Rio-158.0.8127.19.zip', length: 1234, signature: sig, date: new Date(0), ...over })

  it('writes the item Sparkle reads, escaping the URL', () => {
    const xml = item({ url: 'https://updates.example/a.zip?x=1&y="2"' })
    expect(xml).toContain('<sparkle:version>8127.19</sparkle:version>')
    expect(xml).toContain('<sparkle:shortVersionString>158.0.8127.19</sparkle:shortVersionString>')
    expect(xml).toContain('<sparkle:minimumSystemVersion>12.0</sparkle:minimumSystemVersion>')
    expect(xml).toContain('url="https://updates.example/a.zip?x=1&amp;y=&quot;2&quot;" length="1234"')
    expect(xml).toContain(`sparkle:edSignature="${sig}"`)
  })

  it.each([
    [{ version: '8127.19<' }, /bad version/],
    [{ short: '' }, /bad short version/],
    [{ minSystem: 'twelve' }, /minimum system/],
    [{ url: 'http://updates.example/a.zip' }, /https/],
    [{ length: 0 }, /length/],
    [{ length: 1.5 }, /length/],
    [{ signature: 'nope' }, /signature/]
  ])('refuses junk: %o', (over, error) => {
    expect(() => item(over)).toThrow(error)
  })

  it('puts the new release first, replaces the same version, keeps at most N, and drops junk items', () => {
    const one = buildAppcast(item({ version: '8127.18', short: '158.0.8127.18' }), '8127.18')
    const two = buildAppcast(item(), '8127.19', one)
    expect(appcastItems(two).map((i) => i.version)).toEqual(['8127.19', '8127.18'])
    const again = buildAppcast(item({ length: 99 }), '8127.19', two)
    expect(appcastItems(again).map((i) => i.version)).toEqual(['8127.19', '8127.18'])
    expect(again).toContain('length="99"')
    expect(appcastItems(buildAppcast(item(), '8127.19', two, 1)).map((i) => i.version)).toEqual(['8127.19'])
    const junk = `${two}<item><sparkle:version>x<script></sparkle:version></item><item>no version</item>`
    expect(appcastItems(junk).map((i) => i.version)).toEqual(['8127.19', '8127.18'])
    expect(two).toMatch(/^<\?xml version="1.0" encoding="utf-8"\?>\n<rss version="2.0" xmlns:sparkle="http:\/\/www.andymatuschak.org\/xml-namespaces\/sparkle">/)
  })
})

describe('sparkle.mjs feed server', () => {
  let server
  let port = 0
  const lines = []
  const get = (path, method = 'GET') =>
    new Promise((done, fail) => {
      const req = request({ host: '127.0.0.1', port, path, method }, (res) => {
        let body = ''
        res.on('data', (c) => (body += c))
        res.on('end', () => done({ status: res.statusCode, body }))
      })
      req.on('error', fail)
      req.end()
    })

  beforeAll(async () => {
    const root = join(tmp, 'feed')
    mkdirSync(join(root, 'sub'), { recursive: true })
    writeFileSync(join(root, 'appcast.xml'), '<rss/>')
    writeFileSync(join(root, '.hidden'), 'secret')
    writeFileSync(join(tmp, 'outside.txt'), 'outside')
    server = createFeedServer(root, (l) => lines.push(l))
    await new Promise((done) => server.listen(0, '127.0.0.1', done))
    port = server.address().port
  })
  afterAll(() => new Promise((done) => server.close(done)))

  it('listens on 127.0.0.1 only and serves files inside its root', async () => {
    expect(server.address().address).toBe('127.0.0.1')
    expect(await get('/appcast.xml')).toEqual({ status: 200, body: '<rss/>' })
    expect((await get('/appcast.xml', 'HEAD')).status).toBe(200)
    expect(lines).toContain('GET /appcast.xml 200')
  })

  it.each([
    ['/../outside.txt', 404],
    ['/%2e%2e/outside.txt', 404],
    ['/sub/..%2f..%2foutside.txt', 404],
    ['/.hidden', 404],
    ['/sub', 404],
    ['/missing.zip', 404],
    ['/%E0%A4%A', 400]
  ])('refuses %s with %i', async (path, status) => {
    expect((await get(path)).status).toBe(status)
  })

  it('answers only GET and HEAD', async () => {
    expect((await get('/appcast.xml', 'POST')).status).toBe(405)
  })
})

describe.runIf(MAC)('package.sh checks and plans', () => {
  it('plans an ad-hoc package without configuration and refuses nothing', () => {
    const app = standIn(join(tmp, 'plan-adhoc'))
    const r = pkg(['--app', app, '--out', join(tmp, 'plan-out'), '--dry-run'])
    expect(r.out).toContain('signature: ad-hoc')
    expect(r.out).toContain('no feed configured, so this package never checks')
    expect(r.out).toContain('codesign --force --sign -')
    expect(r.out).toContain('hdiutil create -volname Rio')
    expect(r.out).not.toContain('notarytool')
    expect(r.out).not.toContain('would refuse')
    expect(r.status).toBe(0)
    expect(existsSync(join(tmp, 'plan-out'))).toBe(false)
  })

  it('plans a Developer ID package through Chromium signing and notarytool, refusing what is missing', () => {
    const app = standIn(join(tmp, 'plan-devid'))
    const r = pkg(['--app', app, '--out', join(tmp, 'plan-out'), '--dry-run'], {
      CLU_SIGN_IDENTITY: 'Developer ID Application: Example Test (TEAMID1234)',
      CLU_NOTARY_PROFILE: 'clu-notary'
    })
    expect(r.out).toContain('sign_chrome.py')
    expect(r.out).toContain('--identity')
    expect(r.out).toContain("xcrun notarytool submit Rio.zip --keychain-profile 'clu-notary' --wait")
    expect(r.out).toContain('xcrun stapler staple')
    expect(r.out).toContain('-o runtime --timestamp')
    expect(r.out).toMatch(/would refuse: no '.*Rio Packaging\/sign_chrome.py'/)
    expect(r.out).toContain('would refuse: the keychain has no valid identity')
    expect(r.status).toBe(1)
  })

  it('reads ~/.clu/package.env style files, with the environment winning', () => {
    const app = standIn(join(tmp, 'plan-env'))
    const env = join(tmp, 'package.env')
    writeFileSync(env, '# local settings\nCLU_UPDATE_FEED_URL="https://updates.example/clu/appcast.xml"\nCLU_NOTARY_PROFILE=from-file\nNOT_CLU=1\n')
    const r = pkg(['--app', app, '--out', join(tmp, 'plan-out'), '--dry-run'], { CLU_PACKAGE_ENV: env, CLU_UPDATE_KEY_FILE: join(tmp, 'missing.key') })
    expect(r.out).toContain('feed https://updates.example/clu/appcast.xml, archives at https://updates.example/clu/')
    expect(r.out).toContain('would refuse: CLU_NOTARY_PROFILE needs CLU_SIGN_IDENTITY')
    expect(r.out).toMatch(/would refuse: the update key: no key file/)
    const winner = pkg(['--app', app, '--out', join(tmp, 'plan-out'), '--dry-run'], { CLU_PACKAGE_ENV: env, CLU_UPDATE_FEED_URL: 'https://other.example/a.xml', CLU_NOTARY_PROFILE: '', CLU_UPDATE_KEY_FILE: join(tmp, 'missing.key') })
    expect(winner.out).toContain('feed https://other.example/a.xml')
  })

  it.each([
    ['a feed that is not https', { CLU_UPDATE_FEED_URL: 'http://updates.example/appcast.xml' }, /must be https, or http to 127.0.0.1/],
    ['an identity that is not Developer ID', { CLU_SIGN_IDENTITY: 'Apple Development: me' }, /must be a Developer ID Application identity/]
  ])('refuses %s', (_name, env, error) => {
    const app = standIn(join(tmp, `refuse-${Object.keys(env)[0]}`))
    const r = pkg(['--app', app, '--out', join(tmp, 'plan-out'), '--dry-run'], env)
    expect(r.out).toMatch(error)
    expect(r.status).toBe(1)
  })

  it.each([
    ['another bundle id', { id: 'org.chromium.Chromium' }, /bundle id 'org.chromium.Chromium' is not this build's \(org\.example\.rio, CLU_BUNDLE_ID\)/],
    ['a macOS older than Sparkle needs', { min: '11.0' }, /below 12.0/],
    ['a junk version', { version: '8127.18-rc 1' }, /bad CFBundleVersion/],
    ['a component build', { argsGn: 'is_component_build = true\n' }, /component build/]
  ])('refuses %s before doing anything', (_name, opts, error) => {
    const dir = join(tmp, `bad-${opts.id ?? opts.min ?? opts.argsGn?.length ?? 'v'}`)
    const app = standIn(dir, opts)
    const r = pkg(['--app', app, '--out', join(dir, 'out')])
    expect(r.out).toMatch(error)
    expect(r.status).toBe(1)
    expect(existsSync(join(dir, 'out'))).toBe(false)
  })

  it('refuses an app that is missing or not named Rio.app', () => {
    expect(pkg(['--app', join(tmp, 'nowhere', 'Rio.app'), '--dry-run']).out).toContain('no app at')
    const other = join(tmp, 'other', 'Other.app')
    mkdirSync(other, { recursive: true })
    expect(pkg(['--app', other, '--dry-run']).out).toContain('must be named Rio.app')
  })

  it('names the official build and PGO from args.gn', () => {
    const app = standIn(join(tmp, 'official'), { argsGn: 'is_official_build = true\nchrome_pgo_phase = 2\n' })
    expect(pkg(['--app', app, '--dry-run']).out).toContain('build: official, PGO')
  })
})

describe.runIf(MAC && SPARKLE_CACHED)('package.sh makes an ad-hoc Rio.dmg from an existing app', () => {
  it('signs, embeds Sparkle with the feed keys, verifies the image, and signs the update archive', { timeout: 120_000 }, () => {
    const dir = join(tmp, 'real')
    const app = standIn(dir)
    const key = join(dir, 'key')
    const pub = keygen(key)
    const out = join(dir, 'out')
    const r = pkg(['--app', app, '--out', out], { CLU_UPDATE_FEED_URL: 'http://127.0.0.1:9/appcast.xml', CLU_UPDATE_KEY_FILE: key })
    expect(r.out).not.toContain('error')
    expect(r.status).toBe(0)
    expect(readdirSync(out).sort()).toEqual(['Rio-158.0.8127.18.zip', 'Rio.dmg', 'appcast.xml', 'package.txt'])
    expect(spawnSync('hdiutil', ['verify', '-quiet', join(out, 'Rio.dmg')]).status).toBe(0)
    const summary = readFileSync(join(out, 'package.txt'), 'utf8')
    expect(summary).toContain('signature: adhoc')
    expect(summary).toContain('notarized: no')
    expect(summary).toMatch(/gatekeeper: .*rejected/)
    const appcast = readFileSync(join(out, 'appcast.xml'), 'utf8')
    const signature = /sparkle:edSignature="([^"]+)"/.exec(appcast)?.[1] ?? ''
    expect(appcast).toContain('url="http://127.0.0.1:9/Rio-158.0.8127.18.zip"')
    expect(verifyBytes(pub, readFileSync(join(out, 'Rio-158.0.8127.18.zip')), signature)).toBe(true)
    // The update archive holds the signed app with Sparkle and the keys Rio's updater reads.
    const unzip = join(dir, 'unzip')
    execFileSync('ditto', ['-x', '-k', join(out, 'Rio-158.0.8127.18.zip'), unzip])
    const info = join(unzip, 'Rio.app', 'Contents', 'Info.plist')
    const key_ = (k) => execFileSync('plutil', ['-extract', k, 'raw', '-o', '-', info], { encoding: 'utf8' }).trim()
    expect(key_('SUFeedURL')).toBe('http://127.0.0.1:9/appcast.xml')
    expect(key_('SUPublicEDKey')).toBe(pub)
    expect(key_('SUEnableAutomaticChecks')).toBe('true')
    expect(key_('SUScheduledCheckInterval')).toBe('3600')
    expect(key_('SUEnableSystemProfiling')).toBe('false')
    expect(spawnSync('codesign', ['--verify', '--deep', '--strict', join(unzip, 'Rio.app')]).status).toBe(0)
    expect(execFileSync('lipo', ['-archs', join(unzip, 'Rio.app', 'Contents', 'Frameworks', 'Sparkle.framework', 'Versions', 'B', 'Sparkle')], { encoding: 'utf8' }).trim()).toBe('arm64')
    // Nothing it copied stays registered with LaunchServices.
    const dump = spawnSync('/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister', ['-dump'], { encoding: 'utf8', maxBuffer: 512 * 1024 * 1024 }).stdout
    expect(dump.includes(`${out}/`)).toBe(false)
  })
})

/** sign-dev.sh with no configuration but `env`; { status, out }. */
function signDev(args, env = {}) {
  const base = Object.fromEntries(Object.entries(process.env).filter(([k]) => !k.startsWith('CLU_')))
  const r = spawnSync(join(SCRIPTS, 'sign-dev.sh'), args, { cwd: REPO, env: { ...base, CLU_PACKAGE_ENV: join(tmpdir(), 'clu-no-package.env'), ...env }, encoding: 'utf8', timeout: 120_000 })
  return { status: r.status, out: `${r.stdout}${r.stderr}` }
}

describe.runIf(MAC)('self-signed mode (docs/chromium/m12.md section 7)', () => {
  // A throwaway "Test Self-Signed" identity in its own keychain, made the way docs/chromium/m12.md makes the real one.
  // security(1) runs with a private HOME, so the user's keychain search list and trust settings never change.
  let dir = ''
  let keychain = ''
  let env = {}
  beforeAll(() => {
    dir = join(tmp, 'self-signed')
    const home = join(dir, 'home')
    mkdirSync(join(home, 'Library', 'Preferences'), { recursive: true })
    const password = 'test-password'
    keychain = join(dir, 'signing', 'rio-signing.keychain-db')
    mkdirSync(join(dir, 'signing'))
    writeFileSync(join(dir, 'signing', 'keychain-password'), password, { mode: 0o600 })
    const conf = join(dir, 'cert.cnf')
    writeFileSync(conf, '[req]\ndistinguished_name = dn\nprompt = no\nx509_extensions = ext\n[dn]\nCN = Test Self-Signed\n[ext]\nbasicConstraints = critical, CA:false\nkeyUsage = critical, digitalSignature\nextendedKeyUsage = critical, codeSigning\nsubjectKeyIdentifier = hash\n')
    const sh = (cmd, args) => execFileSync(cmd, args, { env: { ...process.env, HOME: home }, stdio: 'pipe' })
    sh('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256', '-days', '2', '-config', conf, '-keyout', join(dir, 'key.pem'), '-out', join(dir, 'cert.pem')])
    sh('openssl', ['pkcs12', '-export', '-inkey', join(dir, 'key.pem'), '-in', join(dir, 'cert.pem'), '-name', 'Test Self-Signed', '-passout', `pass:${password}`, '-out', join(dir, 'id.p12')])
    sh('security', ['create-keychain', '-p', password, keychain])
    sh('security', ['unlock-keychain', '-p', password, keychain])
    sh('security', ['import', join(dir, 'id.p12'), '-k', keychain, '-f', 'pkcs12', '-P', password, '-T', '/usr/bin/codesign'])
    sh('security', ['set-key-partition-list', '-S', 'apple-tool:,apple:', '-s', '-k', password, keychain])
    // Chromium's entitlements, as the checkout holds them.
    const src = join(dir, 'src')
    mkdirSync(join(src, 'chrome', 'app'), { recursive: true })
    const plist = (keys) => `<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict>${keys.map((k) => `<key>${k}</key><true/>`).join('')}</dict></plist>`
    writeFileSync(join(src, 'chrome', 'app', 'app-entitlements.plist'), plist(['com.apple.security.device.camera']))
    writeFileSync(join(src, 'chrome', 'app', 'helper-renderer-entitlements.plist'), plist(['com.apple.security.cs.allow-jit']))
    writeFileSync(join(src, 'chrome', 'app', 'helper-gpu-entitlements.plist'), plist(['com.apple.security.cs.allow-jit']))
    env = { CLU_SIGN_IDENTITY: 'Test Self-Signed', CLU_SIGN_IDENTIFIER: 'org.example.signing', CLU_SIGN_KEYCHAIN: keychain, CLU_CHROMIUM_SRC: src }
    // Keychain and signing steps take tens of seconds while a build loads the Mac.
  }, 120_000)

  /** A stand-in with a renderer helper and a resource. */
  function nested(name) {
    const app = standIn(join(dir, name), { id: 'org.example.rio' })
    const helper = join(app, 'Contents', 'Helpers', 'Rio Helper (Renderer).app')
    mkdirSync(join(helper, 'Contents', 'MacOS'), { recursive: true })
    copyFileSync('/usr/bin/true', join(helper, 'Contents', 'MacOS', 'Rio Helper (Renderer)'))
    writeFileSync(join(helper, 'Contents', 'Info.plist'), '<?xml version="1.0" encoding="UTF-8"?><plist version="1.0"><dict><key>CFBundleExecutable</key><string>Rio Helper (Renderer)</string><key>CFBundleIdentifier</key><string>org.example.rio.helper.renderer</string><key>CFBundlePackageType</key><string>APPL</string></dict></plist>')
    mkdirSync(join(app, 'Contents', 'Resources'))
    writeFileSync(join(app, 'Contents', 'Resources', 'resource.txt'), 'one')
    return app
  }
  const requirement = (app) => execFileSync('codesign', ['-d', '-r-', app], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] })
  const entitlements = (path) => execFileSync('codesign', ['-d', '--entitlements', '-', '--xml', path], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] })

  it('gives two different builds the same designated requirement, with no cdhash in it', { timeout: 120_000 }, () => {
    const a = nested('a')
    const b = nested('b')
    writeFileSync(join(b, 'Contents', 'Resources', 'resource.txt'), 'two')
    for (const app of [a, b]) {
      const r = signDev([app], env)
      expect(r.out).toContain('self-signed, no passkey entitlement')
      expect(r.status).toBe(0)
      expect(spawnSync('codesign', ['--verify', '--deep', '--strict', app]).status).toBe(0)
    }
    expect(requirement(a)).toMatch(/^designated => identifier "org.example.signing" and certificate (root|leaf) = H"[0-9a-f]{40}"\n$/)
    expect(requirement(b)).toBe(requirement(a))
    const cdhash = (app) => /CDHash=(\w+)/.exec(spawnSync('codesign', ['-dvvv', app], { encoding: 'utf8' }).stderr)?.[1]
    expect(cdhash(a)).not.toBe(cdhash(b))
    // Chromium's entitlements per part, and no passkey entitlement or hardened runtime.
    const helper = join(a, 'Contents', 'Helpers', 'Rio Helper (Renderer).app')
    expect(entitlements(helper)).toContain('com.apple.security.cs.allow-jit')
    expect(entitlements(a)).toContain('com.apple.security.device.camera')
    expect(entitlements(a)).not.toContain('public-key-credential')
    expect(spawnSync('codesign', ['-dvv', a], { encoding: 'utf8' }).stderr).toMatch(/flags=0x0\(none\)/)
  })

  // The build install-rio.sh installs is out/Release/Rio.app as sign-dev.sh signed it, so with a feed it
  // carries Sparkle and the update keys, as a package does.
  it.runIf(SPARKLE_CACHED)('embeds Sparkle and the update keys before signing when a feed is configured', { timeout: 120_000 }, () => {
    const app = nested('feed')
    const key = join(dir, 'ed25519.key')
    execFileSync('node', [join(SCRIPTS, 'sparkle.mjs'), 'keygen', key], { stdio: 'pipe' })
    const feed = 'https://example.org/rio/appcast.xml'
    const r = signDev([app], { ...env, CLU_UPDATE_FEED_URL: feed, CLU_UPDATE_KEY_FILE: key })
    expect(r.out).toContain(`embedded Sparkle with the feed ${feed}`)
    expect(r.status).toBe(0)
    const info = join(app, 'Contents', 'Info.plist')
    const read = (k) => execFileSync('plutil', ['-extract', k, 'raw', '-o', '-', info], { encoding: 'utf8' }).trim()
    expect(read('SUFeedURL')).toBe(feed)
    expect(read('SUPublicEDKey')).toBe(execFileSync('node', [join(SCRIPTS, 'sparkle.mjs'), 'pubkey', key], { encoding: 'utf8' }).trim())
    expect(read('SUEnableSystemProfiling')).toBe('false')
    expect(existsSync(join(app, 'Contents', 'Frameworks', 'Sparkle.framework'))).toBe(true)
    expect(spawnSync('codesign', ['--verify', '--deep', '--strict', app]).status).toBe(0)
    expect(signDev(['--dry-run', app], { ...env, CLU_UPDATE_FEED_URL: 'http://example.com/appcast.xml', CLU_UPDATE_KEY_FILE: key }).out).toContain('would refuse: CLU_UPDATE_FEED_URL must be https')
  })

  it('plans without a provisioning profile and refuses a missing keychain, identity or password', { timeout: 120_000 }, () => {
    const app = nested('plan')
    const plan = signDev(['--dry-run', app], env)
    expect(plan.out).toContain(`--keychain ${keychain} --timestamp=none`)
    expect(plan.out).not.toContain('provisioning profile')
    expect(plan.out).not.toContain('--entitlements <entitlements>')
    expect(plan.status).toBe(0)
    expect(signDev(['--dry-run', app], { ...env, CLU_SIGN_KEYCHAIN: join(dir, 'none.keychain-db') }).out).toContain('would refuse: no signing keychain at')
    expect(signDev(['--dry-run', app], { ...env, CLU_SIGN_IDENTITY: 'Someone Else' }).out).toContain('has no code signing identity "Someone Else"')
    const bare = join(dir, 'bare')
    mkdirSync(bare)
    copyFileSync(keychain, join(bare, 'rio-signing.keychain-db'))
    const r = signDev(['--dry-run', app], { ...env, CLU_SIGN_KEYCHAIN: join(bare, 'rio-signing.keychain-db') })
    expect(r.out).toContain('would refuse: no keychain password at')
    expect(r.status).toBe(1)
  })

  it('plans a package with the keychain, without notarization, and refuses a notary profile', { timeout: 120_000 }, () => {
    const app = nested('pkg-plan')
    const r = pkg(['--app', app, '--out', join(dir, 'pkg-plan-out'), '--dry-run'], env)
    expect(r.out).toContain('self-signed from')
    expect(r.out).toContain(`codesign --sign 'Test Self-Signed' --keychain ${keychain} --timestamp=none`)
    expect(r.out).not.toMatch(/notarytool|sign_chrome\.py|-o runtime|would refuse/)
    expect(r.status).toBe(0)
    const notary = pkg(['--app', app, '--dry-run'], { ...env, CLU_NOTARY_PROFILE: 'clu-notary' })
    expect(notary.out).toContain('would refuse: CLU_NOTARY_PROFILE with CLU_SIGN_KEYCHAIN')
    expect(notary.status).toBe(1)
  })

  it.runIf(SPARKLE_CACHED)('packages a self-signed Rio.dmg whose app keeps the requirement', { timeout: 120_000 }, () => {
    const app = nested('pkg')
    const out = join(dir, 'pkg-out')
    const r = pkg(['--app', app, '--out', out], env)
    expect(r.out).not.toContain('error')
    expect(r.status).toBe(0)
    const summary = readFileSync(join(out, 'package.txt'), 'utf8')
    expect(summary).toContain('signature: Test Self-Signed')
    expect(summary).toMatch(/designated requirement: identifier "org.example.signing" and certificate (root|leaf) = H"[0-9a-f]{40}"/)
    expect(summary).toContain('notarized: no')
    expect(summary).toMatch(/gatekeeper: .*rejected/)
    expect(spawnSync('codesign', ['--verify', join(out, 'Rio.dmg')]).status).toBe(0)
  })
})

describe('publish-release.sh repository', () => {
  const publish = (env) => {
    const base = Object.fromEntries(Object.entries(process.env).filter(([k]) => !k.startsWith('CLU_')))
    const r = spawnSync(join(SCRIPTS, 'publish-release.sh'), ['--out', join(tmp, 'no-package')], { cwd: REPO, env: { ...base, CLU_PACKAGE_ENV: join(tmp, 'none.env'), ...env }, encoding: 'utf8', timeout: 60_000 })
    return { status: r.status, out: `${r.stdout}${r.stderr}` }
  }
  it('has no default repository and refuses until CLU_RELEASES_REPO names one', () => {
    const none = publish({})
    expect(none.status).toBe(1)
    expect(none.out).toContain('set CLU_RELEASES_REPO')
    expect(publish({ CLU_RELEASES_REPO: 'not a repo' }).out).toContain('set CLU_RELEASES_REPO')
    const set = publish({ CLU_RELEASES_REPO: 'example/rio-updates' })
    expect(set.out).not.toContain('CLU_RELEASES_REPO')
    expect(set.out).toContain('no-package/package.txt')
  })
})

describe('never the default browser', () => {
  it('has no code that sets the default browser or registers Rio with LaunchServices', () => {
    const files = execFileSync('git', ['ls-files', 'chromium/clu', 'chromium/patches', 'scripts', 'src'], { cwd: REPO, encoding: 'utf8' })
      .split('\n')
      .filter((f) => f && !f.endsWith('package.test.mjs'))
    const banned = /SetAsDefaultBrowser|SetAsDefaultClientForScheme|LSSetDefaultHandlerForURLScheme|setDefaultApplicationAtURL|--make-default-browser|lsregister[^\n]*\s-(f|R)\b/
    const hits = files.filter((f) => existsSync(join(REPO, f)) && banned.test(readFileSync(join(REPO, f), 'utf8')))
    expect(hits).toEqual([])
  })

  it('asks macOS only from the button the user presses (platform.requestDefaultBrowser, CluDefaultBrowser)', () => {
    const files = execFileSync('git', ['ls-files', 'chromium/clu', 'chromium/patches'], { cwd: REPO, encoding: 'utf8' })
      .split('\n')
      .filter((f) => f && !f.includes('browsertest'))
    const hits = files.filter((f) => existsSync(join(REPO, f)) && /StartSetAsDefault/.test(readFileSync(join(REPO, f), 'utf8')))
    expect(hits).toEqual(['chromium/clu/browser/ui/webui/clu_default_browser.cc'])
    const service = readFileSync(join(REPO, 'chromium/clu/browser/ui/webui/clu_default_browser.cc'), 'utf8')
    expect(service.match(/StartSetAsDefault/g)).toHaveLength(1)
    // Only CluPlatformHandler, the bridge's platform.requestDefaultBrowser, calls Request().
    const callers = files.filter((f) => existsSync(join(REPO, f)) && /CluDefaultBrowser::Get\(\)\.Request\(/.test(readFileSync(join(REPO, f), 'utf8')))
    expect(callers).toEqual(['chromium/clu/browser/ui/webui/clu_platform_handler.cc'])
  })
})

describe.skipIf(!shellcheck)('M12 scripts', () => {
  it('are shellcheck-clean', () => {
    const files = ['fetch-sparkle.sh', 'package.sh', 'package-smoke.sh', 'update-smoke.sh'].map((f) => join(SCRIPTS, f))
    const r = spawnSync(shellcheck, ['-x', ...files], { encoding: 'utf8' })
    expect(`${r.stdout}${r.stderr}`).toBe('')
  })
})
