// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_CAPABILITIES_CLU_KEEP_LIVE_H_
#define CLU_BROWSER_CAPABILITIES_CLU_KEEP_LIVE_H_

#include <cstdint>
#include <set>

#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"

class BrowserWindowInterface;
class TabStripModel;

namespace content {
class WebContents;
}  // namespace content

// Applies KeepLiveReason() to every tab of every window (docs/chromium/m9.md): a tab Clu keeps live is not
// auto-discardable, the flag chrome.tabs.update({autoDiscardable}) sets, so Memory Saver's proactive discards pass
// it by. Clu only clears the flag on tabs it set it on, so an extension's choice stays. It also turns Memory Saver
// on by default, as hibernation always was (docs/PLAN.md 3.6); the user can turn it off in chrome://settings.
namespace clu {

class CluKeepLive : public BrowserCollectionObserver,
                    public TabStripModelObserver {
 public:
  // Starts once per process (the first Clu window does it); later calls do nothing.
  static void Start();
  // The running instance, or null before Start() (tests).
  static CluKeepLive* Get();

  CluKeepLive(const CluKeepLive&) = delete;
  CluKeepLive& operator=(const CluKeepLive&) = delete;

  // True when Clu holds `contents` live (tests).
  bool IsHeld(content::WebContents* contents) const;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnSplitTabChanged(const SplitTabChange& change) override;

 private:
  friend class base::NoDestructor<CluKeepLive>;

  CluKeepLive();
  ~CluKeepLive() override;

  void Watch(BrowserWindowInterface* browser);
  void ScheduleApply();
  void ApplyAll();
  // Sets each tab of `browser` to what the rules say.
  void Apply(BrowserWindowInterface* browser);
  // Tabs whose flag Clu set carry a mark (user data), so Clu clears only its own.
  void Hold(content::WebContents* contents, bool hold);

  bool apply_pending_ = false;
  // Session ids of the windows whose tab strips this observes (a window can be both listed and announced).
  std::set<int32_t> watched_;
  base::ScopedObservation<BrowserCollection, BrowserCollectionObserver>
      browsers_observation_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_CAPABILITIES_CLU_KEEP_LIVE_H_
