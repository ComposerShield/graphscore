// SPDX-License-Identifier: Apache-2.0
//
// Pure-C consumer of graphscore_c_abi.h (ADR 0003 §7.4). Compiled as C99 via
// the CMake C_STANDARD property — no compiler-specific flags — to prove the
// header has no C++ leakage, no platform-dependent type sizes, and no
// identifier that is not forward-declared.
//
// This translation unit is the whole test: if it compiles and links, the
// header is consumable from C; if `main` returns 0, the shared library's
// exported entry points are callable through nothing but the public header.

#include <graphscore/c_abi/graphscore_c_abi.h>

#include <stddef.h>
#include <stdio.h>

/* The allocator callback types must be implementable from C. These two do
 * nothing: the runtime does not allocate through them in M1. */
static void* test_alloc(void* context, uint64_t size_bytes,
                        uint64_t alignment) {
  (void)context;
  (void)size_bytes;
  (void)alignment;
  return NULL;
}

static void test_free(void* context, void* ptr, uint64_t size_bytes,
                      uint64_t alignment) {
  (void)context;
  (void)ptr;
  (void)size_bytes;
  (void)alignment;
}

static int check(int condition, const char* what) {
  if (!condition) {
    fprintf(stderr, "gs_c_consumer: FAILED: %s\n", what);
    return 1;
  }
  return 0;
}

int main(void) {
  int failures = 0;

  /* --- every declared type is nameable and instantiable from C --------- */

  gs_version_t          ver;
  gs_result_t           res;
  gs_allocator_t        alloc;
  gs_asset_descriptor_t desc;
  gs_midi_event_t       midi;
  gs_input_event_t      input;
  gs_process_params_t   params;
  gs_diagnostics_t      diag;
  gs_player_t*          player = NULL;
  gs_asset_t*           asset  = NULL;
  gs_bool32_t           flag   = 0;

  (void)desc;
  (void)midi;
  (void)input;
  (void)params;
  (void)asset;
  (void)flag;

  /* The opaque handle types are usable as pointers without their definitions
   * being visible, which is what makes them opaque. */
  failures += check(player == NULL, "gs_player_t* is usable when opaque");

  /* Callback typedefs must accept C functions with the declared signatures. */
  alloc.size    = (uint32_t)sizeof(alloc);
  alloc.version = 1;
  alloc.context = NULL;
  alloc.alloc   = &test_alloc;
  alloc.free    = &test_free;

  /* --- the version query is callable and agrees with the header -------- */

  ver = gs_query_version();

  failures += check(ver.size == (uint32_t)sizeof(gs_version_t),
                    "gs_query_version reports its own struct size");
  failures += check(ver.major == GS_VERSION_MAJOR,
                    "runtime major version matches the header");
  failures += check(ver.minor == GS_VERSION_MINOR,
                    "runtime minor version matches the header");
  failures += check(ver.patch == GS_VERSION_PATCH,
                    "runtime patch version matches the header");

  /* --- lifecycle entry points are callable through the header alone ---- */

  res = gs_player_create(&alloc, &player);
  failures += check(res.code == GS_RESULT_SUCCESS, "gs_player_create");
  failures += check(player != NULL, "gs_player_create yields a handle");

  res = gs_player_poll_diagnostics(player, &diag);
  failures +=
      check(res.code == GS_RESULT_SUCCESS, "gs_player_poll_diagnostics");

  res = gs_player_destroy(player);
  failures += check(res.code == GS_RESULT_SUCCESS, "gs_player_destroy");

  /* A null out-parameter must be rejected rather than dereferenced. */
  res = gs_player_create(&alloc, NULL);
  failures += check(res.code == GS_RESULT_INVALID_ARGUMENT,
                    "gs_player_create rejects a null out-parameter");

  if (failures != 0) {
    fprintf(stderr, "gs_c_consumer: %d check(s) failed\n", failures);
    return 1;
  }

  printf("gs_c_consumer: C ABI consumable from C99\n");
  return 0;
}
