// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace graphscore {

// Acciaccatura ("crushed" grace note, conventionally slashed) is played as
// close as possible to the preceding beat, stealing the least possible
// time from the preceding sounded note; appoggiatura steals a more
// deliberate, structured share of it. The exact steal fraction/limits are
// graphscore/core/playback_mapping.hpp's grace_steal_durations() concern
// (see graphscore/domain/notation_markings.hpp's GraceGroup for the
// notated attachment/ordering this type is embedded in); this enum only
// records the notated kind.
enum class GraceNoteType : std::uint8_t {
  kAcciaccatura = 0,
  kAppoggiatura,
};

}  // namespace graphscore
