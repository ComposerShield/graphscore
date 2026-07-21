// SPDX-License-Identifier: Apache-2.0
//
// Pure-C consumer of graphscore_c_abi.h. Compiled as C99 to prove the
// header has no C++ leakage, platform-dependent type sizes, or non-
// forward-declared identifiers.

#include <graphscore/c_abi/graphscore_c_abi.h>

#include <stddef.h>

int main(void) {
  /* Verify compile-time availability of all types */
  gs_version_t      ver;
  gs_result_t       res;
  gs_allocator_t    alloc;
  gs_asset_descriptor_t desc;
  gs_midi_event_t   midi;
  gs_input_event_t  input;
  gs_process_params_t params;
  gs_diagnostics_t  diag;
  gs_player_t*      player = NULL;
  gs_asset_t*       asset  = NULL;

  /* Suppress unused-variable warnings */
  (void)ver;
  (void)res;
  (void)alloc;
  (void)desc;
  (void)midi;
  (void)input;
  (void)params;
  (void)diag;
  (void)player;
  (void)asset;

  return 0;
}
