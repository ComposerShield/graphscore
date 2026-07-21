// SPDX-License-Identifier: Apache-2.0

#include <graphscore/scheduler/graphscore_scheduler.hpp>

namespace graphscore {
namespace {
constexpr int kSchedulerVersion = 1;
}  // namespace

int scheduler_version() {
  return kSchedulerVersion;
}
}  // namespace graphscore
