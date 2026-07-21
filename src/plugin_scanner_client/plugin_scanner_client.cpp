// SPDX-License-Identifier: Apache-2.0

#include <graphscore/plugin_scanner_client/graphscore_plugin_scanner_client.hpp>

namespace graphscore {
namespace {
constexpr int kPluginScannerClientVersion = 1;
}  // namespace

int plugin_scanner_client_version() { return kPluginScannerClientVersion; }
}  // namespace graphscore
