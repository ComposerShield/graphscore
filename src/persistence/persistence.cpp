// SPDX-License-Identifier: Apache-2.0

#include <graphscore/persistence/graphscore_persistence.hpp>

namespace graphscore {
namespace {
constexpr int kPersistenceVersion = 1;
}  // namespace

int persistence_version() { return kPersistenceVersion; }
}  // namespace graphscore
