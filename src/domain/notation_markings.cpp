// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/notation_markings.hpp>

#include <utility>
#include <vector>

namespace graphscore {

DynamicMarking make_dynamic_marking(NotationEntityId at_event, Dynamic value) {
  return DynamicMarking{NotationEntityId::generate(), at_event, value};
}

Hairpin make_hairpin(NotationEntityId start_event, NotationEntityId end_event,
                     HairpinDirection direction) {
  return Hairpin{NotationEntityId::generate(), start_event, end_event,
                 direction};
}

Slur make_slur(NotationEntityId start_event, NotationEntityId end_event) {
  return Slur{NotationEntityId::generate(), start_event, end_event};
}

BeamOverride make_beam_override(BeamOverride::Kind            kind,
                                std::vector<NotationEntityId> events) {
  return BeamOverride{NotationEntityId::generate(), kind, std::move(events)};
}

PedalSpan make_pedal_span(Rational start, Rational end) {
  return PedalSpan{NotationEntityId::generate(), start, end};
}

GraceGroup make_grace_group(NotationEntityId       principal_event,
                            std::vector<GraceNote> notes) {
  return GraceGroup{NotationEntityId::generate(), principal_event,
                    std::move(notes)};
}

}  // namespace graphscore
