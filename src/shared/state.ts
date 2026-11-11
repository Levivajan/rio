// State snapshot composition. Each lane's fields come from src/shared/state/<lane>.ts.
import { defaultCoreApp, defaultCoreWindow, type CoreAppFields, type CoreWindowFields, type WindowInit } from './state/core'
import type { ProfileId, SpaceId, SplitId, TabId, WindowId, FolderId } from './types'
// #region clu:platform (edit only inside this block)
import { defaultPlatformApp, type PlatformAppFields } from './state/platform'
// #endregion clu:platform
// #region clu:data (edit only inside this block)
import { defaultDataApp, defaultDataTop, type DataAppFields, type DataTopFields } from './state/data'
// #endregion clu:data
// #region clu:spaces (edit only inside this block)
import {
  defaultSpacesProfile, defaultSpacesSpace, defaultSpacesTop, defaultSpacesWindow,
  type ProfileInit, type SpaceInit, type SpacesProfileFields, type SpacesSpaceFields, type SpacesTopFields, type SpacesWindowFields
} from './state/spaces'
// #endregion clu:spaces
// #region clu:engine (edit only inside this block)
import { defaultEngineWindow, defaultPageFields, type EngineWindowFields, type PageFields } from './state/engine'
// #endregion clu:engine
// #region clu:capabilities (edit only inside this block)
import { defaultCapabilitiesTop, defaultCapabilitiesWindow, type CapabilitiesTopFields, type CapabilitiesWindowFields } from './state/capabilities'
// #endregion clu:capabilities
// #region clu:extensions (edit only inside this block)
import {
  defaultExtensionsProfile, defaultExtensionsTab, defaultExtensionsWindow,
  type ExtensionsProfileFields, type ExtensionsTabFields, type ExtensionsWindowFields
} from './state/extensions'
// #endregion clu:extensions
// #region clu:tweaks (edit only inside this block)
import { defaultTweaksTab, type TweaksTabFields } from './state/tweaks'
// #endregion clu:tweaks
// #region clu:tabs (edit only inside this block)
import {
  defaultTabsSpace, defaultTabsTab, defaultTabsTop, defaultTabsWindow,
  type TabInit, type TabsSpaceFields, type TabsTabFields, type TabsTopFields, type TabsWindowFields
} from './state/tabs'
// #endregion clu:tabs
// #region clu:archive (edit only inside this block)
import { defaultArchiveTop, type ArchiveTopFields } from './state/archive'
// #endregion clu:archive
// #region clu:shell (edit only inside this block)
import { defaultShellWindow, type ShellWindowFields } from './state/shell'
// #endregion clu:shell
// #region clu:theme (edit only inside this block)
import { defaultThemeApp, defaultThemeSpace, type ThemeAppFields, type ThemeSpaceFields } from './state/theme'
// #endregion clu:theme
// #region clu:command (edit only inside this block)
import { defaultCommandWindow, type CommandWindowFields } from './state/command'
// #endregion clu:command
// #region clu:split (edit only inside this block)
import { defaultSplitTab, defaultSplitTop, defaultSplitWindow, type SplitTabFields, type SplitTopFields, type SplitWindowFields } from './state/split'
// #endregion clu:split
// #region clu:multitask (edit only inside this block)
import { defaultMultitaskWindow, type MultitaskWindowFields } from './state/multitask'
// #endregion clu:multitask

export type { PageFields } from './state/engine'
export type { NodeRef, TabInit, TabSection, FolderState, SidebarRequest } from './state/tabs'
export type { SpaceInit, ProfileInit } from './state/spaces'
export type { WindowInit, OverlaySurfaceState, ToastState } from './state/core'
export type { SplitState } from './state/split'

export type TabState = PageFields & TabsTabFields & SplitTabFields & ExtensionsTabFields & TweaksTabFields
export type WindowState = CoreWindowFields & SpacesWindowFields & TabsWindowFields & ShellWindowFields & CapabilitiesWindowFields &
  CommandWindowFields & SplitWindowFields & MultitaskWindowFields & EngineWindowFields & ExtensionsWindowFields
export type SpaceState = SpacesSpaceFields & ThemeSpaceFields & TabsSpaceFields
export type ProfileState = SpacesProfileFields & ExtensionsProfileFields
export type AppState = CoreAppFields & ThemeAppFields & DataAppFields & PlatformAppFields

export type CluState = {
  rev: number
  app: AppState
  profiles: Record<ProfileId, ProfileState>
  spaces: Record<SpaceId, SpaceState>
  tabs: Record<TabId, TabState>
  windows: Record<WindowId, WindowState>
  splits: Record<SplitId, SplitTopFields['splits'][string]>
  folders: Record<FolderId, TabsTopFields['folders'][string]>
} & SpacesTopFields & Omit<TabsTopFields, 'folders'> & Omit<SplitTopFields, 'splits'> & CapabilitiesTopFields & ArchiveTopFields & DataTopFields

export function defaultTabState(init: TabInit): TabState {
  return {
    // #region clu:engine (edit only inside this block)
    ...defaultPageFields(),
    // #endregion clu:engine
    // #region clu:split (edit only inside this block)
    ...defaultSplitTab(),
    // #endregion clu:split
    // #region clu:extensions (edit only inside this block)
    ...defaultExtensionsTab(),
    // #endregion clu:extensions
    // #region clu:tweaks (edit only inside this block)
    ...defaultTweaksTab(),
    // #endregion clu:tweaks
    // #region clu:tabs (edit only inside this block)
    ...defaultTabsTab(init)
    // #endregion clu:tabs
  }
}

export function defaultWindowState(init: WindowInit): WindowState {
  return {
    ...defaultCoreWindow(init),
    // #region clu:spaces (edit only inside this block)
    ...defaultSpacesWindow(init.activeSpaceId),
    // #endregion clu:spaces
    // #region clu:tabs (edit only inside this block)
    ...defaultTabsWindow(),
    // #endregion clu:tabs
    // #region clu:shell (edit only inside this block)
    ...defaultShellWindow(),
    // #endregion clu:shell
    // #region clu:capabilities (edit only inside this block)
    ...defaultCapabilitiesWindow(),
    // #endregion clu:capabilities
    // #region clu:command (edit only inside this block)
    ...defaultCommandWindow(),
    // #endregion clu:command
    // #region clu:split (edit only inside this block)
    ...defaultSplitWindow(),
    // #endregion clu:split
    // #region clu:multitask (edit only inside this block)
    ...defaultMultitaskWindow(),
    // #endregion clu:multitask
    // #region clu:engine (edit only inside this block)
    ...defaultEngineWindow(),
    // #endregion clu:engine
    // #region clu:extensions (edit only inside this block)
    ...defaultExtensionsWindow()
    // #endregion clu:extensions
  }
}

export function defaultSpaceState(init: SpaceInit): SpaceState {
  return {
    // #region clu:spaces (edit only inside this block)
    ...defaultSpacesSpace(init),
    // #endregion clu:spaces
    // #region clu:theme (edit only inside this block)
    ...defaultThemeSpace(),
    // #endregion clu:theme
    // #region clu:tabs (edit only inside this block)
    ...defaultTabsSpace()
    // #endregion clu:tabs
  }
}

export function defaultProfileState(init: ProfileInit): ProfileState {
  return {
    // #region clu:spaces (edit only inside this block)
    ...defaultSpacesProfile(init),
    // #endregion clu:spaces
    // #region clu:extensions (edit only inside this block)
    ...defaultExtensionsProfile()
    // #endregion clu:extensions
  }
}

export function defaultState(): CluState {
  return {
    rev: 0,
    app: {
      ...defaultCoreApp(),
      // #region clu:theme (edit only inside this block)
      ...defaultThemeApp(),
      // #endregion clu:theme
      // #region clu:data (edit only inside this block)
      ...defaultDataApp(),
      // #endregion clu:data
      // #region clu:platform (edit only inside this block)
      ...defaultPlatformApp()
      // #endregion clu:platform
    },
    profiles: {},
    spaces: {},
    tabs: {},
    windows: {},
    splits: {},
    folders: {},
    // #region clu:spaces (edit only inside this block)
    ...defaultSpacesTop(),
    // #endregion clu:spaces
    // #region clu:tabs (edit only inside this block)
    favorites: defaultTabsTop().favorites,
    // #endregion clu:tabs
    // #region clu:capabilities (edit only inside this block)
    ...defaultCapabilitiesTop(),
    // #endregion clu:capabilities
    // #region clu:archive (edit only inside this block)
    ...defaultArchiveTop(),
    // #endregion clu:archive
    // #region clu:data (edit only inside this block)
    ...defaultDataTop(),
    // #endregion clu:data
    // #region clu:split (edit only inside this block)
    ...(({ splits: _splits, ...rest }) => rest)(defaultSplitTop())
    // #endregion clu:split
  }
}

// ---- patches -------------------------------------------------------------------------------

export type PatchOp = { op: 'set'; path: string[]; value: unknown } | { op: 'del'; path: string[] }
export type StateMessage = { kind: 'snapshot'; rev: number; state: CluState } | { kind: 'patch'; rev: number; ops: PatchOp[] }

/** Collections whose children are records with one creator (PLAN 3.14). */
export const RECORD_COLLECTIONS = ['tabs', 'windows', 'spaces', 'profiles', 'splits', 'folders'] as const

const FORBIDDEN_KEYS = new Set(['__proto__', 'prototype', 'constructor'])

export function isSafePath(path: readonly string[]): boolean {
  return path.length > 0 && path.every((k) => typeof k === 'string' && !FORBIDDEN_KEYS.has(k))
}

function isObject(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null
}

/** Immutable set: copies only the objects along the path (structural sharing). Missing parents are created. */
export function setIn<T>(root: T, path: readonly string[], value: unknown): T {
  if (path.length === 0) return value as T
  const [head, ...rest] = path as [string, ...string[]]
  const base: Record<string, unknown> = isObject(root) ? root : {}
  const copy: Record<string, unknown> = Array.isArray(base) ? ([...base] as unknown as Record<string, unknown>) : { ...base }
  copy[head] = setIn(base[head], rest, value)
  return copy as T
}

export function delIn<T>(root: T, path: readonly string[]): T {
  if (path.length === 0 || !isObject(root)) return root
  const [head, ...rest] = path as [string, ...string[]]
  if (!(head in root)) return root
  if (rest.length === 0) {
    if (Array.isArray(root)) {
      const copy = [...root]
      copy.splice(Number(head), 1)
      return copy as T
    }
    const { [head]: _removed, ...copy } = root
    return copy as T
  }
  const copy: Record<string, unknown> = Array.isArray(root) ? ([...root] as unknown as Record<string, unknown>) : { ...root }
  copy[head] = delIn(root[head], rest)
  return copy as T
}

export function getIn(root: unknown, path: readonly string[]): unknown {
  let cur = root
  for (const k of path) {
    if (!isObject(cur)) return undefined
    cur = cur[k]
  }
  return cur
}

export function applyPatch(state: CluState, ops: readonly PatchOp[]): CluState {
  let next = state
  for (const op of ops) {
    if (!isSafePath(op.path)) continue
    next = op.op === 'set' ? setIn(next, op.path, op.value) : delIn(next, op.path)
  }
  return next
}
