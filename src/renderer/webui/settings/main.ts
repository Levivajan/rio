// chrome://clu-settings/ entry (clu://settings, the internal:settings surface in a tab): connect the bridge, then load
// the settings page (src/renderer/internal/settings), which reads clu() as it runs. Every import is dynamic, and zod's
// jitless switch comes first: the page's sections import contract modules (src/shared/ipc/data.ts) that build schemas
// as they load, and a schema built before the switch probes `new Function`, which Trusted Types reports.
await import('../../common/bridge/zod-jitless')
const { bootBridge } = await import('@clu/webui-boot')
await bootBridge('settings')
await import('../../internal/settings/main')

export {}
