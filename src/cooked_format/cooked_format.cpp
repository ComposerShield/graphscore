// SPDX-License-Identifier: Apache-2.0

#include <graphscore/cooked_format/graphscore_cooked_format.hpp>

namespace graphscore::cooked_format {
namespace {
constexpr int kFormatVersion = 1;
}  // namespace

int format_version() {
  return kFormatVersion;
}
}  // namespace graphscore::cooked_format
