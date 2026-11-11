// Vite config for the Chromium WebUI bundles (docs/chromium/PLAN.md 2.3 build.sh, M2). Each WebUI is its own static
// bundle: index.html plus hashed assets/, no inline script or style, nothing loaded from the network. `fake` swaps the
// Mojo boot for the FakeBridge boot (the ui:fake harness); the product bundle never contains the fake.
import { resolve } from 'node:path'
import tailwindcss from '@tailwindcss/vite'
import react from '@vitejs/plugin-react'

export const RENDERER = resolve('src/renderer')

/** Entry html per bundle, relative to src/renderer. `harness` exists only in the fake build. */
export const ENTRIES = {
  frame: 'webui/frame/index.html',
  overlay: 'webui/overlay/index.html',
  settings: 'webui/settings/index.html',
  harness: 'webui/fake/index.html'
}

export function webuiConfig({ bundle, fake, outDir }) {
  return {
    configFile: false,
    root: RENDERER,
    base: './',
    logLevel: 'warn',
    plugins: [react(), tailwindcss()],
    resolve: { alias: { '@clu/webui-boot': resolve(RENDERER, `webui/boot-${fake ? 'fake' : 'mojo'}.ts`) } },
    build: {
      outDir,
      emptyOutDir: true,
      assetsDir: 'assets',
      target: 'chrome130',
      // The preload polyfill would inject an inline script; Chromium has modulepreload.
      modulePreload: { polyfill: false },
      // Every asset is a file the .grd lists, never a data: URL inlined into JS or CSS.
      assetsInlineLimit: 0,
      sourcemap: false,
      reportCompressedSize: false,
      rolldownOptions: { input: { index: resolve(RENDERER, ENTRIES[bundle]) } }
    }
  }
}
