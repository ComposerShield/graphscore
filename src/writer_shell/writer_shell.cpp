// SPDX-License-Identifier: Apache-2.0

#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

namespace graphscore {
namespace {
constexpr int kWriterShellVersion = 1;
}  // namespace

int writer_shell_version() { return kWriterShellVersion; }
}  // namespace graphscore
