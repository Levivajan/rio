// Which image URLs a Clu WebUI shows for a page icon. In Chromium the browser sends chrome://favicon2 URLs, the only
// remote icon origin the WebUI CSP allows (scripts/chromium/webui-csp.mjs); the ui:fake seed uses clu://favicon/;
// inline images pass too. Anything else falls back to the letter tile.
const FAVICON2 = 'chrome://favicon2/?'
const CLU_FAVICON = 'clu://favicon/'
const DATA_IMAGE = /^data:image\/(png|gif|jpeg|webp|x-icon|vnd\.microsoft\.icon);/
const MAX_ICON_URL = 32 * 1024

export function safeIconSrc(src: unknown): string | null {
  if (typeof src !== 'string' || src.length > MAX_ICON_URL) return null
  return src.startsWith(FAVICON2) || src.startsWith(CLU_FAVICON) || DATA_IMAGE.test(src) ? src : null
}
