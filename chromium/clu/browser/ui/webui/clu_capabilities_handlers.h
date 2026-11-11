// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_CAPABILITIES_HANDLERS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_CAPABILITIES_HANDLERS_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "ui/gfx/geometry/rect.h"
#include "clu/browser/ui/webui/bridge/bridge_handlers.h"

class Profile;

// The download and site permission commands of src/shared/ipc/capabilities.ts on Chromium (docs/chromium/m9.md,
// docs/chromium/fixes.md). The guard has checked the surface and that the ids are UUID v4s. Downloads: these find the
// row among the window profile's downloads and act through Chromium's download item. Site permissions: list, set and
// clear go to the profile's HostContentSettingsMap, where Chromium's prompts and chrome://settings/content keep them.
// Permission prompts stay Chromium's own bubbles, so permission.respond and HibernationHandler keep answering
// kNotImplemented.
namespace clu {

class CluWindowState;

class CluPermissionHandler : public DefaultPermissionHandler {
 public:
  explicit CluPermissionHandler(base::WeakPtr<CluWindowState> window);
  ~CluPermissionHandler() override;

  // mojom::PermissionHandler:
  void List(mojom::PermissionListInputPtr input, ListCallback callback) override;
  void Set(mojom::PermissionSetInputPtr input, SetCallback callback) override;
  void Clear(mojom::PermissionClearInputPtr input,
             ClearCallback callback) override;

 private:
  // Runs `run` with the profile `profile_id` names: the window's own (its private profile included), else another
  // Rio profile, loaded first. Answers kNotFound for an unknown id.
  void WithProfile(const std::string& profile_id,
                   base::OnceCallback<mojom::CallResultPtr(Profile*)> run,
                   base::OnceCallback<void(mojom::CallResultPtr)> callback);

  base::WeakPtr<CluWindowState> window_;
  base::WeakPtrFactory<CluPermissionHandler> weak_ptr_factory_{this};
};

class CluDownloadHandler : public DefaultDownloadHandler {
 public:
  explicit CluDownloadHandler(base::WeakPtr<CluWindowState> window);
  ~CluDownloadHandler() override;

  // mojom::DownloadHandler:
  void Act(mojom::DownloadActInputPtr input, ActCallback callback) override;
  void Clear(ClearCallback callback) override;
  void OpenPopover(OpenPopoverCallback callback) override;
  void OpenFolder(OpenFolderCallback callback) override;
  // The Library's folders: Files lists one on a blocking thread; FileAct opens, reveals, copies, shares,
  // renames or trashes a file of it, named by its bare name and resolved inside the folder.
  void Files(mojom::DownloadFilesInputPtr input, FilesCallback callback) override;
  void FileAct(mojom::DownloadFileActInputPtr input,
               FileActCallback callback) override;
  // A file's QuickLook thumbnail as a PNG data URL, "" when there is none.
  void Thumb(mojom::DownloadThumbInputPtr input,
             ThumbCallback callback) override;
  void SetButtonRect(mojom::DownloadSetButtonRectInputPtr input,
                     SetButtonRectCallback callback) override;

 private:
  // The drag of a Library file or finished download, with its QuickLook thumbnail as the drag image.
  void DragFile(const base::FilePath& path,
                base::OnceCallback<void(mojom::CallResultPtr)> callback);
  void OnDragThumbnail(const base::FilePath& path,
                       base::OnceCallback<void(mojom::CallResultPtr)> callback,
                       std::string png_data_url);
  void OnFileResolved(mojom::DownloadFileActInputAction action,
                      std::optional<gfx::Rect> anchor,
                      FileActCallback callback,
                      std::optional<base::FilePath> path);

  base::WeakPtr<CluWindowState> window_;
  base::WeakPtrFactory<CluDownloadHandler> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_CAPABILITIES_HANDLERS_H_
