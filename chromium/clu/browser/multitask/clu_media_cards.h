// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MULTITASK_CLU_MEDIA_CARDS_H_
#define CLU_BROWSER_MULTITASK_CLU_MEDIA_CARDS_H_

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/function_ref.h"

// The sidebar media card of one window (split-t5): the most recently started
// tab of the window that plays audible media out of sight. A page lives in one
// window's tab strip on Chromium, so each window keeps its own cards; the
// events come from Chromium's WebContentsObserver media calls and the toggle
// goes through content::MediaSession. Pure, so clu_unittests covers it.
namespace clu {

class CluMediaCards {
 public:
  enum class ToggleAction { kNone, kPause, kPlay };

  CluMediaCards();
  CluMediaCards(const CluMediaCards&) = delete;
  CluMediaCards& operator=(const CluMediaCards&) = delete;
  ~CluMediaCards();

  // The tabs the window shows now (the active tab, or both panes of a split). A
  // tab seen paused lets go of its card.
  void SetShown(const std::vector<std::string>& tab_ids);
  // MediaStartedPlaying and MediaStoppedPlaying (the last player of the tab).
  void Started(const std::string& tab_id);
  void Paused(const std::string& tab_id);
  // OnAudioStateChanged: a tab that was audible while its media played counts
  // as heard.
  void Audible(const std::string& tab_id, bool audible);
  // A main-frame navigation, a crash, or the page leaving the window ends its
  // media.
  void Reset(const std::string& tab_id);

  // media.toggle: what to do with the tab's media. Pausing holds the card, so
  // it stays with a Play button. kNone when the tab has nothing to toggle.
  ToggleAction Toggle(const std::string& tab_id);

  // The card's tab: the most recently started tab out of sight that plays heard
  // media (or plays while muted, so the card can show the crossed speaker), or
  // that the card paused. Nullopt for no card. `muted` reads the tab's mute.
  std::optional<std::string> Card(
      base::FunctionRef<bool(std::string_view)> muted) const;

  bool IsPlaying(std::string_view tab_id) const;

 private:
  struct Media {
    int64_t started = 0;
    bool playing = false;
    bool heard = false;
    bool audible_now = false;
    bool held = false;
  };

  Media& Entry(const std::string& tab_id);

  std::map<std::string, Media, std::less<>> media_;
  std::set<std::string, std::less<>> shown_;
  // Orders starts without a clock: only "which started last" matters.
  int64_t next_start_ = 0;
};

}  // namespace clu

#endif  // CLU_BROWSER_MULTITASK_CLU_MEDIA_CARDS_H_
