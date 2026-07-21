// SPDX-License-Identifier: Apache-2.0
//
// Minimal C++ consumer of the installed GraphScore Runtime package
// (ADR 0003 §7.8 step 4). It includes only the public C header — the same
// header a Unity or Unreal integration would use — and links only
// graphscore::runtime.

#include <graphscore/c_abi/graphscore_c_abi.h>

#include <cstdio>

int main() {
  const gs_version_t version = gs_query_version();

  if (version.major != GS_VERSION_MAJOR || version.minor != GS_VERSION_MINOR ||
      version.patch != GS_VERSION_PATCH) {
    std::fprintf(stderr,
                 "consumer_cxx: installed runtime reports %u.%u.%u, header "
                 "declares %d.%d.%d\n",
                 version.major, version.minor, version.patch, GS_VERSION_MAJOR,
                 GS_VERSION_MINOR, GS_VERSION_PATCH);
    return 1;
  }

  gs_player_t*      player  = nullptr;
  const gs_result_t created = gs_player_create(nullptr, &player);

  if (created.code != GS_RESULT_SUCCESS || player == nullptr) {
    std::fprintf(stderr, "consumer_cxx: gs_player_create failed (%d)\n",
                 static_cast<int>(created.code));
    return 1;
  }

  const gs_result_t destroyed = gs_player_destroy(player);
  if (destroyed.code != GS_RESULT_SUCCESS) {
    std::fprintf(stderr, "consumer_cxx: gs_player_destroy failed (%d)\n",
                 static_cast<int>(destroyed.code));
    return 1;
  }

  std::printf("consumer_cxx: installed runtime usable from C++\n");
  return 0;
}
