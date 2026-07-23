// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace graphscore {

// Per-event manual stem direction override. kAuto defers to automatic
// engraving; this type never decides the automatic direction itself. This
// is an engraving-only concern, not a playback one, so — unlike
// Articulation, relocated to graphscore/core/articulation.hpp — it stays in
// the domain layer.
enum class StemDirection : std::uint8_t {
  kAuto = 0,
  kUp,
  kDown,
};

}  // namespace graphscore
