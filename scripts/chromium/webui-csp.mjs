// The Content-Security-Policy of chrome://clu-frame and chrome://clu-overlay. The C++ WebUIDataSource sets exactly this
// (network::mojom::CSPDirectiveName overrides, docs/chromium/m2-ui-bridge.md), and the ui:fake server sends it as a
// header, so the harness runs under the policy the browser enforces. chrome://resources serves Mojo's bindings.js,
// chrome://favicon2 the tab favicons.
export const WEBUI_CSP = [
  "default-src 'none'",
  "script-src 'self' chrome://resources",
  "style-src 'self'",
  "img-src 'self' data: blob: chrome://favicon2 chrome://image",
  "font-src 'self'",
  "connect-src 'none'",
  "media-src 'none'",
  "object-src 'none'",
  "frame-src 'none'",
  "worker-src 'none'",
  "manifest-src 'none'",
  "base-uri 'none'",
  "form-action 'none'",
  "require-trusted-types-for 'script'",
  "trusted-types 'none'"
].join('; ')

/** The harness page (fake build only) embeds the two WebUIs as same-origin frames. */
export const HARNESS_CSP = WEBUI_CSP.replace("frame-src 'none'", "frame-src 'self'")
