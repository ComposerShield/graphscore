// SPDX-License-Identifier: Apache-2.0

#include <graphscore/accessibility/graphscore_accessibility.hpp>

namespace graphscore {
namespace {
constexpr int kAccessibilityVersion = 1;
}  // namespace

int accessibility_version() { return kAccessibilityVersion; }
}  // namespace graphscore
