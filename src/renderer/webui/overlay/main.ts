// chrome://clu-overlay/ entry: connect the bridge, then load the overlay UI (src/renderer/overlay).
import { bootBridge } from '@clu/webui-boot'

await bootBridge('overlay')
await import('../../overlay/main')
