// Copyright 2026 The Rio Authors. All rights reserved.

#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/page_transition_types.h"

// Motion (docs/chromium/motion.md): the page loading indicator above the card runs
// each phase's animation once, on transform and opacity only, and a finished load fades out before the grabber
// returns.
namespace clu {
namespace {

// Records every phase the indicator shows, in order, and the properties its running animations move.
constexpr char kRecordPhases[] = R"(
  window.__phases = [];
  window.__props = new Set();
  const record = () => {
    const el = document.querySelector('[data-shell="card-loading"]');
    const phase = el ? el.dataset.phase : 'grabber';
    if (window.__phases.at(-1) !== phase) window.__phases.push(phase);
    if (!el) return;
    for (const a of el.getAnimations({ subtree: true })) {
      for (const k of a.effect.getKeyframes()) {
        for (const p of Object.keys(k)) {
          if (!['offset', 'easing', 'composite', 'computedOffset'].includes(p)) window.__props.add(p);
        }
      }
    }
  };
  record();
  new MutationObserver(record).observe(document.body, {
    subtree: true, childList: true, attributes: true, attributeFilter: ['data-phase'],
  });
  true;
)";

class CluMotionBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(content::WaitForLoadStop(frame()));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/title1.html")));
  }

  content::WebContents* frame() {
    return CluShell::From(browser())->frame_contents();
  }
  content::WebContents* page() {
    return browser()->GetTabStripModel()->GetActiveWebContents();
  }
  std::string Phases() {
    return content::EvalJs(frame(), "window.__phases.join(',')")
        .ExtractString();
  }
};

IN_PROC_BROWSER_TEST_F(CluMotionBrowserTest,
                       CluLoadingIndicatorRunsEachPhaseOnceThenFades) {
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(frame(),
                           "!!document.querySelector('[data-shell=\"card-"
                           "grabber-zone\"]')")
        .ExtractBool();
  }));
  ASSERT_EQ(true, content::EvalJs(frame(), kRecordPhases));

  // The server holds the first bytes for a second, so the waiting pulse shows first.
  page()->GetController().LoadURL(embedded_test_server()->GetURL("/slow?1"),
                                  content::Referrer(),
                                  ui::PAGE_TRANSITION_TYPED, std::string());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Phases().find("waiting") != std::string::npos;
  }));
  ASSERT_TRUE(content::WaitForLoadStop(page()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const std::string phases = Phases();
    return phases.size() > 8 &&
           phases.compare(phases.size() - 8, 8, ",grabber") == 0;
  }));

  // grabber, waiting, (receiving,) done, grabber: each phase once, and the done fade before the grabber.
  const std::string phases = Phases();
  EXPECT_TRUE(phases == "grabber,waiting,done,grabber" ||
              phases == "grabber,waiting,receiving,done,grabber")
      << phases;
  // Only compositor properties move.
  EXPECT_EQ(true, content::EvalJs(frame(), R"(
    [...window.__props].every((p) => p === 'opacity' || p === 'transform')
  )"));
  EXPECT_EQ(true, content::EvalJs(frame(), "window.__props.has('opacity')"));
}

}  // namespace
}  // namespace clu
