// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_MENU_APPEARANCE_H_
#define CLU_BROWSER_UI_WEBUI_CLU_MENU_APPEARANCE_H_

#include <memory>
#include <optional>

namespace views {
class Widget;
}

namespace clu {

// The sidebar's native menus wear the chrome set of the WebUI that opened them: a context NSMenu takes the
// appearance of the window it opens for, so for the menu's run the window takes the light or dark Aqua appearance and
// gets its own back after. The menu run is synchronous on Mac. Nothing changes for a missing `light`.
class ScopedMenuAppearance {
 public:
  ScopedMenuAppearance(views::Widget* widget, std::optional<bool> light);
  ScopedMenuAppearance(const ScopedMenuAppearance&) = delete;
  ScopedMenuAppearance& operator=(const ScopedMenuAppearance&) = delete;
  ~ScopedMenuAppearance();

  // The appearance the window wears now: "light", "dark" or "inherit" (none of its own), for the browser tests.
  static const char* WindowAppearanceForTesting(views::Widget* widget);

 private:
  struct Saved;
  std::unique_ptr<Saved> saved_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_MENU_APPEARANCE_H_
