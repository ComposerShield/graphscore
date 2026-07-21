// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <graphscore/c_abi/graphscore_c_abi.h>

static_assert(sizeof(gs_result_t) == 12,
              "gs_result_t size mismatch");

TEST(RuntimeTest, VersionQueryReturnsExpected) {
  gs_version_t v = gs_query_version();
  EXPECT_EQ(v.major, static_cast<uint32_t>(GS_VERSION_MAJOR));
  EXPECT_EQ(v.minor, static_cast<uint32_t>(GS_VERSION_MINOR));
  EXPECT_EQ(v.patch, static_cast<uint32_t>(GS_VERSION_PATCH));
}

TEST(RuntimeTest, CreateDestroySucceeds) {
  gs_allocator_t alloc{};
  alloc.size    = sizeof(alloc);
  alloc.version = 1;
  alloc.context = nullptr;
  alloc.alloc   = nullptr;
  alloc.free    = nullptr;

  gs_player_t* player = nullptr;
  gs_result_t r = gs_player_create(&alloc, &player);
  EXPECT_EQ(r.code, GS_RESULT_SUCCESS);
  EXPECT_NE(player, nullptr);

  r = gs_player_destroy(player);
  EXPECT_EQ(r.code, GS_RESULT_SUCCESS);
}

TEST(RuntimeTest, CreateNullOutPlayerFails) {
  gs_result_t r = gs_player_create(nullptr, nullptr);
  EXPECT_EQ(r.code, GS_RESULT_INVALID_ARGUMENT);
}

TEST(RuntimeTest, PollDiagnosticsReturnsZero) {
  gs_diagnostics_t d{};
  gs_result_t r = gs_player_poll_diagnostics(nullptr, &d);
  EXPECT_EQ(r.code, GS_RESULT_SUCCESS);
  EXPECT_EQ(d.dropped_fifo_events, static_cast<uint64_t>(0));
  EXPECT_EQ(d.undersized_output_retries, static_cast<uint64_t>(0));
}

TEST(RuntimeTest, QueryCapacityReturnsSuccess) {
  uint32_t caps = 0;
  gs_result_t r = gs_player_query_capacity(nullptr, 256, 16, &caps);
  EXPECT_EQ(r.code, GS_RESULT_SUCCESS);
}

TEST(RuntimeTest, ProcessReturnsSuccess) {
  gs_process_params_t params{};
  params.size      = sizeof(params);
  params.version   = 1;
  params.frame_count = 0;
  params.output_count = nullptr;

  gs_result_t r = gs_player_process(nullptr, &params);
  EXPECT_EQ(r.code, GS_RESULT_SUCCESS);
}
