// SPDX-License-Identifier: Apache-2.0

#include <graphscore/vst3_host/graphscore_vst3_host.hpp>

namespace graphscore {
namespace {
constexpr int kVst3HostVersion = 1;
}  // namespace

int vst3_host_version() { return kVst3HostVersion; }
}  // namespace graphscore
