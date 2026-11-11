// Vitest config (npm run test:unit, plain Node). `node` runs shared logic and scripts, `renderer` the React UI in jsdom.
import react from '@vitejs/plugin-react'
import { defineConfig } from 'vitest/config'

export default defineConfig({
  test: {
    pool: 'forks',
    maxWorkers: 4,
    testTimeout: 5000,
    restoreMocks: true,
    unstubEnvs: true,
    projects: [
      {
        extends: true,
        test: {
          name: 'node',
          environment: 'node',
          include: ['src/shared/**/*.test.ts', 'scripts/**/*.test.mjs', 'scripts/**/*.test.ts']
        }
      },
      {
        extends: true,
        plugins: [react()],
        test: {
          name: 'renderer',
          environment: 'jsdom',
          include: ['src/renderer/**/*.test.{ts,tsx}', 'src/test-utils/**/*.test.tsx'],
          setupFiles: ['src/test-utils/renderer-setup.ts']
        }
      }
    ]
  }
})
