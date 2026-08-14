// SPDX-License-Identifier: Apache-2.0
//
// GraphScore Runtime public C ABI. This header is the only interface
// required to consume the graphscore_runtime shared library. It is
// pure C99-compatible and may be included from C and C++ translation
// units. No C++ standard-library types, platform headers, or third-
// party types appear here.

#ifndef GRAPHSCORE_C_ABI_H
#define GRAPHSCORE_C_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef GS_BUILDING_RUNTIME
#define GS_API __declspec(dllexport)
#else
#define GS_API __declspec(dllimport)
#endif
#elif __GNUC__ >= 4 || defined(__clang__)
#define GS_API __attribute__((visibility("default")))
#else
#define GS_API
#endif

/* ------------------------------------------------------------------ */
/*  Numeric types                                                      */
/* ------------------------------------------------------------------ */

typedef uint32_t gs_bool32_t;

/* ------------------------------------------------------------------ */
/*  Version                                                            */
/* ------------------------------------------------------------------ */

#define GS_VERSION_MAJOR 0
#define GS_VERSION_MINOR 1
#define GS_VERSION_PATCH 0

typedef struct gs_version_t {
  uint32_t size;
  uint32_t version;
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
} gs_version_t;

/* ------------------------------------------------------------------ */
/*  Result codes                                                       */
/* ------------------------------------------------------------------ */

typedef enum gs_result_code_t {
  GS_RESULT_SUCCESS           = 0,
  GS_RESULT_INVALID_ARGUMENT  = 1,
  GS_RESULT_OUT_OF_MEMORY     = 2,
  GS_RESULT_VERSION_MISMATCH  = 3,
  GS_RESULT_CORRUPTED_DATA    = 4,
  GS_RESULT_INTERNAL_ERROR    = 5,
  GS_RESULT_CAPACITY_EXCEEDED = 6,
  GS_RESULT_NOT_LOADED        = 7,
} gs_result_code_t;

typedef struct gs_result_t {
  uint32_t         size;
  uint32_t         version;
  gs_result_code_t code;
} gs_result_t;

/* ------------------------------------------------------------------ */
/*  Allocator callbacks                                                */
/* ------------------------------------------------------------------ */

typedef void* (*gs_alloc_fn)(void* context, uint64_t size_bytes,
                             uint64_t alignment);
typedef void (*gs_free_fn)(void* context, void* ptr, uint64_t size_bytes,
                           uint64_t alignment);

typedef struct gs_allocator_t {
  uint32_t    size;
  uint32_t    version;
  void*       context;
  gs_alloc_fn alloc;
  gs_free_fn  free;
} gs_allocator_t;

/* ------------------------------------------------------------------ */
/*  Opaque handles                                                     */
/* ------------------------------------------------------------------ */

typedef struct gs_player_s gs_player_t;
typedef struct gs_asset_s  gs_asset_t;

/* ------------------------------------------------------------------ */
/*  Asset descriptor  (caller-owned output)                            */
/* ------------------------------------------------------------------ */

typedef struct gs_asset_descriptor_t {
  uint32_t       size;
  uint32_t       version;
  const uint8_t* data;
  uint64_t       data_size_bytes;
} gs_asset_descriptor_t;

/* ------------------------------------------------------------------ */
/*  Version query                                                      */
/* ------------------------------------------------------------------ */

GS_API gs_version_t gs_query_version(void);

/* ------------------------------------------------------------------ */
/*  Player lifecycle                                                   */
/* ------------------------------------------------------------------ */

GS_API gs_result_t gs_player_create(const gs_allocator_t* allocator,
                                    gs_player_t**         out_player);

GS_API gs_result_t gs_player_destroy(gs_player_t* player);

GS_API gs_result_t gs_player_load(gs_player_t*                 player,
                                  const gs_asset_descriptor_t* asset);

GS_API gs_result_t gs_player_unload(gs_player_t* player);

/* ------------------------------------------------------------------ */
/*  Transport control                                                  */
/* ------------------------------------------------------------------ */

GS_API gs_result_t gs_player_start_designated(gs_player_t* player);

GS_API gs_result_t gs_player_start_uuid(gs_player_t* player,
                                        const char*  uuid_utf8);

GS_API gs_result_t gs_player_pause(gs_player_t* player);

GS_API gs_result_t gs_player_resume(gs_player_t* player);

GS_API gs_result_t gs_player_stop(gs_player_t* player);

GS_API gs_result_t gs_player_reset(gs_player_t* player);

/* ------------------------------------------------------------------ */
/*  Processing                                                         */
/* ------------------------------------------------------------------ */

typedef struct gs_midi_event_t {
  uint32_t size;
  uint32_t version;
  uint32_t sample_offset;
  uint32_t track_index;
  uint32_t midi_channel;
  uint8_t  data[4];
  uint32_t data_length;
} gs_midi_event_t;

typedef struct gs_input_event_t {
  uint32_t size;
  uint32_t version;
  uint32_t sample_offset;
  char     event_name[64];
} gs_input_event_t;

typedef struct gs_process_params_t {
  uint32_t                size;
  uint32_t                version;
  uint64_t                absolute_sample_position;
  uint32_t                sample_rate;
  uint32_t                frame_count;
  const gs_input_event_t* input_events;
  uint32_t                input_event_count;
  gs_midi_event_t*        output_events;
  uint32_t                output_capacity;
  uint32_t*               output_count;
} gs_process_params_t;

GS_API gs_result_t gs_player_query_capacity(
    const gs_player_t* player, uint32_t max_block_size,
    uint32_t max_input_events, uint32_t* out_required_output_capacity);

GS_API gs_result_t gs_player_process(gs_player_t*         player,
                                     gs_process_params_t* params);

/* ------------------------------------------------------------------ */
/*  Diagnostics                                                        */
/* ------------------------------------------------------------------ */

typedef struct gs_diagnostics_t {
  uint32_t size;
  uint32_t version;
  uint64_t dropped_fifo_events;
  uint64_t undersized_output_retries;
} gs_diagnostics_t;

GS_API gs_result_t gs_player_poll_diagnostics(const gs_player_t* player,
                                              gs_diagnostics_t*  out_diag);

GS_API gs_result_t gs_player_reset_diagnostics(gs_player_t* player);

/* ------------------------------------------------------------------ */
/*  Static assertions (compile-time checks for C consumers)            */
/* ------------------------------------------------------------------ */

/* C99 has no _Static_assert, and this header must compile as C99 (ADR 0003
 * §7.4). The negative-array-size idiom is the portable equivalent: a failing
 * condition yields an array of size -1, which every conforming compiler
 * rejects. The typedef is never instantiated, so this costs nothing. */
#define GS_STATIC_ASSERT(condition, tag) \
  typedef char gs_static_assert_##tag[(condition) ? 1 : -1]

/* Fixed-width members must be exactly as wide as their names claim. A
 * consumer compiled against a different <stdint.h> would otherwise read
 * every sized/versioned struct at the wrong offsets. */
GS_STATIC_ASSERT(sizeof(uint8_t) == 1, uint8_width);
GS_STATIC_ASSERT(sizeof(uint32_t) == 4, uint32_width);
GS_STATIC_ASSERT(sizeof(uint64_t) == 8, uint64_width);
GS_STATIC_ASSERT(sizeof(gs_bool32_t) == 4, bool32_width);

/* Structs holding only fixed-width members have the same size on every
 * supported platform, so their sizes are asserted as literals. */
GS_STATIC_ASSERT(sizeof(gs_version_t) == 20, version_size);
GS_STATIC_ASSERT(sizeof(gs_result_t) == 12, result_size);
GS_STATIC_ASSERT(sizeof(gs_diagnostics_t) == 24, diagnostics_size);
GS_STATIC_ASSERT(sizeof(gs_input_event_t) == 76, input_event_size);

/* The result code enum must have the same width as the fixed-width members
 * it sits beside; a compiler that widened it would shift gs_result_t. */
GS_STATIC_ASSERT(sizeof(gs_result_code_t) == 4, result_code_width);

/* gs_allocator_t and gs_asset_descriptor_t contain pointers, so their sizes
 * are pointer-width dependent by construction. What must hold on every
 * platform is that they contain exactly the declared members with no
 * unexpected padding beyond natural alignment. */
GS_STATIC_ASSERT(sizeof(gs_allocator_t) == 8 + 3 * sizeof(void*),
                 allocator_layout);
GS_STATIC_ASSERT(sizeof(gs_asset_descriptor_t) ==
                     8 + sizeof(const uint8_t*) + sizeof(uint64_t) +
                         (sizeof(void*) == 8 ? 0 : 4),
                 asset_descriptor_layout);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GRAPHSCORE_C_ABI_H
