// chrome://clu-frame/ entry: connect the bridge, then load the frame UI (src/renderer/ui), which reads clu() as it runs.
import { bootBridge } from '@clu/webui-boot'

await bootBridge('frame')
await import('../../ui/main')
