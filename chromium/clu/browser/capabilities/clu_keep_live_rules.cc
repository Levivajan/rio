// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_keep_live_rules.h"

namespace clu {

std::optional<std::string_view> KeepLiveReason(const KeepLiveFacts& facts) {
  if (!facts.normal_window) {
    return "window";
  }
  if (facts.split) {
    return "split";
  }
  return std::nullopt;
}

}  // namespace clu
