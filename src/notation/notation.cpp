// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

namespace graphscore {
namespace {
constexpr int kNotationVersion = 1;
}  // namespace

int notation_version() { return kNotationVersion; }
}  // namespace graphscore
