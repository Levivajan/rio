// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/browser_view_clu_layout_impl.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "base/check.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_background_blur_backdrop.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "chrome/browser/ui/views/side_panel/side_panel.h"
#include "chrome/browser/ui/views/tabs/organizer/organizer_tray_view.h"
#include "clu/browser/multitask/clu_mini_rules.h"
#include "clu/browser/ui/views/frame/clu_anchor_view.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/ui/views/frame/clu_peek_view.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"

namespace clu {

namespace {

// How far inside the page card's top corners native bubbles hang from.
constexpr int kAnchorInset = 12;

// A side panel never takes more than this share of the page card.
constexpr float kMaxSidePanelFraction = 2.f / 3.f;

}  // namespace

BrowserViewCluLayoutImpl::BrowserViewCluLayoutImpl(
    std::unique_ptr<BrowserViewLayoutDelegate> delegate,
    BrowserWindowInterface* browser,
    BrowserViewLayoutViews views)
    : BrowserViewLayoutImpl(std::move(delegate), std::move(views)) {
  CHECK(browser);
  shell_ = std::make_unique<CluShell>(browser, this->views().browser_view);
  shell_->SetMultiContentsView(this->views().multi_contents_view);
}

BrowserViewCluLayoutImpl::~BrowserViewCluLayoutImpl() = default;

gfx::Size BrowserViewCluLayoutImpl::GetMinimumSize(
    const views::View* host) const {
  if (shell_->is_mini()) {
    return kMiniMinSize;
  }
  return gfx::Size(kMinWindowWidth, kMinWindowHeight);
}

BrowserViewCluLayoutImpl::ProposedLayout
BrowserViewCluLayoutImpl::CalculateProposedLayout(
    const BrowserLayoutParams& params) const {
  ProposedLayout layout;
  const gfx::Rect& area = params.visual_client_area;
  views::View* const browser_view = views().browser_view;

  // The window scrim covers the whole window; its visibility belongs to whoever shows it.
  if (IsParentedTo(views().window_scrim, browser_view)) {
    layout.AddChild(views().window_scrim, area);
  }

  // The frame WebUI fills the client area and draws the sidebar, the background and the page card.
  if (IsParentedTo(shell_->frame_view(), browser_view)) {
    layout.AddChild(shell_->frame_view(), area, /*visibility=*/true);
  }

  // Chromium's own chrome is not part of Clu's window.
  HideChild(layout, views().top_container);
  HideChild(layout, views().horizontal_tab_strip_region_view);
  HideChild(layout, views().vertical_tab_strip_region_view);
  HideChild(layout, views().vertical_tab_strip_background_blur_backdrop);
  HideChild(layout, views().vertical_tab_strip_top_corner);
  HideChild(layout, views().vertical_tab_strip_bottom_corner);
  HideChild(layout, views().organizer_tray);
  HideChild(layout, views().main_background_region);
  HideChild(layout, views().main_shadow_overlay);
  HideChild(layout, views().side_panel_content_transition_scrim);
  HideChild(layout, views().top_container_separator);

  // The page card, at the rect the frame WebUI draws it at (mid-tween: the current tick's rect).
  gfx::Rect card = shell_->shown_page_rect();
  card.Offset(area.OffsetFromOrigin());
  const gfx::Rect page_card = card;

  // Infobars sit at the top of the card and push the page down.
  if (IsParentedTo(views().infobar_container, browser_view)) {
    const bool infobar_visible = delegate().IsInfobarVisible();
    gfx::Rect infobar_bounds;
    if (infobar_visible) {
      infobar_bounds = gfx::Rect(
          card.x(), card.y(), card.width(),
          std::min(card.height(),
                   views().infobar_container->GetPreferredSize().height()));
      card.Inset(gfx::Insets::TLBR(infobar_bounds.height(), 0, 0, 0));
    }
    layout.AddChild(views().infobar_container, infobar_bounds,
                    infobar_visible);
  }

  // A side panel the user opened sits at the card's trailing edge, the page keeps at least
  // kContentsContainerMinimumWidth.
  gfx::Rect contents_bounds = card;
  // A page in full screen takes the whole card, which is then the whole window: an open side panel steps
  // aside with no room (its visibility stays, so it comes back when the page leaves full screen).
  const bool content_fullscreen = shell_->chrome_state().fullscreen &&
                                  shell_->chrome_state().content_fullscreen;
  if (content_fullscreen) {
    if (IsParentedToAndVisible(views().side_panel, browser_view)) {
      layout.AddChild(views().side_panel, gfx::Rect());
    }
  } else if (IsParentedToAndVisible(views().side_panel, browser_view)) {
    views::View* const panel = views().side_panel;
    const int min_width = panel->GetMinimumSize().width();
    const int max_width = std::max(
        min_width, static_cast<int>(card.width() * kMaxSidePanelFraction));
    const int room = std::max(
        0, card.width() - kCardInset - kContentsContainerMinimumWidth);
    const int panel_width = std::min(
        room,
        std::clamp(panel->GetPreferredSize().width(), min_width, max_width));
    gfx::Rect panel_bounds;
    if (panel_width > 0) {
      contents_bounds.set_width(card.width() - panel_width - kCardInset);
      panel_bounds = gfx::Rect(contents_bounds.right() + kCardInset, card.y(),
                               panel_width, card.height());
    }
    layout.AddChild(views().side_panel, panel_bounds);
  }

  // With no tab row shown (the empty state) the page area hides and the frame WebUI draws the card alone.
  CHECK(IsParentedTo(views().multi_contents_view, browser_view));
  contents_bounds.set_width(std::max(contents_bounds.width(), 1));
  contents_bounds.set_height(std::max(contents_bounds.height(), 1));
  // A split of 3 or 4: the active page at its pane's share, the
  // other panes in the views the shell hosts.
  const std::vector<gfx::Rect> panes = shell_->ExtraPaneRects(contents_bounds);
  if (!panes.empty()) {
    contents_bounds = panes[shell_->extra_active()];
    for (size_t i = 0; i < panes.size(); ++i) {
      views::WebView* const pane = shell_->ExtraPaneView(i);
      if (pane && IsParentedTo(pane, browser_view)) {
        layout.AddChild(pane, panes[i], /*visibility=*/shell_->page_shown());
      }
    }
  }
  layout.AddChild(views().multi_contents_view, contents_bounds,
                  /*visibility=*/shell_->page_shown());

  // The Peek layer (M6) covers the client area over the page; its card sits at
  // the window coordinates of the state.
  if (IsParentedTo(shell_->peek_view(), browser_view)) {
    layout.AddChild(shell_->peek_view(), area,
                    /*visibility=*/shell_->peek_shown());
  }

  // The anchor spans the card's top edge with no height: a bubble with a leading arrow (page info, permissions,
  // downloads) hangs from the card's top leading corner, one with a trailing arrow (passwords, bookmarks, zoom and
  // the other toolbar bubbles) from its top trailing corner, as in Chrome where those hang from the toolbar's end.
  if (IsParentedTo(shell_->anchor_view(), browser_view)) {
    layout.AddChild(shell_->anchor_view(),
                    gfx::Rect(page_card.x() + kAnchorInset,
                              page_card.y() + kAnchorInset,
                              std::max(0, page_card.width() - 2 * kAnchorInset),
                              0),
                    /*visibility=*/true);
  }
  if (IsParentedTo(shell_->extension_anchor_view(), browser_view)) {
    layout.AddChild(shell_->extension_anchor_view(),
                    shell_->extension_anchor_rect().value_or(gfx::Rect(
                        page_card.x() + kAnchorInset,
                        page_card.y() + kAnchorInset, 0, 0)),
                    /*visibility=*/true);
  }
  // An open downloads bubble follows its button; while the button does not show, the view waits at the card.
  if (IsParentedTo(shell_->downloads_anchor_view(), browser_view)) {
    layout.AddChild(shell_->downloads_anchor_view(),
                    shell_->DownloadsAnchorRect().value_or(gfx::Rect(
                        page_card.x() + kAnchorInset,
                        page_card.y() + kAnchorInset, 0, 0)),
                    /*visibility=*/true);
  }

  return layout;
}

gfx::Rect BrowserViewCluLayoutImpl::CalculateTopContainerLayout(
    ProposedLayout& layout,
    BrowserLayoutParams params,
    bool needs_exclusion) const {
  // Only reached when the top container lives outside the browser view (immersive full screen). It stays empty:
  // the frame WebUI's top row replaces the toolbar.
  return gfx::Rect(params.visual_client_area.x(), params.visual_client_area.y(),
                   params.visual_client_area.width(), 0);
}

void BrowserViewCluLayoutImpl::DoPreLayoutComputations(
    const BrowserLayoutParams& params) {
  BrowserViewLayoutImpl::DoPreLayoutComputations(params);
  shell_->UpdateForLayout(params.visual_client_area.size(),
                          is_fullscreen(delegate().GetBrowserWindowState()));
}

// Split panes keep Chromium's own corners, which equal look::kSplitRadius.
static_assert(MultiContentsView::kSplitViewContentCornerRadius ==
              look::kSplitRadius);

void BrowserViewCluLayoutImpl::DoPostLayoutVisualAdjustments(
    const BrowserLayoutParams& params) {
  BrowserViewLayoutImpl::DoPostLayoutVisualAdjustments(params);
  MultiContentsView* const contents = views().multi_contents_view;
  if (!contents) {
    return;
  }
  // The page's view sits in a ui::Layer under the contents' holder, so the holder's rounded corners clip it on Mac
  // too (NativeViewHostMac::SetNativeViewCornerRadii). An infobar takes the card's top corners.
  const float radius = shell_->layout().radius;
  const bool infobar_visible = delegate().IsInfobarVisible();
  const float top = infobar_visible ? 0.f : radius;
  contents->SetBackgroundRadii(gfx::RoundedCornersF(top, top, radius, radius));
  // The infobar then rounds the card's top corners itself and clips Chromium's shadow under it.
  views::View* const infobars = views().infobar_container;
  if (infobar_visible && IsParentedTo(infobars, views().browser_view.get())) {
    if (!infobars->layer()) {
      infobars->SetPaintToLayer();
      infobars->layer()->SetFillsBoundsOpaquely(false);
    }
    infobars->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(radius, radius, 0, 0));
    infobars->layer()->SetIsFastRoundedCorner(true);
    infobars->layer()->SetMasksToBounds(true);
  }
}

int BrowserViewCluLayoutImpl::GetDialogTop(const ProposedLayout& layout) const {
  if (const std::optional<gfx::Rect> frame =
          layout.GetBoundsFor(shell_->frame_view(), views().browser_view.get())) {
    return frame->y() + shell_->shown_page_rect().y();
  }
  return kCardInset;
}

void BrowserViewCluLayoutImpl::HideChild(ProposedLayout& layout,
                                         views::View* view) const {
  if (IsParentedTo(view, views().browser_view)) {
    layout.AddChild(view, gfx::Rect(), /*visibility=*/false);
  }
}

std::unique_ptr<BrowserViewLayout> CreateBrowserViewCluLayout(
    std::unique_ptr<BrowserViewLayoutDelegate> delegate,
    BrowserWindowInterface* browser,
    BrowserViewLayoutViews views) {
  return std::make_unique<BrowserViewCluLayoutImpl>(std::move(delegate),
                                                    browser, std::move(views));
}

}  // namespace clu
