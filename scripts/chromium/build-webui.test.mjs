import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, describe, expect, it } from 'vitest'
import { checkBundle, grdFor, listFiles } from './build-webui.mjs'
import { fileFor } from './ui-fake.mjs'
import { HARNESS_CSP, WEBUI_CSP } from './webui-csp.mjs'

const dirs = []
function bundle(files) {
  const dir = mkdtempSync(join(tmpdir(), 'clu-webui-'))
  dirs.push(dir)
  for (const [name, text] of Object.entries(files)) {
    mkdirSync(join(dir, name, '..'), { recursive: true })
    writeFileSync(join(dir, name), text)
  }
  return dir
}
afterEach(() => {
  for (const d of dirs.splice(0)) rmSync(d, { recursive: true, force: true })
})

const CLEAN_HTML = '<!doctype html><html><head><script type="module" crossorigin src="./assets/index-a1.js"></script><link rel="stylesheet" href="./assets/main-b2.css"></head><body><div id="root"></div></body></html>'

describe('checkBundle', () => {
  it('passes a bundle of external module scripts and hashed assets', () => {
    expect(checkBundle(bundle({ 'index.html': CLEAN_HTML, 'assets/index-a1.js': 'export {}', 'assets/main-b2.css': 'a{color:red}' }), { product: true })).toEqual([])
  })

  it('flags everything the WebUI CSP would block or that loads from the network', () => {
    const dir = bundle({
      'index.html': '<html><head><script>alert(1)</script><style>a{}</style><script src="https://cdn.example.com/x.js"></script></head><body style="x" onload="y"></body></html>',
      'assets/a.css': '@import url("https://fonts.example.com/f.css"); b{background:url(//img.example.com/i.png)}',
      'assets/a.js': 'import "__vite-browser-external:fs"; const e = require("electron"); new Function("x"); window.cluFakeHost'
    })
    expect(checkBundle(dir, { product: true })).toEqual([
      'assets/a.css: remote URL in CSS',
      'assets/a.js: a Node built-in reached the bundle',
      'assets/a.js: imports electron',
      'assets/a.js: eval or new Function',
      'assets/a.js: the FakeBridge reached the product bundle',
      'index.html: inline script',
      'index.html: inline <style>',
      'index.html: style attribute',
      'index.html: inline event handler',
      'index.html: remote URL'
    ])
    expect(checkBundle(bundle({ 'assets/x.js': '' }), { product: false })).toEqual(['index.html missing'])
  })
})

describe('grdFor', () => {
  it('lists every file with its path under the WebUI host and a stable IDR name', () => {
    const dir = bundle({ 'index.html': CLEAN_HTML, 'assets/index-a1.js': '', 'assets/clu-icon-Z9.svg': '<svg/>' })
    const grd = grdFor('clu_frame_resources', 'CLU_FRAME', 'frame', listFiles(dir))
    expect(grd).toContain('<include name="IDR_CLU_FRAME_INDEX_HTML" file="frame/index.html" resource_path="index.html" type="BINDATA" />')
    expect(grd).toContain('<include name="IDR_CLU_FRAME_ASSETS_CLU_ICON_Z9_SVG" file="frame/assets/clu-icon-Z9.svg" resource_path="assets/clu-icon-Z9.svg" type="BINDATA" />')
    expect(grd).toContain('<output filename="grit/clu_frame_resources_map.cc" type="resource_file_map_source" />')
  })

  it('gzips the page JSON the blocker reads, and nothing else', () => {
    const dir = bundle({ 'index.html': CLEAN_HTML, 'page/zap.js': '', 'page/blocker-rules.json': '{}' })
    const grd = grdFor('clu_overlay_resources', 'CLU_OVERLAY', 'overlay', listFiles(dir))
    expect(grd).toContain('<include name="IDR_CLU_OVERLAY_PAGE_BLOCKER_RULES_JSON" file="overlay/page/blocker-rules.json" resource_path="page/blocker-rules.json" type="BINDATA" compress="gzip" />')
    expect(grd).toContain('<include name="IDR_CLU_OVERLAY_PAGE_ZAP_JS" file="overlay/page/zap.js" resource_path="page/zap.js" type="BINDATA" />')
    expect(grd).toContain('<include name="IDR_CLU_OVERLAY_INDEX_HTML" file="overlay/index.html" resource_path="index.html" type="BINDATA" />')
  })
})

describe('ui:fake server', () => {
  it('serves files inside the bundle only', () => {
    const dir = bundle({ 'index.html': CLEAN_HTML, 'frame/index.html': CLEAN_HTML, 'frame/assets/a.js': '' })
    expect(fileFor(dir, '/')).toBe(join(dir, 'index.html'))
    expect(fileFor(dir, '/frame/?x=1')).toBe(join(dir, 'frame/index.html'))
    expect(fileFor(dir, '/frame/assets/a.js')).toBe(join(dir, 'frame/assets/a.js'))
    for (const bad of ['/../package.json', '/%2e%2e/package.json', '/frame/missing.js', '/%E0%A4%A']) expect(fileFor(dir, bad)).toBeNull()
  })

  it('sends the WebUI CSP, which lets only the harness frame its two WebUIs', () => {
    expect(WEBUI_CSP).toContain("script-src 'self' chrome://resources")
    expect(WEBUI_CSP).toContain("trusted-types 'none'")
    expect(WEBUI_CSP).toContain("frame-src 'none'")
    expect(HARNESS_CSP).toContain("frame-src 'self'")
  })
})
