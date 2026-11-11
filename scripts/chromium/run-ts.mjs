// Runs a TS script under Vite's module runner (the repo's own TS pipeline, extensionless imports and
// import.meta.glob included): `node scripts/chromium/run-ts.mjs <file.ts> [args]` calls its exported main(args).
import { resolve } from 'node:path'
import { runnerImport } from 'vite'

const [file, ...args] = process.argv.slice(2)
if (!file) {
  console.error('usage: node scripts/chromium/run-ts.mjs <file.ts> [args]')
  process.exit(2)
}
const { module } = await runnerImport(resolve(file), { configFile: false, logLevel: 'error' })
if (typeof module.main !== 'function') {
  console.error(`${file} exports no main(args)`)
  process.exit(2)
}
process.exitCode = await module.main(args)
