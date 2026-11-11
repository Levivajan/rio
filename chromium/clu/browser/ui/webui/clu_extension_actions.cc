// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_extension_actions.h"

#include <algorithm>
#include <utility>

#include "base/base64.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/extensions/extensions_container.h"
#include "chrome/browser/ui/toolbar/toolbar_action_view_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "clu/browser/site/clu_site_extension.h"
#include "components/sessions/content/session_tab_helper.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "extensions/common/permissions/permission_message.h"
#include "extensions/common/permissions/permissions_data.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace clu {

namespace {

// The row's buttons are 16 px icons; Retina needs the 2x pixels.
constexpr int kIconSize = 16;
constexpr float kIconScale = 2.0f;

// A PNG data URL of the action's icon (badge drawn in, grayed out where the action does nothing), or nullopt.
std::optional<std::string> IconDataUrl(const ui::ImageModel& model) {
  // Extension action icons are images; a vector icon would need a color provider and never comes from an extension.
  if (!model.IsImage()) {
    return std::nullopt;
  }
  const gfx::ImageSkia image = model.Rasterize(nullptr);
  if (image.isNull()) {
    return std::nullopt;
  }
  const gfx::ImageSkiaRep& rep = image.GetRepresentation(kIconScale);
  if (rep.is_null()) {
    return std::nullopt;
  }
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(rep.GetBitmap(),
                                        /*discard_transparency=*/false);
  if (!png) {
    return std::nullopt;
  }
  std::string url =
      base::StrCat({"data:image/png;base64,", base::Base64Encode(*png)});
  if (url.size() > kMaxIconDataUrl) {
    return std::nullopt;
  }
  return url;
}

std::string BadgeText(Profile* profile,
                      const extensions::Extension& extension,
                      content::WebContents* active) {
  extensions::ExtensionAction* action =
      extensions::ExtensionActionManager::Get(profile)->GetExtensionAction(
          extension);
  if (!action || !active) {
    return std::string();
  }
  const SessionID tab_id = sessions::SessionTabHelper::IdForTab(active);
  return action->GetDisplayBadgeText(
      tab_id.is_valid() ? tab_id.id() : extensions::ExtensionAction::kDefaultTabId);
}

const extensions::Extension* EnabledExtension(Profile* profile,
                                              const std::string& id) {
  return extensions::ExtensionRegistry::Get(profile)
      ->enabled_extensions()
      .GetByID(id);
}

}  // namespace

bool IsListedExtension(Profile* profile, const std::string& id) {
  if (id == kCluExtensionId) {
    return false;
  }
  ToolbarActionsModel* model = ToolbarActionsModel::Get(profile);
  return model && model->HasAction(id);
}

std::optional<ActionRowSnapshot> BuildActionRow(BrowserWindowInterface* browser,
                                                Profile* profile,
                                                const std::string& profile_id,
                                                content::WebContents* active) {
  ToolbarActionsModel* model = ToolbarActionsModel::Get(profile);
  if (!model || !model->actions_initialized()) {
    return std::nullopt;
  }
  ExtensionsContainer* container =
      browser ? ExtensionsContainer::From(*browser) : nullptr;
  ActionRowSnapshot row;
  row.profile_id = profile_id;
  const std::vector<ToolbarActionsModel::ActionId>& pinned =
      model->pinned_action_ids();
  for (const ToolbarActionsModel::ActionId& id : model->action_ids()) {
    const extensions::Extension* extension = EnabledExtension(profile, id);
    if (id == kCluExtensionId || !extension) {
      continue;
    }
    ActionButtonSnapshot button;
    button.id = id;
    button.name = base::UTF16ToUTF8(model->GetExtensionName(id));
    button.title = button.name;
    button.pinned = std::ranges::find(pinned, id) != pinned.end();
    button.badge = BadgeText(profile, *extension, active);
    if (ToolbarActionViewModel* action =
            container ? container->GetActionForId(id) : nullptr) {
      button.title = base::UTF16ToUTF8(action->GetActionTitle(active));
      button.enabled = action->IsEnabled(active);
      button.icon =
          IconDataUrl(action->GetIcon(active, gfx::Size(kIconSize, kIconSize)));
    }
    row.actions.push_back(std::move(button));
  }
  if (row.actions.empty()) {
    return std::nullopt;
  }
  // Pinned first, in the order Chromium keeps the pins; the rest by name.
  auto pin_index = [&](const std::string& id) {
    return static_cast<size_t>(std::ranges::find(pinned, id) - pinned.begin());
  };
  std::ranges::stable_sort(row.actions, [&](const ActionButtonSnapshot& a,
                                            const ActionButtonSnapshot& b) {
    if (a.pinned != b.pinned) {
      return a.pinned;
    }
    if (a.pinned) {
      return pin_index(a.id) < pin_index(b.id);
    }
    return a.name < b.name;
  });
  return row;
}

std::vector<ExtensionInfoSnapshot> BuildExtensionInfos(Profile* profile) {
  std::vector<ExtensionInfoSnapshot> out;
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile);
  ToolbarActionsModel* model = ToolbarActionsModel::Get(profile);
  const extensions::ExtensionSet all =
      registry->GenerateInstalledExtensionsSet(
          extensions::ExtensionRegistry::ENABLED |
          extensions::ExtensionRegistry::DISABLED);
  for (const auto& extension : all) {
    if (extension->id() == kCluExtensionId ||
        !extensions::ui_util::ShouldDisplayInExtensionSettings(*extension)) {
      continue;
    }
    ExtensionInfoSnapshot info;
    info.id = extension->id();
    info.name = extension->name();
    info.version = extension->VersionString();
    info.enabled = registry->enabled_extensions().Contains(extension->id());
    info.has_action = model && model->HasAction(extension->id());
    info.has_options =
        extensions::OptionsPageInfo::HasOptionsPage(extension.get());
    info.pinned = model && model->IsActionPinned(extension->id());
    for (const extensions::PermissionMessage& message :
         extension->permissions_data()->GetPermissionMessages()) {
      info.permissions.push_back(base::UTF16ToUTF8(message.message()));
    }
    out.push_back(std::move(info));
  }
  std::ranges::sort(out, {}, &ExtensionInfoSnapshot::name);
  return out;
}

bool RunExtensionAction(BrowserWindowInterface* browser, const std::string& id) {
  if (!browser || id == kCluExtensionId) {
    return false;
  }
  ExtensionsContainer* container = ExtensionsContainer::From(*browser);
  ToolbarActionViewModel* action =
      container ? container->GetActionForId(id) : nullptr;
  if (!action) {
    return false;
  }
  action->ExecuteUserAction(
      ToolbarActionViewModel::InvocationSource::kToolbarButton);
  return true;
}

base::CallbackListSubscription ObserveActionIcon(BrowserWindowInterface* browser,
                                                 const std::string& id,
                                                 base::RepeatingClosure callback) {
  ExtensionsContainer* container =
      browser ? ExtensionsContainer::From(*browser) : nullptr;
  ToolbarActionViewModel* action =
      container ? container->GetActionForId(id) : nullptr;
  return action ? action->RegisterIconUpdateObserver(std::move(callback))
                : base::CallbackListSubscription();
}

bool SetExtensionPinned(Profile* profile, const std::string& id, bool pinned) {
  // ToolbarActionsModel refuses pin changes from an off-the-record profile.
  if (profile->IsOffTheRecord() || !IsListedExtension(profile, id)) {
    return false;
  }
  ToolbarActionsModel* model = ToolbarActionsModel::Get(profile);
  if (model->IsActionForcePinned(id) || model->IsActionPinned(id) == pinned) {
    return false;
  }
  model->SetActionVisibility(id, pinned);
  return true;
}

}  // namespace clu
