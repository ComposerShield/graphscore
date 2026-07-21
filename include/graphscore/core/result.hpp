// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace graphscore {

enum class ResultCode : std::int32_t {
  kSuccess = 0,
  kInvalidArgument = 1,
  kOutOfMemory = 2,
  kVersionMismatch = 3,
  kCorruptedData = 4,
  kInternalError = 5,
};

class Result {
public:
  constexpr Result() = default;
  constexpr explicit Result(ResultCode code) : code_(code) {}

  [[nodiscard]] constexpr ResultCode code() const noexcept { return code_; }
  [[nodiscard]] constexpr bool ok() const noexcept { return code_ == ResultCode::kSuccess; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok(); }

  [[nodiscard]] constexpr bool operator==(const Result&) const = default;

private:
  ResultCode code_ = ResultCode::kSuccess;
};

}  // namespace graphscore
