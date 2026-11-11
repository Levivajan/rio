// Contract for the extensions module.
import { empty, id, rect, str, z } from '../schema'
import { defineCommand, out } from './define'

// `pinned`: the action shows in the sidebar's action row; every extension is listed in the extensions menu.
export interface ExtensionInfo { id: string; name: string; version: string; enabled: boolean; icon: string | null; hasAction: boolean; hasOptions: boolean; permissions: string[]; pinned: boolean }
const extensionId = () => str(64).regex(/^[a-p]{32}$/)

export const extensionsIpc = {
  'blocking.setSiteEnabled': defineCommand({ input: z.object({ tabId: id(), enabled: z.boolean() }).strict() }),
  // The Mini window toolbar has the button too.
  'siteControls.open': defineCommand({ input: z.object({ anchor: rect() }).strict(), allow: ['ui', 'mini-ui'] }),
  // The panel's Share tile and the copy toast's share button: macOS's share picker for the
  // page's URL, at `anchor` (the tile, frame coordinates); no anchor points it at the toast.
  'siteControls.share': defineCommand({ input: z.object({ anchor: rect().optional() }).strict(), allow: ['overlay'] }),
  // The panel's Screenshot tile: Chromium's own region capture on the page, which copies the shot and offers
  // to save it.
  // The Developer Mode strip's capture button runs it too.
  'siteControls.screenshot': defineCommand({ input: empty(), allow: ['overlay', 'ui'] }),
  // Developer Mode for the site of row `tabId`: the Developer Mode row in Site controls. Stored per site, on
  // by default for localhost and 127.0.0.1.
  'siteControls.setDevMode': defineCommand({ input: z.object({ tabId: id(), on: z.boolean() }).strict(), allow: ['overlay'] }),
  // The Developer Mode strip's tools on the page the window shows: DevTools on its console or its last panel, the
  // element picker, a reload that skips the cache.
  'siteControls.devTool': defineCommand({ input: z.object({ tool: z.enum(['console', 'devtools', 'inspect', 'hardReload']) }).strict(), allow: ['ui'] }),
  // The panel's Capture text tile: Chromium's region selection on the page; macOS reads the text in the
  // region and it goes to the clipboard with a toast.
  'siteControls.captureText': defineCommand({ input: empty(), allow: ['overlay'] }),
  'extensions.list': defineCommand({ input: z.object({ profileId: id() }).strict(), output: out<ExtensionInfo[]>(), allow: ['ui', 'overlay', 'internal:extensions'] }),
  'extensions.remove': defineCommand({ input: z.object({ profileId: id(), id: extensionId() }).strict(), allow: ['ui', 'overlay', 'internal:extensions'] }),
  'extensions.openOptions': defineCommand({ input: z.object({ profileId: id(), id: extensionId() }).strict(), allow: ['ui', 'overlay', 'internal:extensions'] }),
  'extensions.setEnabled': defineCommand({ input: z.object({ profileId: id(), id: extensionId(), enabled: z.boolean() }).strict(), allow: ['ui', 'overlay', 'internal:extensions'] }),
  'extensions.pageProfile': defineCommand({ input: empty(), output: out<{ profileId: string | null }>(), allow: ['internal:extensions'] }),
  'extensions.openStore': defineCommand({ input: empty(), allow: ['internal:extensions'] }),
  // The import summary's Reinstall button: Chromium's own Web Store install prompt for `id` over the
  // window (the dialog "Add to Chrome" shows). Resolves once it is installed (or already was); `rejected` with reason
  // `cancelled` when the user says no, `failed` when the store or the install fails.
  'extensions.installFromStore': defineCommand({ input: z.object({ id: extensionId() }).strict(), allow: ['ui', 'internal:settings'] }),
  'extensions.respondInstall': defineCommand({ input: z.object({ requestId: id(), allow: z.boolean() }).strict(), allow: ['overlay'] }),
  // The action row and the extensions menu (docs/chromium/m7.md): the window's own profile, the action runs on the page
  // the window shows, and its popup hangs from `anchor` (the clicked button, frame or overlay coordinates).
  'extensions.runAction': defineCommand({ input: z.object({ id: extensionId(), anchor: rect() }).strict(), allow: ['ui', 'overlay', 'mini-ui'] }),
  'extensions.setPinned': defineCommand({ input: z.object({ id: extensionId(), pinned: z.boolean() }).strict(), allow: ['ui', 'overlay'] }),
  'extensions.openMenu': defineCommand({ input: z.object({ anchor: rect() }).strict(), allow: ['ui', 'overlay', 'mini-ui'] })
} as const
