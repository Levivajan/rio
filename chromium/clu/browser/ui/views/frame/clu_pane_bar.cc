// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_pane_bar.h"

#include "clu/browser/ui/views/frame/clu_shell_hooks.h"

#include "clu/browser/ui/views/frame/clu_shell.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/reload_type.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/layer.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_constants.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/menu/menu_types.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace clu {
namespace {

// The split pane toolbar: the first glyph centered 21 in, a 37 pitch, close centered 21 in from the right.
constexpr int kFirstCenter = 21;
constexpr int kPitch = 37;
constexpr int kCloseFromRight = 21;
// The split view menu (32 x 24 at right - 69) and site controls (32 x 28 at right - 105).
constexpr int kMenuFromRight = 53;
constexpr int kSiteFromRight = 89;
constexpr int kWideButton = 32;
// Chromium's split outline (ContentsContainerOutline::kThickness).
constexpr int kOutline = 1;

// The split menu's items.
enum SplitMenuCommand { kAddPane = 1, kSeparate, kClosePane };
constexpr int kButton = 24;
// Every glyph is drawn 1:1 on a 16 pt canvas.
constexpr int kGlyph = 16;
constexpr int kLink = kGlyph;
// About 9.5 pt between the link ink and the host.
constexpr int kLinkGap = 8;
// The URL never runs under the buttons.
constexpr int kUrlClear = 8;

// Glyphs #737373, disabled #bfbfbf, the URL's host #666666 about 12.5 pt semibold and its port and path #7f7f7f,
// divider #f2f2f2 under a white bar. The dark set mirrors them on the dark card.
struct Palette {
  SkColor background;
  SkColor divider;
  SkColor glyph;
  SkColor disabled;
  SkColor text;
  SkColor path;
};
constexpr Palette kLightPalette{SK_ColorWHITE, SkColorSetRGB(0xf2, 0xf2, 0xf2),
                                SkColorSetRGB(0x73, 0x73, 0x73),
                                SkColorSetRGB(0xbf, 0xbf, 0xbf),
                                SkColorSetRGB(0x66, 0x66, 0x66),
                                SkColorSetRGB(0x7f, 0x7f, 0x7f)};
constexpr Palette kDarkPalette{SkColorSetRGB(0x23, 0x23, 0x25),
                               SkColorSetRGB(0x33, 0x33, 0x36),
                               SkColorSetRGB(0xa6, 0xa6, 0xa8),
                               SkColorSetRGB(0x5c, 0x5c, 0x60),
                               SkColorSetRGB(0xb0, 0xb0, 0xb3),
                               SkColorSetRGB(0x8a, 0x8a, 0x8e)};

// The pane toolbar glyphs, drawn 1:1 on a
// 16 pt canvas with round caps: back and forward 13 x 10.5, reload 12.5 x 15 with its head, link 13 x 13, site
// controls 16 x 16, split 14.5 x 14.5, close 12 x 12. Chromium's 20-unit icons drawn at 14 came out about half that.
#define DECLARE_VECTOR_COMMAND(x) using gfx::x;
DECLARE_VECTOR_COMMANDS
#undef DECLARE_VECTOR_COMMAND

constexpr gfx::PathElement kBackPath[] = {
    CANVAS_DIMENSIONS, 16, STROKE, 1.5f,
    MOVE_TO, 13.75f, 8, LINE_TO, 2.25f, 8,
    MOVE_TO, 2.25f, 8, LINE_TO, 6.75f, 3.5f,
    MOVE_TO, 2.25f, 8, LINE_TO, 6.75f, 12.5f};
constexpr gfx::PathElement kForwardPath[] = {
    CANVAS_DIMENSIONS, 16, STROKE, 1.5f,
    MOVE_TO, 2.25f, 8, LINE_TO, 13.75f, 8,
    MOVE_TO, 13.75f, 8, LINE_TO, 9.25f, 3.5f,
    MOVE_TO, 13.75f, 8, LINE_TO, 9.25f, 12.5f};
// A clockwise circle from 1:30 round to 12, its head at the top pointing right.
constexpr gfx::PathElement kReloadPath[] = {
    CANVAS_DIMENSIONS, 16, STROKE, 1.5f,
    MOVE_TO, 11.89f, 5.36f, ARC_TO, 5.5f, 5.5f, 0, 1, 1, 8, 3.75f,
    MOVE_TO, 8.5f, 3.75f, LINE_TO, 6.25f, 1.5f,
    MOVE_TO, 8.5f, 3.75f, LINE_TO, 6.25f, 6};
// Two chain links on the diagonal.
constexpr gfx::PathElement kLinkPath[] = {
    CANVAS_DIMENSIONS, 16, STROKE, 1.3f,
    MOVE_TO, 6.16f, 13.16f, LINE_TO, 10.76f, 8.56f,
    ARC_TO, 2.35f, 2.35f, 0, 0, 0, 7.44f, 5.24f, LINE_TO, 2.84f, 9.84f,
    ARC_TO, 2.35f, 2.35f, 0, 0, 0, 6.16f, 13.16f, CLOSE,
    MOVE_TO, 8.56f, 10.76f, LINE_TO, 13.16f, 6.16f,
    ARC_TO, 2.35f, 2.35f, 0, 0, 0, 9.84f, 2.84f, LINE_TO, 5.24f, 7.44f,
    ARC_TO, 2.35f, 2.35f, 0, 0, 0, 8.56f, 10.76f, CLOSE};
// Two sliders in a rounded square.
constexpr gfx::PathElement kSiteControlsPath[] = {
    CANVAS_DIMENSIONS, 16, STROKE, 1.4f,
    ROUND_RECT, 0.7f, 0.7f, 14.6f, 14.6f, 4,
    MOVE_TO, 4.25f, 5.75f, LINE_TO, 8.25f, 5.75f,
    MOVE_TO, 11.5f, 5.75f, LINE_TO, 11.75f, 5.75f,
    CIRCLE, 9.9f, 5.75f, 1.45f,
    MOVE_TO, 4.25f, 10.25f, LINE_TO, 4.5f, 10.25f,
    MOVE_TO, 7.75f, 10.25f, LINE_TO, 11.75f, 10.25f,
    CIRCLE, 6.1f, 10.25f, 1.45f};
// Two panes side by side.
constexpr gfx::PathElement kSplitPath[] = {
    CANVAS_DIMENSIONS, 16, STROKE, 1.4f,
    ROUND_RECT, 1.45f, 1.45f, 13.1f, 13.1f, 3,
    MOVE_TO, 8, 1.45f, LINE_TO, 8, 14.55f};
constexpr gfx::PathElement kClosePath[] = {
    CANVAS_DIMENSIONS, 16, STROKE, 1.5f,
    MOVE_TO, 2.75f, 2.75f, LINE_TO, 13.25f, 13.25f,
    MOVE_TO, 13.25f, 2.75f, LINE_TO, 2.75f, 13.25f};

constexpr gfx::VectorIconRep kBackRep[] = {{kBackPath}};
constexpr gfx::VectorIconRep kForwardRep[] = {{kForwardPath}};
constexpr gfx::VectorIconRep kReloadRep[] = {{kReloadPath}};
constexpr gfx::VectorIconRep kLinkRep[] = {{kLinkPath}};
constexpr gfx::VectorIconRep kSiteControlsRep[] = {{kSiteControlsPath}};
constexpr gfx::VectorIconRep kSplitRep[] = {{kSplitPath}};
constexpr gfx::VectorIconRep kCloseRep[] = {{kClosePath}};
constexpr gfx::VectorIcon kBackGlyph(kBackRep, 1u, "clu_pane_bar_back");
constexpr gfx::VectorIcon kForwardGlyph(kForwardRep, 1u, "clu_pane_bar_forward");
constexpr gfx::VectorIcon kReloadGlyph(kReloadRep, 1u, "clu_pane_bar_reload");
constexpr gfx::VectorIcon kLinkGlyph(kLinkRep, 1u, "clu_pane_bar_link");
constexpr gfx::VectorIcon kSiteControlsGlyph(kSiteControlsRep, 1u,
                                             "clu_pane_bar_site_controls");
constexpr gfx::VectorIcon kSplitGlyph(kSplitRep, 1u, "clu_pane_bar_split");
constexpr gfx::VectorIcon kCloseGlyph(kCloseRep, 1u, "clu_pane_bar_close");

// The host ends at the first port, path, query or fragment after it; a page that is not http(s) stays in one color.
size_t HostEnd(const std::u16string& text, const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return text.size();
  }
  // FormatUrl drops http:// and https://; skip either if it was kept, never a :// later in the path.
  size_t start = 0;
  for (std::u16string_view scheme : {u"http://", u"https://"}) {
    if (std::u16string_view(text).starts_with(scheme)) {
      start = scheme.size();
    }
  }
  if (start < text.size() && text[start] == u'[') {
    start = text.find(u']', start);
    if (start == std::u16string::npos) {
      return text.size();
    }
  }
  const size_t end = text.find_first_of(u":/?#", start);
  return end == std::u16string::npos ? text.size() : end;
}

// The drawn extent of a glyph at 1x.
gfx::Size InkSize(const gfx::ImageSkia& image) {
  if (image.isNull()) {
    return gfx::Size();
  }
  const SkBitmap& bitmap = image.GetRepresentation(1.0f).GetBitmap();
  int left = bitmap.width(), top = bitmap.height(), right = -1, bottom = -1;
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = 0; x < bitmap.width(); ++x) {
      if (SkColorGetA(bitmap.getColor(x, y)) > 0x20) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  return right < 0 ? gfx::Size()
                   : gfx::Size(right - left + 1, bottom - top + 1);
}

std::unique_ptr<views::ImageButton> MakeButton(
    views::Button::PressedCallback callback,
    const std::u16string& name) {
  std::unique_ptr<views::ImageButton> button =
      views::CreateVectorImageButton(std::move(callback));
  button->SetAccessibleName(name);
  button->SetTooltipText(name);
  views::InstallCircleHighlightPathGenerator(button.get());
  return button;
}

}  // namespace

CluPaneBar::CluPaneBar(BrowserWindowInterface* browser,
                       views::WebView* web_view)
    : browser_(browser) {
  // Its own layer, as the mini toolbar has: an unlayered view paints under the frame WebUI's view.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(true);
  SetVisible(false);
  back_ = AddChildView(MakeButton(
      base::BindRepeating(&CluPaneBar::Back, base::Unretained(this)), u"Back"));
  forward_ = AddChildView(MakeButton(
      base::BindRepeating(&CluPaneBar::Forward, base::Unretained(this)),
      u"Forward"));
  reload_ = AddChildView(MakeButton(
      base::BindRepeating(&CluPaneBar::Reload, base::Unretained(this)),
      u"Reload"));
  link_ = AddChildView(std::make_unique<views::ImageView>());
  url_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL,
      views::style::STYLE_PRIMARY,
      gfx::DirectionalityMode::DIRECTIONALITY_AS_URL));
  url_->SetElideBehavior(gfx::ELIDE_TAIL);
  url_->SetSubpixelRenderingEnabled(false);
  url_->SetAutoColorReadabilityEnabled(false);
  const gfx::FontList base;
  url_->SetFontList(base.DeriveWithSizeDelta(13 - base.GetFontSize())
                        .DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  path_ = AddChildView(std::make_unique<views::Label>(
      std::u16string(), views::style::CONTEXT_LABEL,
      views::style::STYLE_PRIMARY,
      gfx::DirectionalityMode::DIRECTIONALITY_AS_URL));
  path_->SetElideBehavior(gfx::ELIDE_TAIL);
  path_->SetSubpixelRenderingEnabled(false);
  path_->SetAutoColorReadabilityEnabled(false);
  path_->SetFontList(url_->font_list());
  site_ = AddChildView(MakeButton(
      base::BindRepeating(&CluPaneBar::SiteControls, base::Unretained(this)),
      u"Site Controls"));
  menu_ = AddChildView(MakeButton(
      base::BindRepeating(&CluPaneBar::SplitMenu, base::Unretained(this)),
      u"Split View Menu"));
  close_ = AddChildView(MakeButton(
      base::BindRepeating(&CluPaneBar::Close, base::Unretained(this)),
      u"Close Split View Pane"));

  attached_ = web_view->AddWebContentsAttachedCallback(
      base::BindRepeating(&CluPaneBar::Attached, base::Unretained(this)));
  detached_ = web_view->AddWebContentsDetachedCallback(
      base::BindRepeating(&CluPaneBar::Detached, base::Unretained(this)));
  Attached(web_view);
}

CluPaneBar::~CluPaneBar() = default;

void CluPaneBar::SetActive(bool active) {
  if (active == active_) {
    return;
  }
  active_ = active;
  Update();
}

std::u16string CluPaneBar::url_text() const {
  return std::u16string(url_->GetText()) + std::u16string(path_->GetText());
}

std::u16string CluPaneBar::host_text() const {
  return std::u16string(url_->GetText());
}

std::vector<gfx::Size> CluPaneBar::glyph_sizes() const {
  std::vector<gfx::Size> sizes;
  for (const views::ImageButton* button :
       {back_.get(), forward_.get(), reload_.get(), site_.get(), menu_.get(),
        close_.get()}) {
    sizes.push_back(InkSize(button->GetImage(views::Button::STATE_NORMAL)));
  }
  sizes.push_back(InkSize(link_->GetImage()));
  return sizes;
}

bool CluPaneBar::reload_visible() const {
  return reload_->GetVisible();
}

bool CluPaneBar::site_controls_visible() const {
  return site_->GetVisible();
}

gfx::Rect CluPaneBar::back_bounds() const {
  return back_->bounds();
}

gfx::Rect CluPaneBar::close_bounds() const {
  return close_->bounds();
}

bool CluPaneBar::split_menu_visible() const {
  return menu_->GetVisible();
}

gfx::Size CluPaneBar::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(available_size.width().value_or(0), kHeight);
}

void CluPaneBar::Layout(PassKey) {
  const int mid = (kHeight - kButton) / 2;
  // The offsets count from the pane's edge, which is Chromium's 1 pt outline; the bar sits inside the split padding
  // within it, so the buttons move out by that padding.
  const gfx::Insets padding =
      parent() ? parent()->GetInsets() - gfx::Insets(kOutline) : gfx::Insets();
  const int first = kFirstCenter - std::max(0, padding.left());
  const int out = std::max(0, padding.right());
  auto place = [&](views::View* view, int center) {
    view->SetBounds(center - kButton / 2, mid, kButton, kButton);
  };
  place(back_, first);
  place(forward_, first + kPitch);
  place(reload_, first + 2 * kPitch);
  place(close_, width() - (kCloseFromRight - out));
  menu_->SetBounds(width() - (kMenuFromRight - out) - kWideButton / 2, mid,
                   kWideButton, kButton);
  site_->SetBounds(width() - (kSiteFromRight - out) - kWideButton / 2,
                   (kHeight - 28) / 2, kWideButton, 28);

  // The link glyph and the URL as one group centered on the pane, kept clear of the buttons on both sides.
  const int left = (reload_->GetVisible() ? reload_->bounds().right()
                                          : forward_->bounds().right()) +
                   kUrlClear;
  const int right =
      (site_->GetVisible() ? site_->x() : close_->x()) - kUrlClear;
  // The host keeps its room first; the path is cut before it.
  const int room = std::max(0, right - left - kLink - kLinkGap);
  const int host = std::min(url_->GetPreferredSize().width(), room);
  const int path = std::min(path_->GetPreferredSize().width(), room - host);
  const int group = kLink + kLinkGap + host + path;
  const int start =
      std::clamp(width() / 2 - group / 2, left, std::max(left, right - group));
  link_->SetBounds(start, (kHeight - kLink) / 2, kLink, kLink);
  url_->SetBounds(start + kLink + kLinkGap, 0, host, kHeight);
  path_->SetBounds(url_->bounds().right(), 0, path, kHeight);
}

bool CluPaneBar::Dark() const {
  const ui::ColorProvider* provider = GetColorProvider();
  return provider &&
         color_utils::IsDark(provider->GetColor(ui::kColorPrimaryBackground));
}

void CluPaneBar::OnPaint(gfx::Canvas* canvas) {
  const Palette& p = Dark() ? kDarkPalette : kLightPalette;
  canvas->DrawColor(p.background);
  canvas->FillRect(gfx::Rect(0, height() - 1, width(), 1), p.divider);
}

void CluPaneBar::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

bool CluPaneBar::OnMousePressed(const ui::MouseEvent& event) {
  Focus();
  return true;
}

void CluPaneBar::DidFinishNavigation(content::NavigationHandle* handle) {
  if (handle->IsInPrimaryMainFrame()) {
    Update();
  }
}

void CluPaneBar::DidStartLoading() {
  Update();
}

void CluPaneBar::DidStopLoading() {
  Update();
}

void CluPaneBar::Attached(views::WebView* web_view) {
  Observe(web_view->web_contents());
  Update();
}

void CluPaneBar::Detached(views::WebView* web_view) {
  Observe(nullptr);
  Update();
}

void CluPaneBar::Update() {
  content::WebContents* contents = web_contents();
  content::NavigationController* controller =
      contents ? &contents->GetController() : nullptr;
  back_->SetEnabled(controller && controller->CanGoBack());
  forward_->SetEnabled(controller && controller->CanGoForward());
  // The focused pane adds reload, site controls and the split menu; the other keeps back, forward, its address and
  // close.
  reload_->SetVisible(active_);
  site_->SetVisible(active_);
  menu_->SetVisible(active_);
  const GURL url = contents ? contents->GetVisibleURL() : GURL();
  const std::u16string text =
      url.is_valid()
          ? url_formatter::FormatUrl(
                url,
                url_formatter::kFormatUrlOmitDefaults |
                    url_formatter::kFormatUrlOmitHTTPS |
                    url_formatter::kFormatUrlOmitTrivialSubdomains |
                    url_formatter::kFormatUrlOmitTrailingSlashOnBareHostname,
                base::UnescapeRule::SPACES, nullptr, nullptr, nullptr)
          : std::u16string();
  // The host is darker, and the port and path after it a lighter grey.
  const size_t host_end = HostEnd(text, url);
  url_->SetText(text.substr(0, host_end));
  path_->SetText(text.substr(host_end));
  url_->SetTooltipText(text);
  path_->SetTooltipText(text);
  InvalidateLayout();
  SchedulePaint();
}

void CluPaneBar::UpdateColors() {
  const Palette& p = Dark() ? kDarkPalette : kLightPalette;
  const views::IconColors colors(p.glyph, p.disabled);
  views::SetImageFromVectorIconWithColor(back_, kBackGlyph, kGlyph, colors);
  views::SetImageFromVectorIconWithColor(forward_, kForwardGlyph, kGlyph,
                                         colors);
  views::SetImageFromVectorIconWithColor(reload_, kReloadGlyph, kGlyph, colors);
  views::SetImageFromVectorIconWithColor(close_, kCloseGlyph, kGlyph, colors);
  views::SetImageFromVectorIconWithColor(site_, kSiteControlsGlyph, kGlyph,
                                         colors);
  views::SetImageFromVectorIconWithColor(menu_, kSplitGlyph, kGlyph, colors);
  link_->SetImage(ui::ImageModel::FromVectorIcon(kLinkGlyph, p.glyph, kLink));
  url_->SetEnabledColor(p.text);
  url_->SetBackgroundColor(p.background);
  path_->SetEnabledColor(p.path);
  path_->SetBackgroundColor(p.background);
  SchedulePaint();
}

void CluPaneBar::Back() {
  if (content::WebContents* contents = web_contents();
      contents && contents->GetController().CanGoBack()) {
    contents->GetController().GoBack();
  }
}

void CluPaneBar::Forward() {
  if (content::WebContents* contents = web_contents();
      contents && contents->GetController().CanGoForward()) {
    contents->GetController().GoForward();
  }
}

void CluPaneBar::Reload() {
  if (content::WebContents* contents = web_contents()) {
    contents->GetController().Reload(content::ReloadType::NORMAL, true);
  }
}

// As Chromium's mini toolbar closes its pane: the tab closes, and Clu's row rules decide what that means (a Today
// row archives, a pinned row unloads to its pinned page).
void CluPaneBar::Close() {
  content::WebContents* contents = web_contents();
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!contents || !model ||
      model->GetIndexOfWebContents(contents) == TabStripModel::kNoTab) {
    return;
  }
  model->CloseWebContents(contents, TabCloseTypes::CLOSE_USER_GESTURE |
                                        TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
}

void CluPaneBar::Focus() {
  content::WebContents* contents = web_contents();
  TabStripModel* model = browser_ ? browser_->GetTabStripModel() : nullptr;
  if (!contents || !model) {
    return;
  }
  const int index = model->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab && index != model->active_index()) {
    model->ActivateTabAt(index);
  }
}

// The pane's site controls, as the URL pill's glyph opens them, hanging from the button.
void CluPaneBar::SiteControls() {
  Focus();
  CluShell* shell = CluShell::From(browser_);
  if (!shell || !shell->client() || !shell->frame_view()) {
    return;
  }
  gfx::Rect anchor = site_->GetLocalBounds();
  views::View::ConvertRectToScreen(site_, &anchor);
  anchor = views::View::ConvertRectFromScreen(shell->frame_view(), anchor);
  shell->client()->OpenSiteControls(anchor);
}

// Add a pane, separate this one into its own tab, or close it.
void CluPaneBar::SplitMenu() {
  Focus();
  if (!GetWidget()) {
    return;
  }
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItem(kAddPane, u"Add Split View");
  menu_model_->AddItem(kSeparate, u"Separate Pane");
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(kClosePane, u"Close Pane");
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  gfx::Rect bounds = menu_->GetLocalBounds();
  views::View::ConvertRectToScreen(menu_, &bounds);
  menu_runner_->RunMenuAt(GetWidget(), /*button_controller=*/nullptr, bounds,
                          views::MenuAnchorPosition::kTopLeft,
                          ui::mojom::MenuSourceType::kMouse);
}

bool CluPaneBar::IsCommandIdEnabled(int command_id) const {
  return web_contents() != nullptr;
}

void CluPaneBar::ExecuteCommand(int command_id, int event_flags) {
  CluShell* shell = CluShell::From(browser_);
  CluShell::Client* client = shell ? shell->client() : nullptr;
  if (command_id == kClosePane) {
    Close();
  } else if (client && command_id == kSeparate) {
    client->RunCommand("split.separate");
  } else if (client && command_id == kAddPane) {
    client->RunCommand("split.add");
  }
}

BEGIN_METADATA(CluPaneBar)
END_METADATA

std::unique_ptr<views::View> CreatePaneBar(BrowserWindowInterface* browser,
                                           views::View* contents_web_view) {
  views::WebView* web_view = views::AsViewClass<views::WebView>(contents_web_view);
  if (!browser || !web_view ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return nullptr;
  }
  return std::make_unique<CluPaneBar>(browser, web_view);
}

views::View* PaneBarOf(views::View* container) {
  if (!container) {
    return nullptr;
  }
  for (views::View* child : container->children()) {
    if (views::IsViewClass<CluPaneBar>(child)) {
      return child;
    }
  }
  return nullptr;
}

bool UpdatePaneBar(views::View* container, bool in_split, bool active) {
  CluPaneBar* bar = views::AsViewClass<CluPaneBar>(PaneBarOf(container));
  if (!bar) {
    return false;
  }
  bar->SetActive(active);
  if (bar->GetVisible() != in_split) {
    bar->SetVisible(in_split);
    container->InvalidateLayout();
  }
  return true;
}

}  // namespace clu
