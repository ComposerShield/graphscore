// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace graphscore::cooked_format {

constexpr std::uint32_t kMagic        = 0x4753434B;  // 'GSCK'
constexpr std::uint32_t kVersionMajor = 0;
constexpr std::uint32_t kVersionMinor = 1;

}  // namespace graphscore::cooked_format
