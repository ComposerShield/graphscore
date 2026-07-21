/* SPDX-License-Identifier: Apache-2.0
 *
 * Minimal C99 consumer of the installed GraphScore Runtime package. Proves
 * the acceptance criterion directly: a host with no C++ at all, holding only
 * the public C header and the shared library, can call the runtime.
 */

#include <graphscore/c_abi/graphscore_c_abi.h>

#include <stddef.h>
#include <stdio.h>

int main(void) {
  gs_version_t version = gs_query_version();
  gs_player_t* player  = NULL;
  gs_result_t  result;

  if (version.major != GS_VERSION_MAJOR || version.minor != GS_VERSION_MINOR ||
      version.patch != GS_VERSION_PATCH) {
    fprintf(stderr, "consumer_c: version mismatch with installed runtime\n");
    return 1;
  }

  result = gs_player_create(NULL, &player);
  if (result.code != GS_RESULT_SUCCESS || player == NULL) {
    fprintf(stderr, "consumer_c: gs_player_create failed\n");
    return 1;
  }

  result = gs_player_destroy(player);
  if (result.code != GS_RESULT_SUCCESS) {
    fprintf(stderr, "consumer_c: gs_player_destroy failed\n");
    return 1;
  }

  printf("consumer_c: installed runtime usable from C99\n");
  return 0;
}
