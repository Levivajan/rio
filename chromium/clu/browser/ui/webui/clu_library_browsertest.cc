// Copyright 2026 The Rio Authors. All rights reserved.

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/capabilities/clu_library_files.h"
#include "clu/browser/capabilities/clu_screenshot_watcher.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_capabilities_handlers.h"
#include "clu/browser/ui/webui/clu_library_drag.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/test/views_test_utils.h"

// The Library, checked against the real frame WebUI and handlers: the 290 ms
// exponential widen and narrow, the rail at the top with five sections and no Esc, the
// Filter button and relative day labels, and QuickLook thumbnails.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

class CluLibraryBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    browser()->GetWindow()->SetBounds(gfx::Rect(40, 40, 1100, 760));
    ASSERT_TRUE(base::test::RunUntil([&] {
      views::test::RunScheduledLayout(
          BrowserView::GetBrowserViewForBrowser(browser()));
      return !shell().layout().content_size.IsEmpty();
    }));
  }

  void TearDownOnMainThread() override {
    gfx::Animation::SetPrefersReducedMotionForTesting(false);
    SetLibraryFoldersForTesting(base::FilePath(), base::FilePath());
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  content::WebContents* frame() { return shell().frame_contents(); }

  void OpenLibrary() {
    window().SetArchivePanel(true);
    ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                                 kWaitForSelector,
                                                 ".clu-library-rail-item")));
  }
};

// Opening and closing the Library tweens the card over kLibraryMs on the exponential curve, published as
// "exp" for the frame WebUI; reduced motion snaps it.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest,
                       CluLibraryWidensAndNarrowsOnTheExponentialTween) {
  window().SetArchivePanel(true);
  ASSERT_TRUE(shell().sidebar_anim());
  EXPECT_EQ(shell().sidebar_anim()->ms, kLibraryMs);
  EXPECT_TRUE(shell().sidebar_anim()->exponential);
  base::DictValue state = window().CurrentState();
  const std::string* curve = state.FindDict("windows")
                                 ->FindDict(window().window_id())
                                 ->FindStringByDottedPath("shell.sidebar.anim.curve");
  ASSERT_TRUE(curve);
  EXPECT_EQ(*curve, "exp");
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  EXPECT_EQ(shell().layout().sidebar_rect.width(), kLibraryWidth);

  window().SetArchivePanel(false);
  ASSERT_TRUE(shell().sidebar_anim());
  EXPECT_EQ(shell().sidebar_anim()->ms, kLibraryMs);
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));

  // The dock keeps its own cubic timing.
  shell().ToggleSidebar();
  ASSERT_TRUE(shell().sidebar_anim());
  EXPECT_FALSE(shell().sidebar_anim()->exponential);
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  shell().ToggleSidebar();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));

  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  window().SetArchivePanel(true);
  EXPECT_FALSE(shell().sidebar_anim());
  window().SetArchivePanel(false);
  EXPECT_FALSE(shell().sidebar_anim());
}

// The Spaces view widens the Library until every space card and the round "+" show without sideways scrolling,
// and another section narrows it back to 417.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest, CluLibrarySpacesFitEveryCard) {
  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  for (const char* name : {"Two", "Three"}) {
    ASSERT_TRUE(space_actions::CreateSpace(&window(), std::string(name),
                                           std::nullopt, std::nullopt,
                                           std::nullopt)
                    .has_value());
  }
  OpenLibrary();
  ASSERT_TRUE(content::ExecJs(frame(), R"(
    [...document.querySelectorAll('[role=tab]')]
        .find(t => t.textContent.trim() === 'Spaces').click();
  )"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell().sidebar_anim() &&
           shell().layout().sidebar_rect.width() > kLibraryWidth;
  }));
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector,
                                               ".clu-space-cards-add button")));
  EXPECT_EQ(3, content::EvalJs(
                   frame(), "document.querySelectorAll('.clu-space-card').length"));
  // Nothing to scroll sideways, and the "+" sits inside the Library.
  EXPECT_EQ(true, content::EvalJs(frame(), R"(
    new Promise(resolve => requestAnimationFrame(() => {
      const row = document.querySelector('.clu-space-cards');
      const add = document.querySelector('.clu-space-cards-add button')
                      .getBoundingClientRect();
      resolve(row.scrollWidth <= row.clientWidth + 1 &&
              add.right <= row.getBoundingClientRect().right);
    })))"));
  // The page keeps its room.
  EXPECT_GE(shell().layout().page_rect.width(), 340);

  ASSERT_TRUE(content::ExecJs(frame(), R"(
    [...document.querySelectorAll('[role=tab]')]
        .find(t => t.textContent.trim() === 'Downloads').click();
  )"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell().sidebar_anim() &&
           shell().layout().sidebar_rect.width() == kLibraryWidth;
  }));
  // Closing forgets the width: the next open starts at 417.
  window().SetArchivePanel(false);
  window().SetArchivePanel(true);
  EXPECT_EQ(shell().layout().sidebar_rect.width(), kLibraryWidth);
}

// A close and reopen that land in one state patch still open the Spaces view wide. The browser forgets the
// width on close; the view asks again when the sidebar's width changes under it, so every card shows side by side.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest, CluLibrarySpacesWideAfterQuickReopen) {
  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  for (const char* name : {"Two", "Three"}) {
    ASSERT_TRUE(space_actions::CreateSpace(&window(), std::string(name),
                                           std::nullopt, std::nullopt,
                                           std::nullopt)
                    .has_value());
  }
  OpenLibrary();
  ASSERT_TRUE(content::ExecJs(frame(), R"(
    [...document.querySelectorAll('[role=tab]')]
        .find(t => t.textContent.trim() === 'Spaces').click();
  )"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell().sidebar_anim() &&
           shell().layout().sidebar_rect.width() > kLibraryWidth;
  }));
  const int wide = shell().layout().sidebar_rect.width();

  // Both in one task: the frame sees no closed state, only the width going back to 417.
  window().SetArchivePanel(false);
  window().SetArchivePanel(true);
  EXPECT_EQ(shell().layout().sidebar_rect.width(), kLibraryWidth);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell().sidebar_anim() &&
           shell().layout().sidebar_rect.width() == wide;
  }));
  EXPECT_EQ(3, content::EvalJs(
                   frame(), "document.querySelectorAll('.clu-space-card').length"));
  EXPECT_EQ(true, content::EvalJs(frame(), R"(
    new Promise(resolve => requestAnimationFrame(() => {
      const row = document.querySelector('.clu-space-cards');
      resolve(row.scrollWidth <= row.clientWidth + 1);
    })))"));
  // The page card starts after the Library: no card sits under it.
  EXPECT_GE(shell().layout().page_rect.x(), wide);
}

// Five sections in order, the first centered at y 144; the archive
// command opens Archived Tabs; a focused tile has no ring; Esc leaves it open and the back arrow closes it.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest,
                       CluLibraryRailStartsAtTheTopAndIgnoresEsc) {
  OpenLibrary();
  ASSERT_TRUE(base::test::RunUntil([&] { return !shell().sidebar_anim(); }));
  EXPECT_EQ(
      "Media|Downloads|Spaces|Site tweaks|Archived Tabs",
      content::EvalJs(frame(), "[...document.querySelectorAll('[role=tab]')]"
                               ".map(t => t.textContent).join('|')"));
  EXPECT_EQ(144, content::EvalJs(frame(), R"(
    (() => {
      const r = document.querySelector('[role=tab]').getBoundingClientRect();
      return Math.round(r.top + r.height / 2);
    })())"));
  EXPECT_EQ("Archived Tabs",
            content::EvalJs(frame(),
                            "document.querySelector('[role=tab][aria-selected="
                            "true]').textContent"));
  EXPECT_EQ("none", content::EvalJs(frame(), R"(
    (() => {
      const t = document.querySelector('[role=tab]');
      t.focus();
      return getComputedStyle(t).outlineStyle;
    })())"));
  // The archive's header: the search field and a Filter button 3.5 to its right; day labels are relative.
  EXPECT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector,
                                               ".clu-library-filter")));
  EXPECT_EQ("Filter", content::EvalJs(frame(),
                                      "document.querySelector('.clu-library-"
                                      "filter').textContent"));

  ASSERT_TRUE(content::ExecJs(
      frame(),
      "document.dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', "
      "bubbles: true}))"));
  ASSERT_TRUE(content::ExecJs(frame(), "new Promise(r => setTimeout(r, 100))"));
  EXPECT_TRUE(window().archive_panel());

  ASSERT_TRUE(content::ExecJs(
      frame(), "document.querySelector('[aria-label=\"Back to tabs\"]').click()"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().archive_panel(); }));
}

// Download.thumb renders a Library file through QuickLook as a PNG data URL; a name that leaves the
// folder or names nothing is not found.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest,
                       CluLibraryThumbnailsAreQuickLookPngDataUrls) {
  base::ScopedTempDir downloads;
  base::ScopedTempDir media;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
    ASSERT_TRUE(media.CreateUniqueTempDir());
    SkBitmap bitmap;
    bitmap.allocN32Pixels(64, 48);
    bitmap.eraseColor(SK_ColorRED);
    std::optional<std::vector<uint8_t>> png =
        gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                          /*discard_transparency=*/true);
    ASSERT_TRUE(png);
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII("shot.png"), *png));
  }
  SetLibraryFoldersForTesting(downloads.GetPath(), media.GetPath());

  CluDownloadHandler handler(window().GetWeakPtr());
  CallFuture thumb;
  handler.Thumb(mojom::DownloadThumbInput::New(
                    mojom::DownloadThumbInputSource::kMedia, "shot.png"),
                thumb.GetCallback());
  mojom::CallResultPtr result = thumb.Take();
  ASSERT_TRUE(result && result->is_value_json());
  EXPECT_TRUE(base::StartsWith(result->get_value_json(),
                               "\"data:image/png;base64,"));

  for (const char* junk : {"../shot.png", "missing.png", "", ".hidden"}) {
    CallFuture missing;
    handler.Thumb(mojom::DownloadThumbInput::New(
                      mojom::DownloadThumbInputSource::kMedia, junk),
                  missing.GetCallback());
    mojom::CallResultPtr r = missing.Take();
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->is_error()) << junk;
  }
}

// A Media tile is a square rounded tile tinted over the sidebar, the thumbnail centered on
// it at its own aspect inside a white frame, and no name under it (the tooltip carries the name).
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest,
                       CluLibraryMediaTileFramesTheThumbnailOnATintedTile) {
  base::ScopedTempDir downloads;
  base::ScopedTempDir media;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
    ASSERT_TRUE(media.CreateUniqueTempDir());
    SkBitmap bitmap;
    bitmap.allocN32Pixels(64, 48);
    bitmap.eraseColor(SK_ColorRED);
    std::optional<std::vector<uint8_t>> png =
        gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                          /*discard_transparency=*/true);
    ASSERT_TRUE(png);
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII("shot.png"), *png));
  }
  SetLibraryFoldersForTesting(downloads.GetPath(), media.GetPath());

  OpenLibrary();
  ASSERT_TRUE(content::ExecJs(
      frame(), "document.querySelector('[role=tab][data-section=media]')"
               ".click()"));
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector,
                                               ".clu-library-thumb img")));
  ASSERT_TRUE(content::ExecJs(
      frame(), "document.querySelector('.clu-library-thumb img').decode()"));
  EXPECT_EQ("square|12px|tinted|solid rgb(255, 255, 255)|inside|no name",
            content::EvalJs(frame(), R"(
    (() => {
      const tile = document.querySelector('.clu-library-thumb');
      const img = tile.querySelector('img');
      const t = tile.getBoundingClientRect();
      const i = img.getBoundingClientRect();
      const ts = getComputedStyle(tile);
      const is = getComputedStyle(img);
      return [
        Math.abs(t.width - t.height) < 1 ? 'square' : `${t.width}x${t.height}`,
        ts.borderTopLeftRadius,
        ts.backgroundColor !== 'rgba(0, 0, 0, 0)' ? 'tinted' : 'bare',
        `${is.borderTopStyle} ${is.borderTopColor}`,
        i.left > t.left && i.right < t.right && i.top > t.top &&
            i.bottom < t.bottom ? 'inside' : 'full-bleed',
        document.querySelector('.clu-library-tile-name') ? 'name' : 'no name',
      ].join('|');
    })())"));
}

// A Media tile carries its full name, right-click opens Rio's menu for it, and download.fileAct renames
// inside the folder, refusing a taken name, a path and a rename without a name.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest,
                       CluLibraryItemMenuRenamesInsideTheFolder) {
  base::ScopedTempDir downloads;
  base::ScopedTempDir media;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
    ASSERT_TRUE(media.CreateUniqueTempDir());
    ASSERT_TRUE(base::WriteFile(
        media.GetPath().AppendASCII("Screenshot 2026-11-09 at 20.35.00.png"),
        "x"));
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII("taken.png"), "y"));
  }
  SetLibraryFoldersForTesting(downloads.GetPath(), media.GetPath());

  OpenLibrary();
  ASSERT_TRUE(content::ExecJs(
      frame(), "document.querySelector('[role=tab][data-section=media]')"
               ".click()"));
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector,
                                               ".clu-library-tile")));
  EXPECT_EQ("Screenshot 2026-11-09 at 20.35.00.png",
            content::EvalJs(frame(), R"(
    [...document.querySelectorAll('.clu-library-tile-open')]
        .map(b => b.getAttribute('aria-label'))
        .find(n => n.startsWith('Screenshot')))"));
  EXPECT_EQ(
      "Open|Show in Finder|Copy|Copy Path|Share…|Rename…|Move to Trash",
      content::EvalJs(frame(), R"(
    (() => {
      const tile = document.querySelector('.clu-library-tile');
      const r = tile.getBoundingClientRect();
      tile.dispatchEvent(new MouseEvent('contextmenu', {bubbles: true,
          cancelable: true, clientX: r.left + 8, clientY: r.top + 8}));
      return new Promise(resolve => requestAnimationFrame(() => resolve(
          [...document.querySelectorAll('[role=menu] [role=menuitem]')]
              .map(m => m.textContent).join('|'))));
    })())"));

  CluDownloadHandler handler(window().GetWeakPtr());
  const auto file_act = [&](const std::string& name,
                            mojom::DownloadFileActInputAction action,
                            std::optional<std::string> new_name) {
    CallFuture done;
    handler.FileAct(mojom::DownloadFileActInput::New(
                        mojom::DownloadFileActInputSource::kMedia, name,
                        action, std::move(new_name), nullptr),
                    done.GetCallback());
    return done.Take();
  };
  using Action = mojom::DownloadFileActInputAction;
  const std::string shot = "Screenshot 2026-11-09 at 20.35.00.png";
  mojom::CallResultPtr r = file_act(shot, Action::kRename, std::nullopt);
  ASSERT_TRUE(r && r->is_error());
  for (const char* junk : {"taken.png", "../out.png", "sub/out.png", ".hidden"}) {
    r = file_act(shot, Action::kRename, std::string(junk));
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->is_error()) << junk;
  }
  r = file_act(shot, Action::kRename, std::string("Trip.png"));
  ASSERT_TRUE(r);
  EXPECT_FALSE(r->is_error());
  {
    base::ScopedAllowBlockingForTesting allow;
    EXPECT_TRUE(base::PathExists(media.GetPath().AppendASCII("Trip.png")));
    EXPECT_FALSE(base::PathExists(media.GetPath().AppendASCII(shot)));
    EXPECT_TRUE(base::PathExists(media.GetPath().AppendASCII("taken.png")));
  }
  r = file_act("missing.png", Action::kCopyPath, std::nullopt);
  ASSERT_TRUE(r);
  EXPECT_TRUE(r->is_error());
}

// A Media tile drags out as the file itself: its dragstart is cancelled so no HTML drag of the button
// starts, and download.fileAct's drag resolves the name inside the Library's folder only, refusing junk and missing
// names. With no press held (the drag ended before the file resolved) nothing drags and the file stays put; the drag
// image is the QuickLook thumbnail fit in kLibraryDragPoints.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest, CluLibraryFileDragIsTheFile) {
  base::ScopedTempDir downloads;
  base::ScopedTempDir media;
  const std::string shot = "Screenshot 2026-11-10 at 18.00.00.png";
  std::string png_url;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
    ASSERT_TRUE(media.CreateUniqueTempDir());
    SkBitmap bitmap;
    bitmap.allocN32Pixels(400, 300);
    bitmap.eraseColor(SK_ColorBLUE);
    std::optional<std::vector<uint8_t>> png =
        gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                          /*discard_transparency=*/true);
    ASSERT_TRUE(png);
    ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII(shot), *png));
    png_url = "data:image/png;base64," + base::Base64Encode(*png);
  }
  SetLibraryFoldersForTesting(downloads.GetPath(), media.GetPath());

  OpenLibrary();
  ASSERT_TRUE(content::ExecJs(
      frame(), "document.querySelector('[role=tab][data-section=media]')"
               ".click()"));
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector,
                                               ".clu-library-tile")));
  EXPECT_EQ(true, content::EvalJs(frame(), R"(
    (() => {
      const tile = document.querySelector('.clu-library-tile-open');
      const e = new DragEvent('dragstart', {bubbles: true, cancelable: true});
      return tile.draggable && !tile.dispatchEvent(e);
    })())"));

  CluDownloadHandler handler(window().GetWeakPtr());
  const auto drag = [&](const std::string& name) {
    CallFuture done;
    handler.FileAct(mojom::DownloadFileActInput::New(
                        mojom::DownloadFileActInputSource::kMedia, name,
                        mojom::DownloadFileActInputAction::kDrag,
                        std::nullopt, nullptr),
                    done.GetCallback());
    return done.Take();
  };
  for (const char* junk : {"missing.png", "../out.png", "sub/out.png", ""}) {
    mojom::CallResultPtr r = drag(junk);
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->is_error()) << junk;
  }
  mojom::CallResultPtr r = drag(shot);
  ASSERT_TRUE(r);
  EXPECT_TRUE(r->is_error());
  {
    base::ScopedAllowBlockingForTesting allow;
    EXPECT_TRUE(base::PathExists(media.GetPath().AppendASCII(shot)));
  }

  const gfx::Size image = LibraryDragImageSize(png_url);
  EXPECT_EQ(kLibraryDragPoints, image.width());
  EXPECT_EQ(kLibraryDragPoints * 3 / 4, image.height());
  EXPECT_TRUE(LibraryDragImageSize("").IsEmpty());
  EXPECT_TRUE(LibraryDragImageSize("data:image/png;base64,!!").IsEmpty());
}

// Dropped on Finder or the Desktop the drag copies the file. Offered copy and link, Finder made a 900-byte
// alias, so outside Rio the drag offers copy alone; inside Rio a page's drop zone still gets copy and link. No context
// offers move, so no drop moves the file.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest,
                       CluLibraryFileDragCopiesOutsideRio) {
  EXPECT_EQ(ui::DragDropTypes::DRAG_COPY,
            LibraryDragOperations(/*outside_application=*/true));
  const int inside = LibraryDragOperations(/*outside_application=*/false);
  EXPECT_TRUE(inside & ui::DragDropTypes::DRAG_COPY);
  EXPECT_TRUE(inside & ui::DragDropTypes::DRAG_LINK);
  EXPECT_FALSE(inside & ui::DragDropTypes::DRAG_MOVE);
}

// Copy and Copy Path on a Library file (the Library's menu and the bottom-left pop-up's, one handler) show
// the toast the URL copy shows, "Copied File" or "Copied Path", in the same toast stack. A missing file copies
// nothing and shows no toast.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest, CluLibraryCopyShowsACopiedToast) {
  base::ScopedTempDir downloads;
  base::ScopedTempDir media;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
    ASSERT_TRUE(media.CreateUniqueTempDir());
    ASSERT_TRUE(
        base::WriteFile(downloads.GetPath().AppendASCII("Dashboard.zip"), "z"));
  }
  SetLibraryFoldersForTesting(downloads.GetPath(), media.GetPath());

  const auto toasts = [&] {
    const base::DictValue state = window().CurrentState();
    const base::ListValue* list = state.FindDict("windows")
                                      ->FindDict(window().window_id())
                                      ->FindList("toasts");
    std::vector<std::string> texts;
    if (list) {
      for (const base::Value& toast : *list) {
        texts.push_back(*toast.GetDict().FindString("text"));
      }
    }
    return texts;
  };
  CluDownloadHandler handler(window().GetWeakPtr());
  using Action = mojom::DownloadFileActInputAction;
  const auto file_act = [&](const std::string& name, Action action) {
    CallFuture done;
    handler.FileAct(mojom::DownloadFileActInput::New(
                        mojom::DownloadFileActInputSource::kDownloads, name,
                        action, std::nullopt, nullptr),
                    done.GetCallback());
    return done.Take();
  };

  const std::vector<std::string> before = toasts();
  mojom::CallResultPtr r = file_act("missing.zip", Action::kCopy);
  ASSERT_TRUE(r && r->is_error());
  EXPECT_EQ(before, toasts());

  r = file_act("Dashboard.zip", Action::kCopy);
  ASSERT_TRUE(r);
  EXPECT_FALSE(r->is_error());
  EXPECT_EQ(std::vector<std::string>{"Copied File"}, toasts());

  // One toast at a time: Copy Path replaces it.
  r = file_act("Dashboard.zip", Action::kCopyPath);
  ASSERT_TRUE(r);
  EXPECT_FALSE(r->is_error());
  EXPECT_EQ(std::vector<std::string>{"Copied Path"}, toasts());
}

// A screenshot that lands while Rio runs lists right above the bottom bar with its own thumbnail: the
// arrival the watcher reports, asked for by its name in the media folder, renders through QuickLook as a PNG
// data URL.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest,
                       CluScreenshotArrivalHasAQuickLookThumbnail) {
  base::ScopedTempDir downloads;
  base::ScopedTempDir media;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
    ASSERT_TRUE(media.CreateUniqueTempDir());
  }
  SetLibraryFoldersForTesting(downloads.GetPath(), media.GetPath());
  CluScreenshotWatcher watcher(media.GetPath(), base::DoNothing());
  ASSERT_TRUE(base::test::RunUntil([&] { return watcher.primed(); }));
  {
    base::ScopedAllowBlockingForTesting allow;
    SkBitmap bitmap;
    bitmap.allocN32Pixels(64, 48);
    bitmap.eraseColor(SK_ColorBLUE);
    std::optional<std::vector<uint8_t>> png =
        gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                          /*discard_transparency=*/true);
    ASSERT_TRUE(png);
    ASSERT_TRUE(base::WriteFile(
        media.GetPath().AppendASCII("Screenshot 2026-11-09 at 22.00.05.png"),
        *png));
  }
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !watcher.arrivals().empty(); }));
  const std::string name = watcher.arrivals()[0].name;
  EXPECT_EQ(name, "Screenshot 2026-11-09 at 22.00.05.png");

  CluDownloadHandler handler(window().GetWeakPtr());
  CallFuture thumb;
  handler.Thumb(mojom::DownloadThumbInput::New(
                    mojom::DownloadThumbInputSource::kMedia, name),
                thumb.GetCallback());
  mojom::CallResultPtr result = thumb.Take();
  ASSERT_TRUE(result && result->is_value_json());
  EXPECT_TRUE(base::StartsWith(result->get_value_json(),
                               "\"data:image/png;base64,"));
}

// Right-click on a row of the bottom-left pop-up opens the Library's menu in a fixed layer that covers the
// window. Once the row's rise animation filled forwards, and a filled transform animation keeps an element a
// containing block in Blink, so the layer took the row's box and the menu opened off the window. With the shipped
// frame CSS, once the rise ends a fixed layer inside a row covers the whole frame again.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest, CluArrivalMenuLayerCoversTheWindow) {
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector,
                                               "nav.clu-sidebar")));
  EXPECT_EQ("clu-arrival-rise|1|0|0|true|true", content::EvalJs(frame(), R"(
    (async () => {
      document.documentElement.dataset.reducedMotion = 'false';
      const anchor = document.createElement('div');
      anchor.className = 'clu-arrivals-anchor';
      const list = document.createElement('ul');
      list.className = 'clu-arrivals';
      const row = document.createElement('li');
      row.className = 'clu-arrival';
      const layer = document.createElement('div');
      layer.className = 'clu-library-filter-layer';
      row.append(layer);
      list.append(row);
      anchor.append(list);
      document.querySelector('nav.clu-sidebar').append(anchor);
      const name = getComputedStyle(row).animationName;
      const animations = row.getAnimations();
      await Promise.all(animations.map((a) => a.finished));
      await new Promise((resolve) => requestAnimationFrame(() => resolve()));
      const r = layer.getBoundingClientRect();
      anchor.remove();
      return [name, animations.length, Math.round(r.left), Math.round(r.top),
              Math.round(r.width) === innerWidth,
              Math.round(r.height) === innerHeight].join('|');
    })())"));
}

// While a pop-up row's Rename is open, one more screenshot landing in a full pop-up drops the oldest row
// that is not being renamed; the renamed row keeps its field and the typed name, and Enter renames the real file.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest, CluArrivalRenameSurvivesANewArrival) {
  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  base::ScopedTempDir downloads;
  base::ScopedTempDir media;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
    ASSERT_TRUE(media.CreateUniqueTempDir());
  }
  SetLibraryFoldersForTesting(downloads.GetPath(), media.GetPath());
  window().WatchScreenshotsForTesting(media.GetPath());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window().screenshots_for_testing().primed(); }));
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector,
                                               "nav.clu-sidebar")));
  auto land = [&](int n) {
    {
      base::ScopedAllowBlockingForTesting allow;
      SkBitmap bitmap;
      bitmap.allocN32Pixels(8, 8);
      bitmap.eraseColor(SK_ColorBLUE);
      std::optional<std::vector<uint8_t>> png =
          gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                            /*discard_transparency=*/true);
      ASSERT_TRUE(png);
      ASSERT_TRUE(base::WriteFile(
          media.GetPath().AppendASCII(
              "Screenshot " + base::NumberToString(n) + ".png"),
          *png));
    }
    ASSERT_TRUE(base::test::RunUntil([&] {
      return window().screenshots_for_testing().arrivals().size() ==
             static_cast<size_t>(n);
    }));
  };
  // One at a time, so the rows stack newest on top: 4, 3, 2, 1.
  for (int n = 1; n <= 4; ++n) {
    land(n);
  }
  EXPECT_EQ(true, content::EvalJs(frame(), R"(
    new Promise(resolve => {
      const check = () => document.querySelectorAll('.clu-arrival').length === 4
          ? resolve(true) : setTimeout(check, 20);
      check();
    }))"));
  // Rename the oldest row, at the bottom, and type a new name.
  EXPECT_EQ("Screenshot 1.png", content::EvalJs(frame(), R"(
    (async () => {
      const rows = [...document.querySelectorAll('.clu-arrival')];
      const row = rows[rows.length - 1];
      row.querySelector('.clu-arrival-open').dispatchEvent(new MouseEvent(
          'contextmenu', {bubbles: true, cancelable: true, clientX: 20, clientY: 500}));
      const until = (f) => new Promise(resolve => {
        const check = () => f() ? resolve(f()) : setTimeout(check, 20);
        check();
      });
      const item = await until(() => [...document.querySelectorAll('[role=menuitem]')]
          .find((m) => m.textContent === 'Rename\u2026'));
      item.click();
      const field = await until(() => row.querySelector('input.clu-library-rename'));
      field.value = 'Receipt.png';
      window.cluRenameField = field;
      return row.querySelector('.clu-library-peek-title').textContent;
    })())"));
  land(5);
  EXPECT_EQ("Screenshot 5.png|Screenshot 4.png|Screenshot 3.png|Screenshot 1.png|true|Receipt.png",
            content::EvalJs(frame(), R"(
    new Promise(resolve => {
      const check = () => {
        const titles = [...document.querySelectorAll('.clu-arrival .clu-library-peek-title')]
            .map((t) => t.textContent);
        if (titles[0] !== 'Screenshot 5.png') return setTimeout(check, 20);
        const field = document.querySelector('input.clu-library-rename');
        resolve([...titles, field === window.cluRenameField, field?.value].join('|'));
      };
      check();
    }))"));
  EXPECT_TRUE(content::ExecJs(frame(), R"(
    window.cluRenameField.dispatchEvent(new KeyboardEvent(
        'keydown', {key: 'Enter', bubbles: true, cancelable: true}));)"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    base::ScopedAllowBlockingForTesting allow;
    return base::PathExists(media.GetPath().AppendASCII("Receipt.png")) &&
           !base::PathExists(media.GetPath().AppendASCII("Screenshot 1.png"));
  }));
}

// Rename on a pop-up row always names and renames that row's file, while more
// screenshots land and the rows shift, and the Library hover peek stays shut while the field is open.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest, CluArrivalRenameActsOnTheRowClicked) {
  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  base::ScopedTempDir downloads;
  base::ScopedTempDir media;
  {
    base::ScopedAllowBlockingForTesting allow;
    ASSERT_TRUE(downloads.CreateUniqueTempDir());
    ASSERT_TRUE(media.CreateUniqueTempDir());
  }
  SetLibraryFoldersForTesting(downloads.GetPath(), media.GetPath());
  window().WatchScreenshotsForTesting(media.GetPath());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window().screenshots_for_testing().primed(); }));
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector,
                                               "nav.clu-sidebar")));
  auto exists = [&](const std::string& name) {
    base::ScopedAllowBlockingForTesting allow;
    return base::PathExists(media.GetPath().AppendASCII(name));
  };
  auto land = [&](int n) {
    const std::string name = "Screenshot " + base::NumberToString(n) + ".png";
    {
      base::ScopedAllowBlockingForTesting allow;
      SkBitmap bitmap;
      bitmap.allocN32Pixels(8, 8);
      bitmap.eraseColor(SK_ColorBLUE);
      std::optional<std::vector<uint8_t>> png =
          gfx::PNGCodec::EncodeBGRASkBitmap(bitmap,
                                            /*discard_transparency=*/true);
      ASSERT_TRUE(png);
      ASSERT_TRUE(base::WriteFile(media.GetPath().AppendASCII(name), *png));
    }
    ASSERT_TRUE(base::test::RunUntil([&] {
      const auto& arrivals = window().screenshots_for_testing().arrivals();
      return !arrivals.empty() && arrivals.front().name == name;
    }));
  };
  for (int n = 1; n <= 4; ++n) {
    land(n);
  }
  int next = 5;
  std::set<std::string> done;
  for (int i = 0; i < 4; ++i) {
    // Right-click the oldest screenshot row still showing.
    const std::string name =
        content::EvalJs(frame(), R"(
      (async () => {
        const until = (f) => new Promise(resolve => {
          const check = () => f() ? resolve(f()) : setTimeout(check, 20);
          check();
        });
        const row = await until(() => [...document.querySelectorAll(
            '.clu-arrival:not([data-gap]):not([data-leaving])')]
            .filter((r) => r.querySelector('.clu-arrival-open')?.title.startsWith('Screenshot'))
            .pop());
        window.cluRow = row;
        row.querySelector('.clu-arrival-open').dispatchEvent(new MouseEvent(
            'contextmenu', {bubbles: true, cancelable: true, clientX: 20, clientY: 500}));
        await until(() => document.querySelector('[role=menu]'));
        return row.querySelector('.clu-arrival-open').title;
      })())")
            .ExtractString();
    ASSERT_TRUE(base::StartsWith(name, "Screenshot "));
    ASSERT_FALSE(done.contains(name));
    // A file lands while the menu is open, then Rename opens the field.
    land(next++);
    EXPECT_EQ(name, content::EvalJs(frame(), R"(
      (async () => {
        const until = (f) => new Promise(resolve => {
          const check = () => f() ? resolve(f()) : setTimeout(check, 20);
          check();
        });
        [...document.querySelectorAll('[role=menuitem]')]
            .find((m) => m.textContent === 'Rename…').click();
        const field = await until(() => window.cluRow.querySelector('input.clu-library-rename'));
        window.cluRenameField = field;
        return field.value;
      })())"));
    if (i == 0) {
      // The pointer moves onto the Library button below: the peek stays shut and the field stays.
      EXPECT_EQ(true, content::EvalJs(frame(), R"(
        new Promise(resolve => {
          document.querySelector('.clu-archive-button').dispatchEvent(
              new PointerEvent('pointerover', {bubbles: true}));
          setTimeout(() => resolve(window.cluRenameField.isConnected &&
              !document.querySelector('.clu-library-peek[data-state=open]')), 400);
        }))"));
      EXPECT_TRUE(content::ExecJs(frame(), R"(
        document.querySelector('.clu-archive-button').dispatchEvent(
            new PointerEvent('pointerout', {bubbles: true}));)"));
    }
    // Another lands while the field is open; the field keeps its row.
    land(next++);
    const std::string renamed = "Renamed" + base::NumberToString(i) + ".png";
    EXPECT_TRUE(content::ExecJs(
        frame(), content::JsReplace(R"(
      window.cluRenameField.value = $1;
      window.cluRenameField.dispatchEvent(new KeyboardEvent(
          'keydown', {key: 'Enter', bubbles: true, cancelable: true}));)",
                                    renamed)));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return exists(renamed) && !exists(name); }));
    done.insert(name);
    // Only that file changed: every other screenshot keeps its name.
    for (int n = 1; n < next; ++n) {
      const std::string other = "Screenshot " + base::NumberToString(n) + ".png";
      EXPECT_EQ(!done.contains(other), exists(other)) << other;
    }
  }
}

// Library > Spaces shows every space as a 163 wide card painted with its own theme, its header carrying the
// icon, name, profile pill and brush, its foot the move handle and "..."; a round + follows the last card, and the
// brush opens the theme editor for that card's space in the overlay.
IN_PROC_BROWSER_TEST_F(CluLibraryBrowserTest, CluLibrarySpacesAreTintedCards) {
  OpenLibrary();
  ASSERT_EQ(true, content::EvalJs(frame(), R"(
    (() => { document.querySelector('[role=tab][data-section=spaces]').click(); return true; })())"));
  ASSERT_EQ(true, content::EvalJs(frame(), content::JsReplace(
                                               kWaitForSelector, ".clu-space-card")));
  EXPECT_EQ(true, content::EvalJs(
                      frame(), content::JsReplace(
                                   "!!document.querySelector(`.clu-space-card[data-space-id=\"${$1}\"]`)",
                                   window().space_id())));
  EXPECT_EQ("163px|true|true|true|true|true|true", content::EvalJs(frame(), R"(
    (() => {
      const card = document.querySelector('.clu-space-card');
      const has = (s) => !!card.querySelector(s);
      return [getComputedStyle(card).width,
              getComputedStyle(card).backgroundImage.includes('gradient'),
              has('.clu-space-card-icon'), has('.clu-space-card-name'),
              has('.clu-space-card-brush'), has('.clu-space-card-move') && has('.clu-space-card-more'),
              !!document.querySelector('.clu-space-cards-add button')].join('|');
    })())"));

  ASSERT_EQ(true, content::EvalJs(frame(), R"(
    (() => { document.querySelector('.clu-space-card-brush').click(); return true; })())"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::WebContents* overlay = shell().overlay_contents();
    return overlay && !overlay->IsLoading() &&
           content::EvalJs(overlay, "!!document.querySelector('.clu-theme-picker')")
                   .ExtractBool();
  }));
}

}  // namespace
}  // namespace clu
