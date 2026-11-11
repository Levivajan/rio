// Copyright 2026 The Rio Authors. All rights reserved.

// rio-ai: the app settings AI tab's on-device model (docs/chromium/parity-ai.md). Apple's Foundation Models API is
// Swift only, so the browser runs this small tool (Rio.app/Contents/Helpers/rio-ai) with one task in its arguments and
// reads one line back. Nothing leaves the Mac.
//
//   rio-ai title <page title> <url>              a short name for a pinned tab
//   rio-ai download <file name> <page title> <url>  a clearer file name stem for a download
//   rio-ai summary <page title> <url> <page text>   two or three sentences for a link preview
//   rio-ai tidy <numbered tabs, one per line>        a topic per tab for Tidy Today, as "1: Travel | 2: Python"
//
// Exit 0 with the answer on stdout, 2 when the model is unavailable on this Mac, 3 when it failed or took longer than
// 8 seconds, 64 for bad arguments. The browser falls back to its rules on anything but 0.

import Foundation
import FoundationModels

let kTimeoutSeconds = 8.0
let kMaxInput = 6000

func clip(_ s: String, _ n: Int) -> String { s.count > n ? String(s.prefix(n)) : s }

func prompt(for args: [String]) -> (instructions: String, prompt: String)? {
  guard args.count >= 2 else { return nil }
  switch args[1] {
  case "title" where args.count == 4:
    return (
      "You name pinned browser tabs. Answer with a short title of one to four words: the page's subject (a place, "
        + "person, product or document name, kept whole) or what the page is for. Drop the website's own name, counts "
        + "like (2) and separators. Examples: 'Rio de Janeiro - Wikipedia' is 'Rio de Janeiro'; 'National Weather "
        + "Service' is 'Weather Alerts'; 'Apple' on apple.com is 'Apple Products'; 'MDN Web Docs' is 'JavaScript "
        + "Reference'. No quotes, no emoji, no punctuation at the end, no explanation.",
      "Page title: \(clip(args[2], 400))\nAddress: \(clip(args[3], 400))")
  case "download" where args.count == 5:
    return (
      "You rename downloaded files so they are easy to find later. Answer with a clear file name of two to six words "
        + "separated by spaces, without the extension, no quotes, no explanation.",
      "File name: \(clip(args[2], 300))\nDownloaded from the page: \(clip(args[3], 300))\nAddress: \(clip(args[4], 400))")
  case "summary" where args.count == 5:
    return (
      "You preview web pages for someone deciding whether to open a link. Answer with two or three short plain "
        + "sentences about what the page says. No lists, no markdown, no preamble.",
      "Title: \(clip(args[2], 400))\nAddress: \(clip(args[3], 400))\nText:\n\(clip(args[4], kMaxInput))")
  case "tidy" where args.count == 3:
    return (
      "You sort a browser's open tabs into a few broad topics so related tabs can sit together. You get numbered "
        + "tabs with their titles and sites. Pick at most five topics, each one or two words, broad enough that several "
        + "tabs share it: a city's beaches, mountains and Wikipedia page are all that city; a programming language's "
        + "site and its docs are both that language. Never copy a tab's title or site as its topic. For every tab write "
        + "its number, a colon and its topic, and separate the tabs with |. Example answer: 1: Rio | 2: Python | "
        + "3: Rio | 4: News | 5: Python. No other words.",
      clip(args[2], kMaxInput))
  default:
    return nil
  }
}

guard let task = prompt(for: CommandLine.arguments) else { exit(64) }

if #available(macOS 26.0, *) {
  guard case .available = SystemLanguageModel.default.availability else { exit(2) }
  DispatchQueue.main.asyncAfter(deadline: .now() + kTimeoutSeconds) { exit(3) }
  Task {
    do {
      let session = LanguageModelSession(instructions: task.instructions)
      let answer = try await session.respond(to: task.prompt)
      let line = answer.content
        .replacingOccurrences(of: "\n", with: " ")
        .trimmingCharacters(in: CharacterSet.whitespacesAndNewlines.union(CharacterSet(charactersIn: "\"“”'")))
      guard !line.isEmpty else { exit(3) }
      FileHandle.standardOutput.write(Data(line.utf8))
      exit(0)
    } catch {
      exit(3)
    }
  }
  dispatchMain()
} else {
  exit(2)
}
