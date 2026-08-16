// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>

namespace graphscore {

// Standard notation dynamic marking, ppp through fff. Lives in
// graphscore_core rather than graphscore_domain: it is a project-wide
// default reused by the notation model, but the Phase 7 velocity mapping
// (graphscore/core/playback_mapping.hpp's velocity_for_dynamic()) needs to
// consume it directly from core-layer code, and core cannot depend on
// domain (ADR 0003) — the same reasoning that moved TempoPoint/
// TempoSegmentKind into core in Phase 7a (graphscore/core/tempo_point.hpp).
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

// The single source of truth for "every Dynamic enumerator", so call sites
// that must reason over all of them (e.g. the writer's dynamic palette rows)
// derive from this array instead of maintaining their own copy of the
// enumerator list that could go stale relative to this one. Order is the
// enumerator declaration order above, softest to loudest.
inline constexpr std::array<Dynamic, 8> kAllDynamics = {
    Dynamic::kPpp, Dynamic::kPp, Dynamic::kP,  Dynamic::kMp,
    Dynamic::kMf,  Dynamic::kF,  Dynamic::kFf, Dynamic::kFff,
};

inline constexpr std::uint8_t kDynamicCount =
    static_cast<std::uint8_t>(kAllDynamics.size());

}  // namespace graphscore
