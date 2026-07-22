// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace graphscore {

// Standard notation dynamic marking, ppp through fff. This lives in the
// domain layer rather than core: it is introduced here as a project-wide
// default, and is expected to be reused as-is by the Phase 4 notation model,
// which already depends on graphscore_domain.
enum class Dynamic : std::uint8_t {
  kPpp = 0,
  kPp,
  kP,
  kMp,
  kMf,
  kF,
  kFf,
  kFff,
};

}  // namespace graphscore
