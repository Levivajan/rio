// npm run ui:fake: builds the WebUI bundles with the FakeBridge and serves them on 127.0.0.1 under the WebUI CSP.
//   /                  the harness: the frame WebUI with the overlay WebUI stacked over it, one FakeHost
//   /frame/, /overlay/ each WebUI alone (its own FakeHost)
// Options: --port N (default 4178), --no-build (serve the last build in out/webui-fake). Ctrl+C stops it.
import { createServer } from 'node:http'
import { existsSync, readFileSync, statSync } from 'node:fs'
import { extname, join, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'
import { buildWebUi } from './build-webui.mjs'
import { HARNESS_CSP, WEBUI_CSP } from './webui-csp.mjs'

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.woff2': 'font/woff2',
  '.json': 'application/json'
}

/** Resolves a request path inside `root`, or null for anything outside it or missing. */
export function fileFor(root, urlPath) {
  let path
  try {
    path = decodeURIComponent(new URL(urlPath, 'http://x').pathname)
  } catch {
    return null
  }
  const abs = resolve(root, `.${path}`)
  if (abs !== root && !abs.startsWith(root + sep)) return null
  const file = existsSync(abs) && statSync(abs).isDirectory() ? join(abs, 'index.html') : abs
  return existsSync(file) && statSync(file).isFile() ? file : null
}

export function serve(root, port) {
  const server = createServer((req, res) => {
    const file = req.method === 'GET' || req.method === 'HEAD' ? fileFor(root, req.url ?? '/') : null
    if (!file) {
      res.writeHead(404, { 'content-type': 'text/plain' }).end('not found')
      return
    }
    const harness = file === join(root, 'index.html')
    res.writeHead(200, {
      'content-type': TYPES[extname(file)] ?? 'application/octet-stream',
      'content-security-policy': harness ? HARNESS_CSP : WEBUI_CSP,
      'x-content-type-options': 'nosniff',
      'cache-control': 'no-store'
    })
    res.end(req.method === 'HEAD' ? undefined : readFileSync(file))
  })
  return new Promise((ok, fail) => {
    server.once('error', fail)
    server.listen(port, '127.0.0.1', () => ok(server))
  })
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2)
  const at = args.indexOf('--port')
  const port = at >= 0 ? Number(args[at + 1]) : 4178
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    console.error(`bad --port ${args[at + 1]}`)
    process.exit(2)
  }
  const root = resolve('out/webui-fake')
  if (!args.includes('--no-build')) await buildWebUi({ fake: true, out: root })
  const server = await serve(root, port)
  console.log(`ui:fake on http://127.0.0.1:${port}/ (frame alone at /frame/, overlay at /overlay/?layer=top)`)
  const stop = () => server.close(() => process.exit(0))
  process.on('SIGINT', stop)
  process.on('SIGTERM', stop)
}
