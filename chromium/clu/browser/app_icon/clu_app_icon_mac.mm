// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/app_icon/clu_app_icon.h"

#include <optional>
#include <utility>

#import <AppKit/AppKit.h>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/sys_string_conversions.h"
#include "clu/grit/clu_app_icon_resources.h"
#include "ui/base/resource/resource_bundle.h"

namespace clu {

namespace {

constexpr char kDefaultAppIcon[] = "flatWarm";

struct Alternate {
  std::string_view id;
  int resource_id;
};

// The ids of src/shared/appIcons.ts; their PNGs are rendered from assets/brand/app-icons by make-branding.mjs.
constexpr Alternate kAlternates[] = {
    {"airySun", IDR_CLU_APP_ICON_AIRY_SUN},
    {"ocean", IDR_CLU_APP_ICON_OCEAN},
    {"flatBlue", IDR_CLU_APP_ICON_FLAT_BLUE},
    {"sunset", IDR_CLU_APP_ICON_SUNSET},
    {"warm", IDR_CLU_APP_ICON_WARM},
    {"dusk", IDR_CLU_APP_ICON_DUSK},
};

std::string& Applied() {
  static base::NoDestructor<std::string> applied;
  return *applied;
}

std::optional<std::string>& DomainForTesting() {
  static base::NoDestructor<std::optional<std::string>> domain;
  return *domain;
}

NSImage* AlternateImage(int resource_id) {
  const std::string_view png =
      ui::ResourceBundle::GetSharedInstance().GetRawDataResource(resource_id);
  if (png.empty()) {
    return nil;
  }
  NSImage* image = [[NSImage alloc]
      initWithData:[NSData dataWithBytes:png.data() length:png.size()]];
  return image.valid ? image : nil;
}

// The plug-in's copy of the choice: the alternate's id, or no value for the default. Written only when it differs,
// so a launch with the same icon writes nothing.
void MirrorToDefaults(const std::string& id) {
  const std::string domain_name = AppIconDefaultsDomain();
  base::apple::ScopedCFTypeRef<CFStringRef> domain =
      base::SysUTF8ToCFStringRef(domain_name);
  base::apple::ScopedCFTypeRef<CFStringRef> key =
      base::SysUTF8ToCFStringRef(kAppIconDefaultsKey);
  base::apple::ScopedCFTypeRef<CFPropertyListRef> stored(
      CFPreferencesCopyAppValue(key.get(), domain.get()));
  const CFStringRef stored_id =
      base::apple::CFCast<CFStringRef>(stored.get());
  const bool is_default = id == kDefaultAppIcon;
  if (is_default ? !stored
                 : stored_id && base::SysCFStringRefToUTF8(stored_id) == id) {
    return;
  }
  if (is_default) {
    CFPreferencesSetAppValue(key.get(), nullptr, domain.get());
  } else {
    base::apple::ScopedCFTypeRef<CFStringRef> value =
        base::SysUTF8ToCFStringRef(id);
    CFPreferencesSetAppValue(key.get(), value.get(), domain.get());
  }
  CFPreferencesAppSynchronize(domain.get());
  NSString* name = base::SysUTF8ToNSString(
      base::StrCat({domain_name, kAppIconChangedSuffix}));
  [NSDistributedNotificationCenter.defaultCenter
      postNotificationName:name
                    object:nil
                  userInfo:nil
        deliverImmediately:YES];
}

}  // namespace

int AppIconResourceId(std::string_view id) {
  for (const Alternate& alternate : kAlternates) {
    if (alternate.id == id) {
      return alternate.resource_id;
    }
  }
  return 0;
}

void ApplyAppIcon(std::string_view id) {
  const int resource_id = AppIconResourceId(id);
  NSImage* image = resource_id ? AlternateImage(resource_id) : nil;
  if (resource_id && !image) {
    LOG(WARNING) << "clu: the " << id << " app icon is missing; showing the default";
  }
  const std::string shown = image ? std::string(id) : kDefaultAppIcon;
  if (Applied() == shown) {
    return;
  }
  Applied() = shown;
  // nil puts the bundle's icon back.
  NSApp.applicationIconImage = image;
  MirrorToDefaults(shown);
}

const std::string& AppliedAppIconForTesting() {
  return Applied();
}

std::string AppIconDefaultsDomain() {
  return DomainForTesting().value_or(std::string(base::apple::BaseBundleID()));
}

void SetAppIconDefaultsDomainForTesting(std::optional<std::string> domain) {
  DomainForTesting() = std::move(domain);
}

}  // namespace clu
