// SPDX-License-Identifier: Apache-2.0

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {
namespace {
constexpr int kCoreVersion = 1;
}  // namespace

int core_version() { return kCoreVersion; }
}  // namespace graphscore
