// Playwright config for the Chromium WebUI harness (npm run test:webui): headless Chromium against `npm run ui:fake`.
// Never a window. At most 2 workers.
import { defineConfig } from '@playwright/test'

const PORT = Number(process.env.CLU_WEBUI_PORT) || 4178

export default defineConfig({
  testDir: 'e2e/chromium-webui',
  testMatch: /.*\.webui\.ts$/,
  workers: 2,
  fullyParallel: true,
  forbidOnly: true,
  timeout: 30_000,
  expect: { timeout: 5_000 },
  outputDir: 'test-results/webui',
  reporter: [['list']],
  use: {
    baseURL: `http://127.0.0.1:${PORT}/`,
    browserName: 'chromium',
    headless: true,
    viewport: { width: 1280, height: 800 },
    deviceScaleFactor: 2,
    // Sandboxed macOS sessions cannot register Mach services, which Chromium's child-process launcher needs; the
    // harness is one static same-origin document tree, so a single process runs it the same way.
    launchOptions: { args: process.platform === 'darwin' ? ['--single-process'] : [] }
  },
  webServer: {
    command: `node scripts/chromium/ui-fake.mjs --port ${PORT}`,
    url: `http://127.0.0.1:${PORT}/`,
    reuseExistingServer: false,
    timeout: 120_000
  }
})
