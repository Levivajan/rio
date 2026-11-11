// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_APP_ICON_CLU_APP_ICON_H_
#define CLU_BROWSER_APP_ICON_CLU_APP_ICON_H_

#include <optional>
#include <string>
#include <string_view>

// Rio's app icon in the Dock (docs/chromium/icons.md). The setting appearance.appIcon names one of seven icons
// (src/shared/appIcons.ts): the default is the bundle's own icon, the six alternates are PNGs in Clu's resources and in
// the Dock tile plug-in (dock_tile/). Nothing here writes into the app bundle, which would break its signature.
namespace clu {

// The user defaults key (in AppIconDefaultsDomain()) the Dock tile plug-in reads while Rio is closed: an alternate's
// id, absent for the default.
inline constexpr char kAppIconDefaultsKey[] = "clu.appIcon";
// The distributed notification, after the domain, that tells a loaded plug-in to read the key again.
inline constexpr char kAppIconChangedSuffix[] = ".appIconChanged";

// The resource id of an alternate's PNG, 0 for the default and anything unknown.
int AppIconResourceId(std::string_view id);

// Shows icon `id` in the Dock while Rio runs (NSApp.applicationIconImage: the alternate's PNG, or nil for the bundle's
// icon when `id` is the default, unknown or its PNG is missing), mirrors it into the user defaults key and tells the
// Dock tile plug-in. Calling it again with the icon it shows does nothing, so every window may call it as it opens.
void ApplyAppIcon(std::string_view id);

// The id ApplyAppIcon() shows now: "" before the first call, the default for an id it could not show.
const std::string& AppliedAppIconForTesting();

// Rio's bundle id (base::apple::BaseBundleID()), unless a test set its own.
std::string AppIconDefaultsDomain();
// Tests: a defaults domain of their own, so parallel test processes never share the key; nullopt goes back to the
// bundle id. Never override BaseBundleID() itself: child processes find the browser by it.
void SetAppIconDefaultsDomainForTesting(std::optional<std::string> domain);

}  // namespace clu

#endif  // CLU_BROWSER_APP_ICON_CLU_APP_ICON_H_
