// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/scheduler/graphscore_scheduler.hpp>

TEST(SchedulerTest, Compiles) {
  graphscore::Scheduler s;
  static_cast<void>(s);
  SUCCEED();
}
