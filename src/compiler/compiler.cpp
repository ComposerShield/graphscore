// SPDX-License-Identifier: Apache-2.0

#include <graphscore/compiler/graphscore_compiler.hpp>

namespace graphscore {
namespace {
constexpr int kCompilerVersion = 1;
}  // namespace

int compiler_version() {
  return kCompilerVersion;
}
}  // namespace graphscore
