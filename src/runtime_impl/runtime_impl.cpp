// SPDX-License-Identifier: Apache-2.0

#include <graphscore/runtime_impl/graphscore_runtime_impl.hpp>

namespace graphscore {
namespace {
constexpr int kRuntimeImplVersion = 1;
}  // namespace

int runtime_impl_version() {
  return kRuntimeImplVersion;
}
}  // namespace graphscore
