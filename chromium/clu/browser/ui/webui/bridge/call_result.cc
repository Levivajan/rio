// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/bridge/call_result.h"

#include <optional>
#include <string>
#include <utility>

#include "base/json/json_writer.h"

namespace clu {

mojom::CallResultPtr NullResult() {
  return mojom::CallResult::NewValueJson("null");
}

mojom::CallResultPtr ValueResult(base::Value value) {
  std::optional<std::string> json = base::WriteJson(value);
  if (!json) {
    return ErrorResult(mojom::ErrorCode::kRejected, "unserializable result");
  }
  return mojom::CallResult::NewValueJson(std::move(*json));
}

mojom::CallResultPtr ErrorResult(mojom::ErrorCode code,
                                 std::string_view message) {
  return mojom::CallResult::NewError(mojom::CallError::New(
      code,
      message.empty() ? std::nullopt : std::make_optional<std::string>(message),
      std::nullopt));
}

mojom::CallResultPtr RejectedResult(std::string_view reason) {
  return mojom::CallResult::NewError(mojom::CallError::New(
      mojom::ErrorCode::kRejected, std::nullopt, std::string(reason)));
}

}  // namespace clu
