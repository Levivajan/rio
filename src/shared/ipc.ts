// IPC contract composition. One region per lane; a lane edits only its own block.
import type { z } from 'zod'
import { coreIpc } from './ipc/core'
import type { CommandDef, IpcResult } from './ipc/define'
// #region clu:platform (edit only inside this block)
import { platformIpc } from './ipc/platform'
// #endregion clu:platform
// #region clu:data (edit only inside this block)
import { dataIpc } from './ipc/data'
// #endregion clu:data
// #region clu:spaces (edit only inside this block)
import { spacesIpc } from './ipc/spaces'
// #endregion clu:spaces
// #region clu:engine (edit only inside this block)
import { engineIpc } from './ipc/engine'
// #endregion clu:engine
// #region clu:capabilities (edit only inside this block)
import { capabilitiesIpc } from './ipc/capabilities'
// #endregion clu:capabilities
// #region clu:extensions (edit only inside this block)
import { extensionsIpc } from './ipc/extensions'
// #endregion clu:extensions
// #region clu:tweaks (edit only inside this block)
import { tweaksIpc } from './ipc/tweaks'
// #endregion clu:tweaks
// #region clu:tabs (edit only inside this block)
import { tabsIpc } from './ipc/tabs'
// #endregion clu:tabs
// #region clu:archive (edit only inside this block)
import { archiveIpc } from './ipc/archive'
// #endregion clu:archive
// #region clu:shell (edit only inside this block)
import { shellIpc } from './ipc/shell'
// #endregion clu:shell
// #region clu:theme (edit only inside this block)
import { themeIpc } from './ipc/theme'
// #endregion clu:theme
// #region clu:command (edit only inside this block)
import { commandIpc } from './ipc/command'
// #endregion clu:command
// #region clu:split (edit only inside this block)
import { splitIpc } from './ipc/split'
// #endregion clu:split
// #region clu:multitask (edit only inside this block)
import { multitaskIpc } from './ipc/multitask'
// #endregion clu:multitask
// #region clu:quality (edit only inside this block)
import { qualityIpc } from './ipc/quality'
// #endregion clu:quality

export * from './ipc/define'

export const contract = {
  ...coreIpc,
  // #region clu:platform (edit only inside this block)
  ...platformIpc,
  // #endregion clu:platform
  // #region clu:data (edit only inside this block)
  ...dataIpc,
  // #endregion clu:data
  // #region clu:spaces (edit only inside this block)
  ...spacesIpc,
  // #endregion clu:spaces
  // #region clu:engine (edit only inside this block)
  ...engineIpc,
  // #endregion clu:engine
  // #region clu:capabilities (edit only inside this block)
  ...capabilitiesIpc,
  // #endregion clu:capabilities
  // #region clu:extensions (edit only inside this block)
  ...extensionsIpc,
  // #endregion clu:extensions
  // #region clu:tweaks (edit only inside this block)
  ...tweaksIpc,
  // #endregion clu:tweaks
  // #region clu:tabs (edit only inside this block)
  ...tabsIpc,
  // #endregion clu:tabs
  // #region clu:archive (edit only inside this block)
  ...archiveIpc,
  // #endregion clu:archive
  // #region clu:shell (edit only inside this block)
  ...shellIpc,
  // #endregion clu:shell
  // #region clu:theme (edit only inside this block)
  ...themeIpc,
  // #endregion clu:theme
  // #region clu:command (edit only inside this block)
  ...commandIpc,
  // #endregion clu:command
  // #region clu:split (edit only inside this block)
  ...splitIpc,
  // #endregion clu:split
  // #region clu:multitask (edit only inside this block)
  ...multitaskIpc,
  // #endregion clu:multitask
  // #region clu:quality (edit only inside this block)
  ...qualityIpc
  // #endregion clu:quality
} as const satisfies Record<string, CommandDef>

export type Contract = typeof contract
export type CommandName = keyof Contract
export type Input<N extends CommandName> = z.input<Contract[N]['input']>
export type ParsedInput<N extends CommandName> = z.output<Contract[N]['input']>
export type Output<N extends CommandName> = Contract[N]['output']

export const COMMAND_NAMES = Object.keys(contract) as CommandName[]

type AreaOf<N extends string> = N extends `${infer A}.${string}` ? A : never
export type Area = AreaOf<CommandName>
// eslint-disable-next-line @typescript-eslint/no-empty-object-type
type Method<N extends CommandName> = {} extends Input<N>
  ? (payload?: Input<N>) => Promise<IpcResult<Output<N>>>
  : (payload: Input<N>) => Promise<IpcResult<Output<N>>>

/** `window.clu.<area>.<verb>(payload)`, built from the contract by each bridge. */
export type CluAreas = {
  [A in Area]: { [N in CommandName as N extends `${A}.${infer V}` ? V : never]: Method<N> }
}
