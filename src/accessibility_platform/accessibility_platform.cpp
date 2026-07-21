// SPDX-License-Identifier: Apache-2.0

#include <graphscore/accessibility_platform/graphscore_accessibility_platform.hpp>

namespace graphscore {
namespace {
constexpr int kAccessibilityPlatformVersion = 1;
}  // namespace

int accessibility_platform_version() { return kAccessibilityPlatformVersion; }
}  // namespace graphscore
