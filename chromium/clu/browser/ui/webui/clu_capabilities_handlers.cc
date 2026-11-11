// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_capabilities_handlers.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/notreached.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "clu/browser/capabilities/clu_downloads.h"
#include "clu/browser/capabilities/clu_library_files.h"
#include "clu/browser/capabilities/clu_library_thumbs.h"
#include "clu/browser/capabilities/clu_site_permissions.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/webui/bridge/call_result.h"
#include "clu/browser/ui/webui/clu_window_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/webview/webview.h"

#if BUILDFLAG(IS_MAC)
#include "clu/browser/ui/webui/clu_library_drag.h"
#include "clu/browser/ui/webui/clu_share_services.h"
#endif

namespace clu {

namespace {

// As long as the Copy URL toast (CluPeek's kCopyToast).
constexpr base::TimeDelta kLibraryCopyToast = base::Milliseconds(1500);

mojom::CallResultPtr WindowGone() {
  return ErrorResult(mojom::ErrorCode::kRejected, "the window is closing");
}

DownloadAction ActionOf(mojom::DownloadActInputAction action) {
  switch (action) {
    case mojom::DownloadActInputAction::kPause:
      return DownloadAction::kPause;
    case mojom::DownloadActInputAction::kResume:
      return DownloadAction::kResume;
    case mojom::DownloadActInputAction::kCancel:
      return DownloadAction::kCancel;
    case mojom::DownloadActInputAction::kRetry:
      return DownloadAction::kRetry;
    case mojom::DownloadActInputAction::kOpen:
      return DownloadAction::kOpen;
    case mojom::DownloadActInputAction::kReveal:
      return DownloadAction::kReveal;
    case mojom::DownloadActInputAction::kRemove:
      return DownloadAction::kRemove;
    case mojom::DownloadActInputAction::kDrag:
      // Act() starts a drag itself (DragFile) and never asks for an action for it.
      NOTREACHED();
  }
  return DownloadAction::kPause;
}

gfx::Rect RectOf(const mojom::Rect& rect) {
  return gfx::ToEnclosingRect(
      gfx::RectF(static_cast<float>(rect.x), static_cast<float>(rect.y),
                 static_cast<float>(std::max(0.0, rect.width)),
                 static_cast<float>(std::max(0.0, rect.height))));
}

HostContentSettingsMap* MapOf(Profile* profile) {
  return profile ? HostContentSettingsMapFactory::GetForProfile(profile)
                 : nullptr;
}

mojom::CallResultPtr NoSettings() {
  return ErrorResult(mojom::ErrorCode::kRejected, "no site settings");
}

// An origin the guard let through as a URL, as the http or https origin it names. Nullopt with `*error` set for
// anything else.
std::optional<GURL> WebOrigin(const std::string& url,
                              mojom::CallResultPtr* error) {
  std::optional<GURL> origin = SitePermissionOrigin(url);
  if (!origin) {
    *error = ErrorResult(mojom::ErrorCode::kBadPayload, "not a web origin");
  }
  return origin;
}

LibrarySource SourceOf(mojom::DownloadFilesInputSource source) {
  return source == mojom::DownloadFilesInputSource::kMedia
             ? LibrarySource::kMedia
             : LibrarySource::kDownloads;
}

LibrarySource SourceOf(mojom::DownloadFileActInputSource source) {
  return source == mojom::DownloadFileActInputSource::kMedia
             ? LibrarySource::kMedia
             : LibrarySource::kDownloads;
}

LibrarySource SourceOf(mojom::DownloadThumbInputSource source) {
  return source == mojom::DownloadThumbInputSource::kMedia
             ? LibrarySource::kMedia
             : LibrarySource::kDownloads;
}

}  // namespace

CluDownloadHandler::CluDownloadHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}

CluDownloadHandler::~CluDownloadHandler() = default;

void CluDownloadHandler::Act(mojom::DownloadActInputPtr input,
                             ActCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  CluDownloads& downloads = window_->downloads();
  // Rows are addressed by id only: a path never comes from the WebUI (docs/plan/06-engine.md 2.10).
  const bool listed = std::ranges::any_of(
      downloads.Facts(), [&](const DownloadFacts& facts) {
        return !facts.hidden && facts.guid == input->id;
      });
  if (!listed) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such download"));
    return;
  }
  if (input->action == mojom::DownloadActInputAction::kDrag) {
    // As DownloadsDOMHandler::Drag: only a finished file still on disk drags.
    const base::FilePath path = downloads.CompletedPath(input->id);
    if (path.empty()) {
      std::move(callback).Run(RejectedResult("unavailable"));
      return;
    }
    DragFile(path, std::move(callback));
    return;
  }
  if (!downloads.Act(input->id, ActionOf(input->action))) {
    std::move(callback).Run(RejectedResult("unavailable"));
    return;
  }
  std::move(callback).Run(NullResult());
}

void CluDownloadHandler::Clear(ClearCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->downloads().ClearFinished();
  std::move(callback).Run(NullResult());
}

void CluDownloadHandler::OpenPopover(OpenPopoverCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // The same command as Cmd+Shift+J: Chromium's bubble, anchored to CluAnchorView.
  window_->RunCommand("downloads.open");
  std::move(callback).Run(NullResult());
}

void CluDownloadHandler::OpenFolder(OpenFolderCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->downloads().OpenFolder();
  std::move(callback).Run(NullResult());
}

void CluDownloadHandler::Files(mojom::DownloadFilesInputPtr input,
                               FilesCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // The first read of Downloads or the Desktop asks for macOS's Files and Folders access; a denied folder lists
  // nothing.
  const LibrarySource source = SourceOf(input->source);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          [](LibrarySource source) {
            return LibraryFilesValue(
                ListLibraryFolder(LibraryFolder(source), source));
          },
          source),
      base::BindOnce(
          [](FilesCallback callback, base::ListValue rows) {
            std::move(callback).Run(ValueResult(base::Value(std::move(rows))));
          },
          std::move(callback)));
}

void CluDownloadHandler::FileAct(mojom::DownloadFileActInputPtr input,
                                 FileActCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const LibrarySource source = SourceOf(input->source);
  const mojom::DownloadFileActInputAction action = input->action;
  if (action == mojom::DownloadFileActInputAction::kRename &&
      !input->new_name) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kBadPayload, "rename needs newName"));
    return;
  }
  std::optional<gfx::Rect> anchor;
  if (input->anchor) {
    anchor = RectOf(*input->anchor);
  }
  // Rename and Move to Trash change the folder on the blocking thread too; the rest act on the resolved file.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          [](LibrarySource source, mojom::DownloadFileActInputAction action,
             const std::string& name,
             const std::string& new_name) -> std::optional<base::FilePath> {
            const base::FilePath folder = LibraryFolder(source);
            if (action == mojom::DownloadFileActInputAction::kRename) {
              return RenameLibraryFile(folder, name, new_name);
            }
            std::optional<base::FilePath> path =
                ResolveLibraryFile(folder, name);
            if (path && action == mojom::DownloadFileActInputAction::kTrash &&
                !TrashLibraryFile(*path)) {
              return std::nullopt;
            }
            return path;
          },
          source, action, input->name, input->new_name.value_or("")),
      base::BindOnce(&CluDownloadHandler::OnFileResolved,
                     weak_ptr_factory_.GetWeakPtr(), action, anchor,
                     std::move(callback)));
}

void CluDownloadHandler::Thumb(mojom::DownloadThumbInputPtr input,
                               ThumbCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // The name resolves inside its folder on a blocking thread, as FileAct's does; QuickLook then renders it.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(
          [](LibrarySource source, const std::string& name) {
            return ResolveLibraryFile(LibraryFolder(source), name);
          },
          SourceOf(input->source), input->name),
      base::BindOnce(
          [](ThumbCallback callback, std::optional<base::FilePath> path) {
            if (!path) {
              std::move(callback).Run(
                  ErrorResult(mojom::ErrorCode::kNotFound, "no such file"));
              return;
            }
            LibraryThumbnail(
                *path, base::BindOnce(
                           [](ThumbCallback callback, std::string url) {
                             std::move(callback).Run(
                                 ValueResult(base::Value(std::move(url))));
                           },
                           std::move(callback)));
          },
          std::move(callback)));
}

void CluDownloadHandler::OnFileResolved(
    mojom::DownloadFileActInputAction action,
    std::optional<gfx::Rect> anchor,
    FileActCallback callback,
    std::optional<base::FilePath> path) {
  using Action = mojom::DownloadFileActInputAction;
  if (!path) {
    // A rename to a taken or bad name, or a Trash macOS refused, leaves the file where it was.
    std::move(callback).Run(
        action == Action::kRename || action == Action::kTrash
            ? ErrorResult(mojom::ErrorCode::kRejected, "file not changed")
            : ErrorResult(mojom::ErrorCode::kNotFound, "no such file"));
    return;
  }
  Profile* profile = window_ ? window_->profile() : nullptr;
  if (!profile) {
    std::move(callback).Run(WindowGone());
    return;
  }
  switch (action) {
    case Action::kReveal:
      platform_util::ShowItemInFolder(profile, *path);
      break;
    case Action::kOpen:
      platform_util::OpenItem(profile, *path, platform_util::OPEN_FILE,
                              platform_util::OpenOperationCallback());
      break;
    case Action::kCopy:
    case Action::kCopyPath:
      if (!CopyLibraryFile(*path, action == Action::kCopyPath)) {
        // No toast for a copy that put nothing on the clipboard.
        std::move(callback).Run(
            ErrorResult(mojom::ErrorCode::kRejected, "couldn't copy"));
        return;
      }
      // As Copy URL does, a toast says what landed on the clipboard, at the URL toast's place.
      window_->ShowToast(
          action == Action::kCopyPath ? "Copied Path" : "Copied File",
          std::nullopt, kLibraryCopyToast);
      break;
    case Action::kShare: {
#if BUILDFLAG(IS_MAC)
      views::WebView* frame = window_->shell().frame_view();
      if (frame && ShowFileSharePicker(frame->web_contents(),
                                       anchor.value_or(gfx::Rect()), *path)) {
        break;
      }
#endif
      std::move(callback).Run(
          ErrorResult(mojom::ErrorCode::kRejected, "couldn't share"));
      return;
    }
    case Action::kDrag:
      DragFile(*path, std::move(callback));
      return;
    case Action::kTrash:
    case Action::kRename:
      break;
  }
  std::move(callback).Run(NullResult());
}

void CluDownloadHandler::DragFile(
    const base::FilePath& path,
    base::OnceCallback<void(mojom::CallResultPtr)> callback) {
#if BUILDFLAG(IS_MAC)
  // The drag image is the QuickLook thumbnail the tile shows; QuickLook has it cached by the time a tile is dragged.
  LibraryThumbnail(path, base::BindOnce(&CluDownloadHandler::OnDragThumbnail,
                                        weak_ptr_factory_.GetWeakPtr(), path,
                                        std::move(callback)));
#else
  std::move(callback).Run(RejectedResult("no file drag"));
#endif
}

void CluDownloadHandler::OnDragThumbnail(
    const base::FilePath& path,
    base::OnceCallback<void(mojom::CallResultPtr)> callback,
    std::string png_data_url) {
#if BUILDFLAG(IS_MAC)
  views::WebView* frame = window_ ? window_->shell().frame_view() : nullptr;
  if (frame &&
      BeginLibraryFileDrag(frame->web_contents(), path, png_data_url)) {
    // The drag session sends the floating sidebar's widget no events until it ends, and no leave after the drop.
    window_->sidebar_peek().PointerTaken();
    std::move(callback).Run(NullResult());
    return;
  }
#endif
  // The press ended before the file resolved, or the window is closing: nothing drags.
  std::move(callback).Run(RejectedResult("drag ended"));
}

void CluDownloadHandler::SetButtonRect(
    mojom::DownloadSetButtonRectInputPtr input,
    SetButtonRectCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // The guard checked the numbers are finite; a rect outside the docked sidebar is never an anchor
  // (DownloadsButtonAnchor()).
  window_->shell().SetDownloadsButtonRect(
      input->rect ? std::optional<gfx::Rect>(RectOf(*input->rect))
                  : std::nullopt);
  std::move(callback).Run(NullResult());
}

// permission.* -------------------------------------------------------------------------------------------------------

CluPermissionHandler::CluPermissionHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}

CluPermissionHandler::~CluPermissionHandler() = default;

void CluPermissionHandler::WithProfile(
    const std::string& profile_id,
    base::OnceCallback<mojom::CallResultPtr(Profile*)> run,
    base::OnceCallback<void(mojom::CallResultPtr)> callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (profile_id == window_->state_profile_id()) {
    std::move(callback).Run(std::move(run).Run(window_->profile()));
    return;
  }
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces || !spaces->list().FindProfile(profile_id)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such profile"));
    return;
  }
  spaces->LoadProfile(
      profile_id,
      base::BindOnce(
          [](base::WeakPtr<CluPermissionHandler> self,
             base::OnceCallback<mojom::CallResultPtr(Profile*)> run,
             base::OnceCallback<void(mojom::CallResultPtr)> callback,
             Profile* profile) {
            if (!self || !profile) {
              std::move(callback).Run(
                  ErrorResult(mojom::ErrorCode::kNotFound, "no such profile"));
              return;
            }
            std::move(callback).Run(std::move(run).Run(profile));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(run),
          std::move(callback)));
}

void CluPermissionHandler::List(mojom::PermissionListInputPtr input,
                                ListCallback callback) {
  std::optional<GURL> origin;
  if (input->origin) {
    // Site controls on a page with no web origin (chrome://, file:) has no permissions to show: an empty list.
    origin = SitePermissionOrigin(*input->origin);
    if (!origin) {
      std::move(callback).Run(ValueResult(base::Value(base::ListValue())));
      return;
    }
  }
  const std::string profile_id = input->profile_id;
  WithProfile(
      profile_id,
      base::BindOnce(
          [](const std::string& profile_id, const std::optional<GURL>& origin,
             Profile* profile) {
            HostContentSettingsMap* map = MapOf(profile);
            if (!map) {
              return NoSettings();
            }
            base::ListValue rows;
            for (const SitePermissionRow& row :
                 ListSitePermissions(*map, origin)) {
              rows.Append(base::DictValue()
                              .Set("profileId", profile_id)
                              .Set("origin", row.origin)
                              .Set("permission", row.permission)
                              .Set("decision", row.allow ? "allow" : "deny")
                              .Set("updatedAt",
                                   static_cast<double>(row.updated_at_ms)));
            }
            return ValueResult(base::Value(std::move(rows)));
          },
          profile_id, origin),
      std::move(callback));
}

void CluPermissionHandler::Set(mojom::PermissionSetInputPtr input,
                               SetCallback callback) {
  mojom::CallResultPtr error;
  const std::optional<GURL> origin = WebOrigin(input->origin, &error);
  if (!origin) {
    std::move(callback).Run(std::move(error));
    return;
  }
  const SitePermissionKind* kind = FindSitePermission(input->permission);
  if (!kind) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kBadPayload, "unknown permission"));
    return;
  }
  SitePermissionDecision decision = SitePermissionDecision::kAsk;
  switch (input->decision) {
    case mojom::PermissionSetInputDecision::kAsk:
      decision = SitePermissionDecision::kAsk;
      break;
    case mojom::PermissionSetInputDecision::kAllow:
      decision = SitePermissionDecision::kAllow;
      break;
    case mojom::PermissionSetInputDecision::kBlock:
      decision = SitePermissionDecision::kBlock;
      break;
  }
  WithProfile(input->profile_id,
              base::BindOnce(
                  [](GURL origin, const SitePermissionKind* kind,
                     SitePermissionDecision decision, Profile* profile) {
                    HostContentSettingsMap* map = MapOf(profile);
                    if (!map) {
                      return NoSettings();
                    }
                    // Refused when Chromium does not take the value for this kind, or a policy or an extension
                    // decides it.
                    return SetSitePermission(*map, origin, *kind, decision)
                               ? NullResult()
                               : RejectedResult("managed");
                  },
                  *origin, kind, decision),
              std::move(callback));
}

void CluPermissionHandler::Clear(mojom::PermissionClearInputPtr input,
                                 ClearCallback callback) {
  std::optional<GURL> origin;
  if (input->origin) {
    mojom::CallResultPtr error;
    origin = WebOrigin(*input->origin, &error);
    if (!origin) {
      std::move(callback).Run(std::move(error));
      return;
    }
  }
  const SitePermissionKind* kind = nullptr;
  if (input->permission) {
    kind = FindSitePermission(*input->permission);
    if (!kind) {
      std::move(callback).Run(
          ErrorResult(mojom::ErrorCode::kBadPayload, "unknown permission"));
      return;
    }
  }
  WithProfile(input->profile_id,
              base::BindOnce(
                  [](const std::optional<GURL>& origin,
                     const SitePermissionKind* kind, Profile* profile) {
                    HostContentSettingsMap* map = MapOf(profile);
                    if (!map) {
                      return NoSettings();
                    }
                    ClearSitePermissions(*map, origin, kind);
                    return NullResult();
                  },
                  origin, kind),
              std::move(callback));
}

}  // namespace clu
