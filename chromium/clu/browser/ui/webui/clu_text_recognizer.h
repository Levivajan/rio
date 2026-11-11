// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_TEXT_RECOGNIZER_H_
#define CLU_BROWSER_UI_WEBUI_CLU_TEXT_RECOGNIZER_H_

#include <string>

#include "base/functional/callback_forward.h"

class SkBitmap;

namespace clu {

// Capture text: macOS Vision reads the text in `bitmap` off the UI thread. `done` runs on the calling
// sequence with the lines top to bottom, joined by newlines, or empty when it found none or the bitmap is empty.
void RecognizeText(const SkBitmap& bitmap,
                   base::OnceCallback<void(std::u16string)> done);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_TEXT_RECOGNIZER_H_
