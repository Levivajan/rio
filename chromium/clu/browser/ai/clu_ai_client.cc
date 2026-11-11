// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ai/clu_ai_client.h"

#include <utility>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/process/launch.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
#include "base/apple/bundle_locations.h"
#endif

namespace clu {

namespace {

// The longest answer read back; the helper prints one line.
constexpr size_t kMaxAnswer = 2000;

base::FilePath HelperPath() {
#if BUILDFLAG(IS_MAC)
  return base::apple::OuterBundlePath()
      .Append("Contents")
      .Append("Helpers")
      .Append("rio-ai");
#else
  return base::FilePath();
#endif
}

std::optional<std::string> RunHelper(std::vector<std::string> args) {
  const base::FilePath path = HelperPath();
  if (path.empty() || !base::PathExists(path)) {
    return std::nullopt;
  }
  base::CommandLine command(path);
  for (const std::string& arg : args) {
    command.AppendArg(arg);
  }
  std::string out;
  int code = -1;
  // rio-ai exits on its own after 8 seconds at most.
  if (!base::GetAppOutputWithExitCode(command, &out, &code) || code != 0) {
    return std::nullopt;
  }
  if (out.size() > kMaxAnswer) {
    out.resize(kMaxAnswer);
  }
  if (!base::IsStringUTF8(out)) {
    return std::nullopt;
  }
  std::string line(base::TrimWhitespaceASCII(out, base::TRIM_ALL));
  if (line.empty()) {
    return std::nullopt;
  }
  return line;
}

}  // namespace

void AskOnDeviceModel(std::vector<std::string> args, AiReply reply) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::WithBaseSyncPrimitives(),
       base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&RunHelper, std::move(args)), std::move(reply));
}

std::optional<std::string> CleanModelTitle(std::string_view answer,
                                           size_t max_units) {
  std::string line(answer.substr(0, answer.find('\n')));
  base::RemoveChars(line, "\"*#`", &line);
  std::u16string text = base::UTF8ToUTF16(line);
  base::RemoveChars(text, u"\u201c\u201d", &text);
  base::TrimString(text, u" \t.:;,!-'", &text);
  if (text.empty() || text.size() > 3 * max_units) {
    return std::nullopt;
  }
  const std::u16string lower = base::ToLowerASCII(text);
  for (std::u16string_view refusal :
       {u"i'm sorry", u"i am sorry", u"i can't", u"i cannot", u"sorry",
        u"as an ai", u"title:"}) {
    if (lower.starts_with(refusal)) {
      return std::nullopt;
    }
  }
  if (text.size() > max_units) {
    // Cut at the last word boundary that fits.
    size_t cut = text.rfind(u' ', max_units);
    if (cut == std::u16string::npos || cut == 0) {
      return std::nullopt;
    }
    text.resize(cut);
    base::TrimString(text, u" .:;,-", &text);
  }
  return text.empty() ? std::nullopt
                      : std::optional<std::string>(base::UTF16ToUTF8(text));
}

}  // namespace clu
