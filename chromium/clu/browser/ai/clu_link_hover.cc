// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ai/clu_link_hover.h"

namespace clu {

namespace {

LinkHoverHandler g_handler = nullptr;

}  // namespace

void SetLinkHoverHandler(LinkHoverHandler handler) {
  g_handler = handler;
}

void OnTargetUrl(content::WebContents* source, const GURL& url) {
  if (g_handler) {
    g_handler(source, url);
  }
}

}  // namespace clu
