// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_SITE_EXTENSION_H_
#define CLU_BROWSER_SITE_CLU_SITE_EXTENSION_H_

// Clu's component extension (docs/chromium/m7.md). It holds no files: it is the host the blocker's
// declarativeNetRequest rules belong to, and the isolated world Site tweaks and the zap picker run in. Component
// extensions never show in chrome://extensions, cannot be turned off and run in private windows. Its action exists
// only so Chromium counts the requests it blocks per tab (setExtensionActionOptions); Clu's action row leaves it out.
namespace clu {

// crx_file::id_util::GenerateId of the key below; chrome-browser-extensions-component_extensions_allowlist-
// allowlist.cc.patch names the same id, and CluSiteExtensionTest checks they agree.
inline constexpr char kCluExtensionId[] = "nefakicccnkadleninchkbeapobjmcnp";

// The public half of a key made for this extension; the private half was never kept (component extensions are not
// signed).
inline constexpr char kCluExtensionPublicKey[] =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAps7GA5wi8cV9/Q9V5omms/30NI66qgbg"
    "O7r2E2oTWcUgnIzHhCAWfj15DEAF0yEXAOVNRC0WHr1ccncW83J6lLkDYEssMqgNFpi40Hrcq6Vc"
    "d458Ar8Cx1wnq47R3CfjYa17Yu/ycAWkocdj95NrFzRoMij1pHQjj7L6rP/PZEry5uq+txdJMAwI"
    "dr8yS7i8krWfaJfyYjGNW8ug8Qn1gNO4oiBGz9Pfw3xTPs5yp52cYQCSbEG1WbMhrPt+EAyGjUua"
    "LnkTiJAcTBsotKhlBi9EOSTk/5YIWb8TUwpyilwyNO9o+LIntPO189MRGXSWN3ZXhhqqQXcA+yQt"
    "yYvpWwIDAQAB";

// The manifest ComponentLoader::Add() reads. "<all_urls>" lets the tweaks and the picker reach every web page;
// "declarativeNetRequest" is all a block or allow rule needs.
inline constexpr char kCluExtensionManifest[] = R"({
  "manifest_version": 3,
  "name": "Rio",
  "version": "1",
  "description": "Rio's content blocker and Site tweaks.",
  "key": "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAps7GA5wi8cV9/Q9V5omms/30NI66qgbgO7r2E2oTWcUgnIzHhCAWfj15DEAF0yEXAOVNRC0WHr1ccncW83J6lLkDYEssMqgNFpi40Hrcq6Vcd458Ar8Cx1wnq47R3CfjYa17Yu/ycAWkocdj95NrFzRoMij1pHQjj7L6rP/PZEry5uq+txdJMAwIdr8yS7i8krWfaJfyYjGNW8ug8Qn1gNO4oiBGz9Pfw3xTPs5yp52cYQCSbEG1WbMhrPt+EAyGjUuaLnkTiJAcTBsotKhlBi9EOSTk/5YIWb8TUwpyilwyNO9o+LIntPO189MRGXSWN3ZXhhqqQXcA+yQtyYvpWwIDAQAB",
  "permissions": ["declarativeNetRequest"],
  "host_permissions": ["<all_urls>"],
  "incognito": "spanning",
  "action": {}
})";

// Where ComponentLoader roots it, under the resources directory. Nothing is read from there.
inline constexpr char kCluExtensionRoot[] = "clu_site";

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_SITE_EXTENSION_H_
