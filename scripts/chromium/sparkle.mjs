#!/usr/bin/env node
// Sparkle's side of Rio's updates without the Keychain (docs/chromium/m12.md). Sparkle's own generate_keys keeps the
// private key in the login keychain; Rio keeps it in a file the release owner keeps (~/.clu/sparkle/ed25519.key), in the format
// Sparkle's sign_update --ed-key-file reads: base64 of the 32-byte Ed25519 seed. Signatures are plain Ed25519 over
// the archive's bytes, which is what Sparkle checks against SUPublicEDKey.
//
//   node scripts/chromium/sparkle.mjs keygen <key file>          writes a new key (mode 600), prints the public key
//   node scripts/chromium/sparkle.mjs pubkey <key file>          prints the public key (SUPublicEDKey)
//   node scripts/chromium/sparkle.mjs sign <file> --key <key>    prints the archive's sparkle:edSignature
//   node scripts/chromium/sparkle.mjs appcast --out <xml> --archive <zip> --url <archive url> --version <v>
//        --short <short version> --min-system <12.0> --key <key> [--from <existing xml>] [--keep 10]
//   node scripts/chromium/sparkle.mjs serve --root <dir> [--port 0] [--log <file>]
//        serves <dir> on 127.0.0.1 only (GET and HEAD, no dot files, nothing outside <dir>); prints "listening <port>"
import { createPrivateKey, createPublicKey, generateKeyPairSync, sign, verify } from 'node:crypto'
import { appendFileSync, createReadStream, existsSync, readFileSync, realpathSync, statSync, writeFileSync } from 'node:fs'
import { createServer } from 'node:http'
import { basename, join, relative, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

// DER prefixes of an Ed25519 PKCS#8 private key and SPKI public key; the 32 key bytes follow.
const PKCS8_ED25519 = Buffer.from('302e020100300506032b657004220420', 'hex')
const SPKI_ED25519 = Buffer.from('302a300506032b6570032100', 'hex')
export const MAX_FEED_URL = 2048
export const SPARKLE_NS = 'http://www.andymatuschak.org/xml-namespaces/sparkle'

const fail = (message) => {
  throw new Error(message)
}

/** The 32-byte seed in a key file, or an error naming what is wrong with it. */
export function readSeed(path) {
  if (!existsSync(path)) fail(`no key file at ${path}`)
  const text = readFileSync(path, 'utf8').trim()
  if (!/^[A-Za-z0-9+/]+={0,2}$/.test(text)) fail(`${path} is not base64`)
  const seed = Buffer.from(text, 'base64')
  if (seed.length === 96) fail(`${path} holds an old 96-byte Sparkle key; export a seed with Sparkle's generate_keys -x`)
  if (seed.length !== 32) fail(`${path} holds ${seed.length} bytes, expected a 32-byte Ed25519 seed`)
  return seed
}

const privateKeyOf = (seed) => createPrivateKey({ key: Buffer.concat([PKCS8_ED25519, seed]), format: 'der', type: 'pkcs8' })

/** SUPublicEDKey: base64 of the 32-byte public key. */
export function publicKeyOf(seed) {
  const der = createPublicKey(privateKeyOf(seed)).export({ format: 'der', type: 'spki' })
  return Buffer.from(der.subarray(SPKI_ED25519.length)).toString('base64')
}

export function signBytes(seed, bytes) {
  return sign(null, bytes, privateKeyOf(seed)).toString('base64')
}

export function verifyBytes(publicKey, bytes, signature) {
  const raw = Buffer.from(publicKey, 'base64')
  if (raw.length !== 32) return false
  const key = createPublicKey({ key: Buffer.concat([SPKI_ED25519, raw]), format: 'der', type: 'spki' })
  return verify(null, bytes, key, Buffer.from(signature, 'base64'))
}

export function keygen(path) {
  if (existsSync(path)) fail(`${path} exists; Rio never overwrites an update key`)
  const { privateKey } = generateKeyPairSync('ed25519')
  const seed = Buffer.from(privateKey.export({ format: 'jwk' }).d, 'base64url')
  writeFileSync(path, `${seed.toString('base64')}\n`, { mode: 0o600, flag: 'wx' })
  return publicKeyOf(seed)
}

/** The feed rule CluUpdateService applies (clu_update_rules.cc): https, or http to this Mac only. */
export function isAllowedFeedUrl(text) {
  if (typeof text !== 'string' || text.length === 0 || text.length > MAX_FEED_URL) return false
  let url
  try {
    url = new URL(text)
  } catch {
    return false
  }
  if (url.username || url.password || url.hash || text.includes('#') || !url.hostname) return false
  if (url.protocol === 'https:') return true
  return url.protocol === 'http:' && ['127.0.0.1', '[::1]', 'localhost'].includes(url.hostname)
}

const VERSION = /^\d{1,9}(\.\d{1,9}){0,4}$/
const escapeXml = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;').replace(/'/g, '&apos;')

/** One appcast <item>: CFBundleVersion as sparkle:version, the short version as the title. */
export function appcastItem({ version, short, minSystem, url, length, signature, date = new Date() }) {
  if (!VERSION.test(version)) fail(`bad version '${version}' (CFBundleVersion, digits and dots)`)
  if (!VERSION.test(short)) fail(`bad short version '${short}' (CFBundleShortVersionString)`)
  if (!VERSION.test(minSystem)) fail(`bad minimum system version '${minSystem}'`)
  if (!isAllowedFeedUrl(url)) fail(`the archive URL must be https, or http to this Mac: ${url}`)
  if (!Number.isSafeInteger(length) || length <= 0) fail(`bad archive length ${length}`)
  if (!/^[A-Za-z0-9+/]{86}==$/.test(signature)) fail('bad EdDSA signature')
  return [
    '    <item>',
    `      <title>Rio ${escapeXml(short)}</title>`,
    `      <pubDate>${date.toUTCString()}</pubDate>`,
    `      <sparkle:version>${escapeXml(version)}</sparkle:version>`,
    `      <sparkle:shortVersionString>${escapeXml(short)}</sparkle:shortVersionString>`,
    `      <sparkle:minimumSystemVersion>${escapeXml(minSystem)}</sparkle:minimumSystemVersion>`,
    `      <enclosure url="${escapeXml(url)}" length="${length}" type="application/octet-stream" sparkle:edSignature="${signature}"/>`,
    '    </item>'
  ].join('\n')
}

/** The <item> blocks of an appcast this script wrote, with their sparkle:version. Anything else in it is dropped. */
export function appcastItems(xml) {
  const items = []
  for (const m of String(xml).matchAll(/ {0,4}<item>[\s\S]*?<\/item>/g)) {
    const version = /<sparkle:version>([^<]{1,64})<\/sparkle:version>/.exec(m[0])?.[1]
    if (version && VERSION.test(version)) items.push({ version, xml: m[0].replace(/^ */, '    ') })
  }
  return items
}

/** The appcast with `item` first, an older item of the same version replaced, at most `keep` items. */
export function buildAppcast(item, version, previous = '', keep = 10) {
  const older = appcastItems(previous).filter((i) => i.version !== version)
  const items = [item, ...older.map((i) => i.xml)].slice(0, Math.max(1, keep))
  return [
    '<?xml version="1.0" encoding="utf-8"?>',
    `<rss version="2.0" xmlns:sparkle="${SPARKLE_NS}">`,
    '  <channel>',
    '    <title>Rio</title>',
    ...items,
    '  </channel>',
    '</rss>',
    ''
  ].join('\n')
}

/** Serves `root` on 127.0.0.1: GET and HEAD of plain files inside it, nothing else. */
export function createFeedServer(root, log) {
  const base = realpathSync(resolve(root))
  return createServer((req, res) => {
    const deny = (code) => {
      res.writeHead(code, { 'content-type': 'text/plain' })
      res.end()
      log?.(`${req.method} ${req.url} ${code}`)
    }
    if (req.method !== 'GET' && req.method !== 'HEAD') return deny(405)
    let path
    try {
      path = decodeURIComponent(new URL(req.url ?? '/', 'http://127.0.0.1').pathname)
    } catch {
      return deny(400)
    }
    if (path.includes('\0') || path.split('/').some((p) => p.startsWith('.'))) return deny(404)
    let file
    try {
      file = realpathSync(join(base, path))
    } catch {
      return deny(404)
    }
    const inside = relative(base, file)
    if (!inside || inside.startsWith('..') || inside.split(sep).includes('..')) return deny(404)
    const st = statSync(file)
    if (!st.isFile()) return deny(404)
    res.writeHead(200, { 'content-length': st.size, 'content-type': file.endsWith('.xml') ? 'application/xml' : 'application/octet-stream' })
    log?.(`${req.method} ${req.url} 200`)
    if (req.method === 'HEAD') return res.end()
    createReadStream(file).pipe(res)
  })
}

function args(argv) {
  const out = { _: [] }
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i]
    if (a.startsWith('--')) {
      if (i + 1 >= argv.length) fail(`${a} needs a value`)
      out[a.slice(2)] = argv[++i]
    } else out._.push(a)
  }
  return out
}

function main(argv) {
  const [cmd, ...rest] = argv
  const a = args(rest)
  switch (cmd) {
    case 'keygen':
      console.log(keygen(a._[0] ?? fail('keygen <key file>')))
      return
    case 'pubkey':
      console.log(publicKeyOf(readSeed(a._[0] ?? fail('pubkey <key file>'))))
      return
    case 'sign':
      console.log(signBytes(readSeed(a.key ?? fail('--key <key file>')), readFileSync(a._[0] ?? fail('sign <file>'))))
      return
    case 'appcast': {
      for (const k of ['out', 'archive', 'url', 'version', 'short', 'min-system', 'key']) if (!a[k]) fail(`appcast needs --${k}`)
      const bytes = readFileSync(a.archive)
      const seed = readSeed(a.key)
      const signature = signBytes(seed, bytes)
      const item = appcastItem({ version: a.version, short: a.short, minSystem: a['min-system'], url: a.url, length: bytes.length, signature })
      const previous = a.from ? readFileSync(a.from, 'utf8') : ''
      writeFileSync(a.out, buildAppcast(item, a.version, previous, Number(a.keep ?? 10)))
      console.log(`${basename(a.out)}: Rio ${a.short} (${a.version}), ${bytes.length} bytes, signed`)
      return
    }
    case 'serve': {
      const root = a.root ?? fail('serve --root <dir>')
      const log = a.log ? (line) => appendFileSync(a.log, `${line}\n`) : undefined
      const server = createFeedServer(root, log)
      server.listen(Number(a.port ?? 0), '127.0.0.1', () => console.log(`listening ${server.address().port}`))
      const stop = () => server.close(() => process.exit(0))
      process.on('SIGTERM', stop)
      process.on('SIGINT', stop)
      return
    }
    default:
      fail('usage: sparkle.mjs keygen|pubkey|sign|appcast|serve (see the header)')
  }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    main(process.argv.slice(2))
  } catch (e) {
    console.error(`sparkle.mjs: error: ${e.message}`)
    process.exit(1)
  }
}
