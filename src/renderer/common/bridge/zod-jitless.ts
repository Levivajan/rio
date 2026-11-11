// Zod's JIT probes `new Function`, which the WebUI CSP (Trusted Types, no 'unsafe-eval') reports as a violation. The
// UI parses with zod too (and the fake builds the whole contract), so every WebUI boot imports this first, before any
// schema parses.
import { z } from 'zod'

z.config({ jitless: true })
