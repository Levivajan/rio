// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_text_recognizer.h"

#import <Vision/Vision.h>

#include <utility>

#include "base/apple/scoped_cftyperef.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/thread_pool.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/utils/mac/SkCGUtils.h"

namespace clu {

namespace {

// A region bigger than this is not worth reading (and would hold the worker for seconds).
constexpr int kMaxSide = 8192;

std::u16string Recognize(const SkBitmap& bitmap) {
  if (bitmap.drawsNothing() || bitmap.width() > kMaxSide ||
      bitmap.height() > kMaxSide) {
    return std::u16string();
  }
  @autoreleasepool {
    base::apple::ScopedCFTypeRef<CGImageRef> image(
        SkCreateCGImageRefWithColorspace(bitmap, nullptr));
    if (!image) {
      return std::u16string();
    }
    VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc] init];
    request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
    request.usesLanguageCorrection = YES;
    VNImageRequestHandler* handler =
        [[VNImageRequestHandler alloc] initWithCGImage:image.get()
                                               options:@{}];
    NSError* error = nil;
    if (![handler performRequests:@[ request ] error:&error]) {
      return std::u16string();
    }
    // Vision reports the observations top to bottom.
    NSMutableArray<NSString*>* lines = [NSMutableArray array];
    for (VNRecognizedTextObservation* observation in request.results) {
      VNRecognizedText* best = [observation topCandidates:1].firstObject;
      if (best.string.length) {
        [lines addObject:best.string];
      }
    }
    return base::SysNSStringToUTF16([lines componentsJoinedByString:@"\n"]);
  }
}

}  // namespace

void RecognizeText(const SkBitmap& bitmap,
                   base::OnceCallback<void(std::u16string)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&Recognize, bitmap), std::move(done));
}

}  // namespace clu
