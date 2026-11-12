# Contributing to Rio

Thanks for helping. Bug reports with exact steps are the most useful contribution.

## Before you start

- Search existing issues first. For a bug, use the bug template and fill in every field, including the Rio version from the About Rio window.
- For a larger change (a new feature, a new setting, anything that touches a Chromium patch), open an issue and wait for an answer before you write code.
- Rio is a patch set over Chromium. Keep patches against upstream files small. `scripts/chromium/apply.sh --stats` shows the budget (60 patched files, 1,500 patched lines). Put new code in Rio's own sources under `chromium/` instead of patching upstream files.

## Setting up

See the README. The UI (`src/`, `e2e/`) builds and tests without a Chromium checkout:

```sh
npm install
npm run verify
```

Changes under `chromium/` need a Chromium build to test; say in the pull request what you ran and on which macOS.

## Pull requests

- One change per pull request, with tests next to the code (`*.test.ts`, `*.test.tsx`, `e2e/chromium-webui/*.webui.ts`, and `*_unittest.cc` next to the C++ code).
- Do not edit generated files (`mojo-wire.gen.ts`, the generated mojom and command table). Change `src/shared` and run `npm run gen:chromium`.
- Do not add feature flags or hidden toggles; a feature either ships on or is not merged.
- Do not use names, logos, icons or copy from other browsers in UI strings, assets or bundle ids. `scripts/chromium/check-brand.sh` and `src/shared/banned-words.test.ts` enforce this.
- Commit messages use Conventional Commits: `feat:`, `fix:`, `test:`, `docs:`, `refactor:`, `chore:`.
- Never commit API keys, signing identities, provisioning profiles or update keys.
