// ESLint flat config.
import { builtinModules } from 'node:module'
import js from '@eslint/js'
import reactHooks from 'eslint-plugin-react-hooks'
import globals from 'globals'
import tseslint from 'typescript-eslint'

export default tseslint.config(
  { ignores: ['out/**', 'build/**', 'release/**', 'test-results/**', 'playwright-report/**', 'node_modules/**', 'coverage/**'] },
  js.configs.recommended,
  ...tseslint.configs.strict,
  {
    languageOptions: { ecmaVersion: 2024, sourceType: 'module', globals: { ...globals.node } },
    rules: {
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_', varsIgnorePattern: '^_', destructuredArrayIgnorePattern: '^_' }],
      '@typescript-eslint/no-non-null-assertion': 'error'
    }
  },
  {
    files: ['src/renderer/**/*.{ts,tsx}'],
    plugins: { 'react-hooks': reactHooks },
    languageOptions: { globals: { ...globals.browser } },
    rules: {
      'react-hooks/rules-of-hooks': 'error',
      'react-hooks/exhaustive-deps': 'warn',
      'no-restricted-syntax': [
        'error',
        { selector: "JSXAttribute[name.name='dangerouslySetInnerHTML']", message: 'dangerouslySetInnerHTML is banned (CSP Trusted Types).' },
        { selector: "MemberExpression[object.name='window'][property.name='clu']", message: 'Call clu() from common/clu.ts: one CluBridge for Mojo and the fake.' }
      ],
      'no-restricted-imports': [
        'error',
        {
          // The UI runs as Chromium WebUI with no Node (docs/chromium/m2-ui-bridge.md).
          paths: [...builtinModules.map((name) => ({ name, message: 'No Node modules in the UI.' }))],
          patterns: [{ group: ['node:*'], message: 'No Node modules in the UI.' }]
        }
      ]
    }
  },
  {
    // The accessor itself, tests and the harness boot set or stub window.clu.
    files: ['src/renderer/common/clu.ts', 'src/renderer/**/*.test.{ts,tsx}', 'src/renderer/**/testing.ts', 'src/renderer/webui/boot-fake.ts'],
    rules: {
      'no-restricted-syntax': [
        'error',
        { selector: "JSXAttribute[name.name='dangerouslySetInnerHTML']", message: 'dangerouslySetInnerHTML is banned (CSP Trusted Types).' }
      ]
    }
  },
  {
    files: ['e2e/**/*.ts'],
    rules: {
      'no-restricted-syntax': [
        'error',
        { selector: "CallExpression[callee.object.name=/^(test|describe)$/][callee.property.name='only']", message: 'No .only in e2e.' },
        { selector: "CallExpression[callee.object.object.name='test'][callee.object.property.name='describe'][callee.property.name='only']", message: 'No describe.only.' },
        { selector: "CallExpression[callee.property.name='waitForTimeout']", message: 'Wait for a condition, never a fixed timeout.' }
      ]
    }
  },
  {
    files: ['scripts/**/*.{mjs,ts}', '*.config.{js,ts}', 'eslint.config.js'],
    languageOptions: { globals: { ...globals.node } }
  }
)
