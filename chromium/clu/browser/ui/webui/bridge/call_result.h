// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_BRIDGE_CALL_RESULT_H_
#define CLU_BROWSER_UI_WEBUI_BRIDGE_CALL_RESULT_H_

#include <string_view>

#include "base/values.h"
#include "clu/common/mojom/bridge.mojom.h"

// Builders for mojom::CallResult, the answer of every bridge command (bridge.mojom).
namespace clu {

// A command that returns nothing: value_json "null".
mojom::CallResultPtr NullResult();

// `value` as the JSON text of the command's TS output type.
mojom::CallResultPtr ValueResult(base::Value value);

// A failed call. `message` is for logs and tests, never shown to the user.
mojom::CallResultPtr ErrorResult(mojom::ErrorCode code,
                                 std::string_view message = {});

// kRejected with a lane-defined `reason` the UI can branch on.
mojom::CallResultPtr RejectedResult(std::string_view reason);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_BRIDGE_CALL_RESULT_H_
