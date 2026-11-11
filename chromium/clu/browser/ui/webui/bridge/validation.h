// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_BRIDGE_VALIDATION_H_
#define CLU_BROWSER_UI_WEBUI_BRIDGE_VALIDATION_H_

#include <cstddef>
#include <string_view>

// The checks behind bridge_validators.cc, ported from the zod helpers in src/shared/schema.ts. Strings arrive as
// UTF-8 over mojo and are measured the way zod measures them: in UTF-16 code units (JS String.length).
namespace clu {

// MAX_URL in src/shared/schema.ts: the cap on URLs and on JSON strings.
inline constexpr size_t kMaxUrlLength = 32 * 1024;
// The cap on one JSON text field. The whole message cap in the TS guard is 256 KiB.
inline constexpr size_t kMaxJsonTextBytes = 256 * 1024;

// Well-formed UTF-8 (noncharacters allowed, as in a JS string).
bool IsValidUtf8(std::string_view value);

// Well-formed UTF-8 of at most `max` UTF-16 code units.
bool IsUtf16LengthAtMost(std::string_view value, size_t max);

// id() in src/shared/schema.ts: a UUID v4 in either case.
bool IsUuidV4(std::string_view value);

// int() over a wide range: a finite whole number within JS's safe integers.
bool IsWholeNumber(double value);

// json() in src/shared/schema.ts: JSON text whose strings are at most kMaxUrlLength, arrays at most 1,000 items and
// object keys at most 256 code units.
bool IsJsonText(std::string_view text);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_BRIDGE_VALIDATION_H_
