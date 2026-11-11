// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_THEME_CLU_SPACE_THEMES_H_
#define CLU_BROWSER_THEME_CLU_SPACE_THEMES_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/values.h"
#include "clu/browser/theme/clu_theme_math.h"

// The space themes of every space: committed themes, which persist, and the picker's previews, which live in state
// only until a commit, a reset or a revert. A space without a theme shows its named color (accent from the color, the
// frame's default fill). Pure: CluAppSettings keeps the committed themes in Local State (`clu.space_themes`, {space
// id: theme}), because spaces span profiles there too.
namespace clu {

class CluSpaceThemes {
 public:
  CluSpaceThemes();
  CluSpaceThemes(const CluSpaceThemes&) = delete;
  CluSpaceThemes& operator=(const CluSpaceThemes&) = delete;
  ~CluSpaceThemes();

  // Reads stored themes. An entry that is not a valid theme (corrupt JSON shapes, junk numbers) or whose key is not
  // one of `space_ids` is dropped: the space shows its default theme. Returns how many entries were dropped, so the
  // caller writes the repaired value back once.
  int Load(const base::DictValue& stored, const std::set<std::string>& space_ids);
  // The committed themes as stored.
  base::DictValue Value() const;

  // The theme a space shows now: its preview, else its committed theme, else nullopt.
  std::optional<SpaceTheme> Shown(std::string_view space_id) const;
  // Its committed theme only.
  std::optional<SpaceTheme> Committed(std::string_view space_id) const;

  // theme.setSpaceTheme: commits and ends any preview. True when the committed value changed.
  bool Set(const std::string& space_id, const SpaceTheme& theme);
  // theme.resetSpaceTheme: drops the theme and any preview. True when anything changed.
  bool Reset(std::string_view space_id);
  // theme.previewSpaceTheme: state only. True when what the space shows changed.
  bool Preview(const std::string& space_id, const SpaceTheme& theme);
  // The picker closed without a commit: the space shows its committed theme again. True when that changed anything.
  bool RevertPreview(std::string_view space_id);
  // Spaces that are gone take their themes and previews along. True when anything was dropped.
  bool Retain(const std::set<std::string>& space_ids);

 private:
  std::map<std::string, SpaceTheme, std::less<>> committed_;
  std::map<std::string, SpaceTheme, std::less<>> previews_;
};

}  // namespace clu

#endif  // CLU_BROWSER_THEME_CLU_SPACE_THEMES_H_
