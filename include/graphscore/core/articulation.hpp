// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
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

// The single source of truth for "every Articulation enumerator", so
// call sites that must reason over all of them (e.g.
// graphscore::NotePaletteState::armed_articulations()) derive from this
// array instead of maintaining their own copy of the enumerator list that
// could go stale relative to this one. Order is the enumerator declaration
// order above.
inline constexpr std::array<Articulation, 5> kAllArticulations = {
    Articulation::kAccent,   Articulation::kMarcato,
    Articulation::kStaccato, Articulation::kStaccatissimo,
    Articulation::kTenuto,
};

inline constexpr std::uint8_t kArticulationCount =
    static_cast<std::uint8_t>(kAllArticulations.size());

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
