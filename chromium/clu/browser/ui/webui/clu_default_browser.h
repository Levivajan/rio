// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_DEFAULT_BROWSER_H_
#define CLU_BROWSER_UI_WEBUI_CLU_DEFAULT_BROWSER_H_

#include <string_view>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/no_destructor.h"
#include "base/sequence_checker.h"
#include "chrome/browser/shell_integration.h"

namespace clu {

// app.defaultBrowser for a shell_integration answer: "default", "not-default", or "unavailable" when macOS cannot
// name this build (no bundle id, as outside an app bundle).
std::string_view DefaultBrowserStateName(
    shell_integration::DefaultWebClientState state);

// platform.requestDefaultBrowser's status from the state before the request: "already-default", "not-packaged" (macOS
// cannot name this build, or the platform does not allow the change), or empty when Rio asks macOS.
std::string_view DefaultBrowserRequestStatus(
    shell_integration::DefaultWebClientState before,
    bool can_set);

// Whether Rio is the default browser (app.defaultBrowser in every window), and the request from onboarding and
// Settings (docs/chromium/fixes.md). Both run Chromium's shell_integration workers, which do the LaunchServices work
// off the UI thread. macOS asks the user itself, so a request only starts the change; the answer shows at the next
// Refresh(). UI thread only.
class CluDefaultBrowser {
 public:
  using WorkerFactory = base::RepeatingCallback<
      scoped_refptr<shell_integration::DefaultWebClientWorker>()>;
  using RequestCallback = base::OnceCallback<void(std::string_view status)>;

  static CluDefaultBrowser& Get();

  CluDefaultBrowser(const CluDefaultBrowser&) = delete;
  CluDefaultBrowser& operator=(const CluDefaultBrowser&) = delete;

  std::string_view state_name() const {
    return DefaultBrowserStateName(state_);
  }

  // Checks again; observers run when the answer changed. Windows call it when they open and when they become active,
  // so the user's answer to macOS's prompt, or a change in System Settings, shows when they come back to Rio.
  void Refresh();
  // Asks macOS to make Rio the default browser, unless it already is or cannot be. `callback` gets the status.
  void Request(RequestCallback callback);

  // Runs `callback` after every change of state_name().
  base::CallbackListSubscription AddObserver(base::RepeatingClosure callback);

  // Test seam: the worker every check and request uses (a shell_integration::DefaultBrowserWorker otherwise). A null
  // factory restores it. ResetForTesting() also forgets the state and drops answers still in flight.
  void SetWorkerFactoryForTesting(WorkerFactory factory);
  void ResetForTesting();

 private:
  friend class base::NoDestructor<CluDefaultBrowser>;

  CluDefaultBrowser();
  ~CluDefaultBrowser();

  scoped_refptr<shell_integration::DefaultWebClientWorker> MakeWorker();
  void OnChecked(int generation,
                 shell_integration::DefaultWebClientState state);
  void OnCheckedForRequest(int generation,
                           RequestCallback callback,
                           shell_integration::DefaultWebClientState state);

  SEQUENCE_CHECKER(sequence_checker_);
  shell_integration::DefaultWebClientState state_ =
      shell_integration::UNKNOWN_DEFAULT;
  // Bumped by ResetForTesting(), so a test's answers never land in the next test.
  int generation_ = 0;
  WorkerFactory factory_;
  base::RepeatingClosureList observers_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_DEFAULT_BROWSER_H_
