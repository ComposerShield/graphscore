// SPDX-License-Identifier: Apache-2.0

#include <graphscore/c_abi/graphscore_c_abi.h>
#include <graphscore/runtime_impl/graphscore_runtime_impl.hpp>

namespace {

graphscore::RuntimeImpl g_impl;

}  // namespace

extern "C" {

GS_API gs_version_t gs_query_version(void) {
  gs_version_t v{};
  v.size    = sizeof(v);
  v.version = 1;
  v.major   = GS_VERSION_MAJOR;
  v.minor   = GS_VERSION_MINOR;
  v.patch   = GS_VERSION_PATCH;
  return v;
}

GS_API gs_result_t gs_player_create(const gs_allocator_t* /*allocator*/,
                                    gs_player_t** out_player) {
  (void)g_impl;
  if (out_player == nullptr) {
    gs_result_t r{};
    r.size    = sizeof(r);
    r.version = 1;
    r.code    = GS_RESULT_INVALID_ARGUMENT;
    return r;
  }
  *out_player = reinterpret_cast<gs_player_t*>(&g_impl);
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_destroy(gs_player_t* /*player*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_load(gs_player_t* /*player*/,
                                  const gs_asset_descriptor_t* /*asset*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_unload(gs_player_t* /*player*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_start_designated(gs_player_t* /*player*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_start_uuid(gs_player_t* /*player*/,
                                        const char* /*uuid_utf8*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_pause(gs_player_t* /*player*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_resume(gs_player_t* /*player*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_stop(gs_player_t* /*player*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_reset(gs_player_t* /*player*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_query_capacity(
    const gs_player_t* /*player*/, uint32_t /*max_block_size*/,
    uint32_t /*max_input_events*/, uint32_t* out_required_output_capacity) {
  if (out_required_output_capacity != nullptr) {
    *out_required_output_capacity = 0;
  }
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_process(gs_player_t* /*player*/,
                                     gs_process_params_t* params) {
  if (params != nullptr && params->output_count != nullptr) {
    *params->output_count = 0;
  }
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_poll_diagnostics(const gs_player_t* /*player*/,
                                              gs_diagnostics_t* out_diag) {
  if (out_diag != nullptr) {
    out_diag->size                      = sizeof(*out_diag);
    out_diag->version                   = 1;
    out_diag->dropped_fifo_events       = 0;
    out_diag->undersized_output_retries = 0;
  }
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

GS_API gs_result_t gs_player_reset_diagnostics(gs_player_t* /*player*/) {
  gs_result_t r{};
  r.size    = sizeof(r);
  r.version = 1;
  r.code    = GS_RESULT_SUCCESS;
  return r;
}

}  // extern "C"
