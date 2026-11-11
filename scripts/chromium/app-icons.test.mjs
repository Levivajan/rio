// The App icon picker's seven icons (docs/chromium/icons.md) are the same everywhere: the shared list, the settings
// registry in C++, the Mac code's resource table, the .grd, GN's plug-in resources and the rendered PNGs.
import { readFileSync } from 'node:fs'
import { join } from 'node:path'
import { describe, expect, it } from 'vitest'
import { APP_ICONS, DEFAULT_APP_ICON } from '../../src/shared/appIcons'
import { pngSize, renderIcon } from '../make-icon.mjs'
import { ALTERNATE_SIZE, ALTERNATES, ALTERNATES_OUT, ALTERNATES_SRC } from './make-branding.mjs'

const REPO = join(import.meta.dirname, '..', '..')
const read = (rel) => readFileSync(join(REPO, rel), 'utf8')
const APP_ICON = 'chromium/clu/browser/app_icon'

describe('app icons', () => {
  it('lists the same six alternates after the default in TypeScript, C++, the grd and GN', () => {
    expect(APP_ICONS.map((i) => i.id)).toEqual([DEFAULT_APP_ICON, ...ALTERNATES])
    const registry = read('chromium/clu/browser/settings/clu_settings_registry.cc')
    expect(registry).toContain('{kAppIconKey, SettingScope::kGlobal, &IsAppIcon, "\\"flatWarm\\""}')
    for (const { id } of APP_ICONS) expect(registry).toContain(`s == "${id}"`)
    const mac = read(`${APP_ICON}/clu_app_icon_mac.mm`)
    expect(mac).toContain('constexpr char kDefaultAppIcon[] = "flatWarm";')
    const table = [...mac.matchAll(/\{"(\w+)", (IDR_CLU_APP_ICON_\w+)\}/g)].map(([, id, idr]) => ({ id, idr }))
    expect(table.map((r) => r.id)).toEqual(ALTERNATES)
    const grd = read(`${APP_ICON}/clu_app_icon_resources.grd`)
    for (const { id, idr } of table) expect(grd).toContain(`<include name="${idr}" file="icons/${id}.png" type="BINDATA" />`)
    const gn = read(`${APP_ICON}/BUILD.gn`)
    for (const id of ALTERNATES) expect(gn).toContain(`"icons/${id}.png",`)
  })

  it('ships each alternate rendered from its artwork at 512 px', { timeout: 60_000 }, () => {
    for (const id of ALTERNATES) {
      const png = readFileSync(join(ALTERNATES_OUT, `${id}.png`))
      expect(pngSize(png)).toEqual({ width: ALTERNATE_SIZE, height: ALTERNATE_SIZE })
      expect(png.equals(renderIcon(readFileSync(join(ALTERNATES_SRC, `rio-${id}.svg`), 'utf8'), ALTERNATE_SIZE))).toBe(true)
    }
  })

  it('names the Dock tile plug-in the same way in the plist patch, the signing patch, GN and package.sh', () => {
    expect(read('chromium/patches/chrome-app-app-Info.plist.patch')).toContain('+\t<string>RioDockTile.docktileplugin</string>')
    expect(read('chromium/patches/chrome-installer-mac-signing-parts.py.patch')).toContain('Contents/PlugIns/RioDockTile.docktileplugin')
    expect(read('chromium/patches/chrome-BUILD.gn.patch')).toContain('+    bundle_deps += [ "//clu/browser/app_icon:dock_tile_plugin_bundle_data" ]')
    expect(read(`${APP_ICON}/BUILD.gn`)).toContain('_plugin_name = "RioDockTile"')
    expect(read(`${APP_ICON}/dock_tile/Info.plist`)).toContain('<string>RioDockTilePlugIn</string>')
    const plugin = read(`${APP_ICON}/dock_tile/rio_dock_tile_plugin.m`)
    expect(plugin).toContain('@implementation RioDockTilePlugIn')
    // The defaults key and the notification the browser writes and posts.
    const header = read(`${APP_ICON}/clu_app_icon.h`)
    expect(header).toContain('kAppIconDefaultsKey[] = "clu.appIcon";')
    expect(plugin).toContain('kAppIconKey = @"clu.appIcon";')
    expect(header).toContain('kAppIconChangedSuffix[] = ".appIconChanged";')
    expect(plugin).toContain('@".appIconChanged"')
    expect(read('scripts/chromium/package.sh')).toContain("-o -name '*.docktileplugin'")
    // The self-signed pass (sign-dev.sh and package.sh) seals it too.
    expect(read('scripts/chromium/common.sh')).toContain("-o -name '*.docktileplugin'")
  })
})
