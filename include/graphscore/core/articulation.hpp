// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace graphscore {

// Presence-only articulation marking attached to a Note or Chord event. A
// note/chord may carry several (e.g. accent + staccato); mapping these to
// MIDI velocity/duration is graphscore/core/playback_mapping.hpp — this
// type only records that the marking exists.
enum class Articulation : std::uint8_t {
  kAccent = 0,
  kMarcato,
  kStaccato,
  kStaccatissimo,
  kTenuto,
};

// Staccato, staccatissimo, and tenuto all shorten or lengthen a note's
// sounded duration and are mutually exclusive on one event; accent and
// marcato affect emphasis and freely combine with any of these three. See
// the referential validator (graphscore/domain/notation_validation.hpp) for
// the check that flags more than one duration articulation on the same
// event, and playback_mapping.hpp for the sounded-duration/velocity
// mappings these markings drive.
[[nodiscard]] constexpr bool is_duration_articulation(
    Articulation articulation) noexcept {
  return articulation == Articulation::kStaccato ||
         articulation == Articulation::kStaccatissimo ||
         articulation == Articulation::kTenuto;
}

}  // namespace graphscore
