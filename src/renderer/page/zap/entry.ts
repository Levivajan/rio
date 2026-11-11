// page/zap.js (docs/chromium/m7.md): installs the zap picker in the page's isolated world. The browser appends
// `globalThis.__cluZap.update(bar); globalThis.__cluZap.pick()` and waits for the promise the last call returns.
import { zapPicker } from './picker'
import { zapTarget } from './selector'

zapPicker(zapTarget)
