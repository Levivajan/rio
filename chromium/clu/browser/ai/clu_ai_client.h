// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_AI_CLU_AI_CLIENT_H_
#define CLU_BROWSER_AI_CLU_AI_CLIENT_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback_forward.h"

// Asks Apple's on-device model through rio-ai (clu_ai_helper.swift) off the UI thread. The answer is one cleaned line,
// or nullopt when the Mac has no model (before macOS 26, Apple Intelligence off), the helper is missing (test
// binaries, a build without it), it failed or took longer than 8 seconds. Callers keep their rules as the fallback.
namespace clu {

using AiReply = base::OnceCallback<void(std::optional<std::string>)>;

// `args` are rio-ai's: {"title", page title, url}, {"download", file name, page title, url}, {"summary", title, url,
// text} or {"tidy", numbered tabs}.
void AskOnDeviceModel(std::vector<std::string> args, AiReply reply);

// A model answer as a tab title: one line without quotes or a final period, at most `max_units` UTF-16 units cut at a
// word, nullopt when nothing usable is left (empty, an apology, a sentence instead of a name).
std::optional<std::string> CleanModelTitle(std::string_view answer,
                                           size_t max_units);

}  // namespace clu

#endif  // CLU_BROWSER_AI_CLU_AI_CLIENT_H_
