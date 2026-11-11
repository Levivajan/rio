// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_site_hooks.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/task/sequenced_task_runner.h"
#include "clu/browser/site/clu_blocker.h"
#include "clu/browser/site/clu_instant_link.h"
#include "clu/browser/site/clu_site_page.h"
#include "clu/browser/site/clu_site_profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"

namespace clu {

namespace {

// A broken list must never stall browsing: past this the page loads unblocked (R6).
constexpr base::TimeDelta kMaxBlockerWait = base::Seconds(5);

class CluBlockerReadyThrottle : public content::NavigationThrottle {
 public:
  CluBlockerReadyThrottle(content::NavigationThrottleRegistry& registry,
                          base::WeakPtr<CluBlocker> blocker)
      : content::NavigationThrottle(registry), blocker_(std::move(blocker)) {}
  CluBlockerReadyThrottle(const CluBlockerReadyThrottle&) = delete;
  CluBlockerReadyThrottle& operator=(const CluBlockerReadyThrottle&) = delete;
  ~CluBlockerReadyThrottle() override = default;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override {
    // The profile may be going away (its blocker with it): nothing to wait for.
    if (!blocker_ || blocker_->ready()) {
      return PROCEED;
    }
    blocker_->WhenReady(base::BindOnce(&CluBlockerReadyThrottle::Release,
                                       weak_ptr_factory_.GetWeakPtr()));
    timeout_.Start(FROM_HERE, kMaxBlockerWait,
                   base::BindOnce(&CluBlockerReadyThrottle::Release,
                                  weak_ptr_factory_.GetWeakPtr()));
    deferred_ = true;
    return DEFER;
  }

  const char* GetNameForLogging() override {
    return "CluBlockerReadyThrottle";
  }

 private:
  void Release() {
    if (!deferred_) {
      return;
    }
    deferred_ = false;
    timeout_.Stop();
    Resume();
  }

  base::WeakPtr<CluBlocker> blocker_;
  bool deferred_ = false;
  base::OneShotTimer timeout_;
  base::WeakPtrFactory<CluBlockerReadyThrottle> weak_ptr_factory_{this};
};

// An Instant Links search: Google redirects it to its "Redirect Notice" page; the page goes to the top result
// that page names instead, as one navigation of its own.
class CluInstantLinkThrottle : public content::NavigationThrottle {
 public:
  explicit CluInstantLinkThrottle(content::NavigationThrottleRegistry& registry)
      : content::NavigationThrottle(registry) {}
  CluInstantLinkThrottle(const CluInstantLinkThrottle&) = delete;
  CluInstantLinkThrottle& operator=(const CluInstantLinkThrottle&) = delete;
  ~CluInstantLinkThrottle() override = default;

  // content::NavigationThrottle:
  ThrottleCheckResult WillRedirectRequest() override {
    content::NavigationHandle* handle = navigation_handle();
    const std::vector<GURL>& chain = handle->GetRedirectChain();
    if (chain.empty()) {
      return PROCEED;
    }
    std::optional<GURL> target = InstantLinkTarget(chain.front(), handle->GetURL());
    if (!target) {
      return PROCEED;
    }
    content::NavigationController::LoadURLParams params(*target);
    params.transition_type = handle->GetPageTransition();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<content::WebContents> contents,
               content::NavigationController::LoadURLParams params) {
              if (contents) {
                contents->GetController().LoadURLWithParams(params);
              }
            },
            handle->GetWebContents()->GetWeakPtr(), std::move(params)));
    return CANCEL_AND_IGNORE;
  }

  const char* GetNameForLogging() override { return "CluInstantLinkThrottle"; }
};

}  // namespace

void AddCluNavigationThrottles(content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& navigation = registry.GetNavigationHandle();
  content::WebContents* contents = navigation.GetWebContents();
  // Only web pages: Clu's own WebUIs, extension pages and DevTools get no helper.
  if (!contents || !navigation.IsInPrimaryMainFrame() ||
      !navigation.GetURL().SchemeIsHTTPOrHTTPS() ||
      !CluSitePage::GetOrCreate(contents)) {
    return;
  }
  if (IsInstantLinkSearch(navigation.GetURL())) {
    registry.AddThrottle(std::make_unique<CluInstantLinkThrottle>(registry));
  }
  CluBlocker* blocker = GetCluBlocker(contents->GetBrowserContext());
  if (blocker && !blocker->ready()) {
    registry.AddThrottle(
        std::make_unique<CluBlockerReadyThrottle>(registry,
                                                  blocker->GetWeakPtr()));
  }
}

}  // namespace clu
