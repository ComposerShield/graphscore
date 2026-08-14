// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

namespace graphscore::writer_app {

const char* describe(graphscore::ShellError error);

int report(const graphscore::ShellResult& result, bool smoke_test);

}  // namespace graphscore::writer_app
