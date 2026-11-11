// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_library_thumbs.h"

#include <optional>
#include <utility>
#include <vector>

#import <Foundation/Foundation.h>
#import <QuickLookThumbnailing/QuickLookThumbnailing.h>

#include "base/apple/foundation_util.h"
#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "skia/ext/skia_utils_mac.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"

namespace clu {

namespace {

std::string PngDataUrl(CGImageRef image) {
  if (!image) {
    return std::string();
  }
  const SkBitmap bitmap = skia::CGImageToSkBitmap(image);
  if (bitmap.drawsNothing()) {
    return std::string();
  }
  std::optional<std::vector<uint8_t>> png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
  if (!png) {
    return std::string();
  }
  return "data:image/png;base64," + base::Base64Encode(*png);
}

}  // namespace

void LibraryThumbnail(const base::FilePath& path,
                      base::OnceCallback<void(std::string)> callback) {
  scoped_refptr<base::SequencedTaskRunner> reply =
      base::SequencedTaskRunner::GetCurrentDefault();
  QLThumbnailGenerationRequest* request = [[QLThumbnailGenerationRequest alloc]
        initWithFileAtURL:base::apple::FilePathToNSURL(path)
                     size:CGSizeMake(kLibraryThumbPixels, kLibraryThumbPixels)
                    scale:1.0
      representationTypes:QLThumbnailGenerationRequestRepresentationTypeAll];
  __block base::OnceCallback<void(std::string)> block_callback =
      std::move(callback);
  // QuickLook calls back once, on a queue of its own: encode there, answer on the caller's sequence.
  [QLThumbnailGenerator.sharedGenerator
      generateBestRepresentationForRequest:request
                         completionHandler:^(
                             QLThumbnailRepresentation* thumbnail,
                             NSError* error) {
                           std::string url =
                               thumbnail ? PngDataUrl(thumbnail.CGImage)
                                         : std::string();
                           reply->PostTask(
                               FROM_HERE,
                               base::BindOnce(std::move(block_callback),
                                              std::move(url)));
                         }];
}

}  // namespace clu
