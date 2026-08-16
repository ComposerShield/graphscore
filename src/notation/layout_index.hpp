// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <graphscore/core/rational.hpp>
#include <graphscore/domain/voice_content.hpp>
#include <graphscore/notation/notation_types.hpp>

namespace graphscore {

class MeasureMap;
class Node;
class Project;
struct NotationLayoutWork;

struct SystemFragment {
  SystemLayout                            system;
  std::vector<NotationCommand>            commands;
  std::vector<HitRegion>                  hit_regions;
  std::vector<NotationLayout::Diagnostic> diagnostics;
};

struct IndexedEvent {
  std::size_t      event_index = 0;
  Rational         onset;
  std::size_t      measure = 0;
  NotationEntityId id;
  double           spacing = 0.0;
};

template <typename Record>
struct ReferenceFamily {
  struct Entry {
    Record                          record;
    std::uint64_t                   order_key = 0;
    std::unordered_set<std::size_t> measure_membership;
  };

  std::unordered_map<NotationEntityId, Entry>       entries;
  std::uint64_t                                     next_order_key = 0;
  std::vector<std::unordered_set<NotationEntityId>> by_measure;
};

struct IndexedVoice {
  std::vector<std::vector<IndexedEvent>> measures;
  // Tail events whose onset falls inside the node's pickdown region
  // ([boundary, node_end)), a non-MeasureMap layout region. Their
  // IndexedEvent::measure is the pickdown ordinal: exactly one past the last
  // real measure (== map.measure_count()), so it never names a MeasureMap
  // measure. See pickdown_reference_ids / the pickdown engraving region.
  std::vector<IndexedEvent>                          pickdown;
  std::unordered_map<NotationEntityId, IndexedEvent> by_id;
  ReferenceFamily<DynamicMarking>                    dynamics;
  ReferenceFamily<Hairpin>                           hairpins;
  ReferenceFamily<Slur>                              slurs;
  ReferenceFamily<BeamOverride>                      beam_overrides;
  ReferenceFamily<GraceGroup>                        grace_groups;
  std::unordered_map<NotationEntityId, std::unordered_set<NotationEntityId>>
                                                               reverse_refs;
  std::vector<NotationDiagnostic>                              diagnostics;
  std::vector<std::optional<std::pair<std::size_t, Rational>>> predecessor;
  VoiceRevision                                                last_revision;
  VoiceValidationState                                         validation_state;
};

// The pickdown region's pseudo-measure ordinal: one past the last real
// measure. ReferenceFamily::by_measure and IndexedEvent::measure use it to
// bucket tail material without inventing a MeasureMap entry.
[[nodiscard]] inline std::size_t pickdown_ordinal(std::size_t measure_count) {
  return measure_count;
}

// The ids of a reference family whose membership includes the pickdown
// bucket, in deterministic order-key order -- the pickdown counterpart of
// system_reference_ids().
template <typename Record>
[[nodiscard]] std::vector<NotationEntityId> pickdown_reference_ids(
    const ReferenceFamily<Record>& family, std::size_t measure_count) {
  const std::size_t             ordinal = pickdown_ordinal(measure_count);
  std::vector<NotationEntityId> result;
  if (ordinal >= family.by_measure.size()) {
    return result;
  }
  for (const NotationEntityId& id : family.by_measure[ordinal]) {
    result.push_back(id);
  }
  std::ranges::sort(
      result, [&](const NotationEntityId& a, const NotationEntityId& b) {
        return family.entries.at(a).order_key < family.entries.at(b).order_key;
      });
  return result;
}

struct IndexedStaff {
  StaveId                     stave_id;
  std::array<IndexedVoice, 4> voices;
  ReferenceFamily<PedalSpan>  pedals;
  VoiceRevision               last_pedal_revision;
};

struct LayoutIndex {
  std::vector<IndexedStaff> staves;
};

[[nodiscard]] inline const IndexedStaff* indexed_staff(const LayoutIndex& index,
                                                       StaveId stave_id) {
  const auto found =
      std::ranges::find(index.staves, stave_id, &IndexedStaff::stave_id);
  return found == index.staves.end() ? nullptr : &*found;
}

[[nodiscard]] inline IndexedStaff* indexed_staff(LayoutIndex& index,
                                                 StaveId      stave_id) {
  const auto found =
      std::ranges::find(index.staves, stave_id, &IndexedStaff::stave_id);
  return found == index.staves.end() ? nullptr : &*found;
}

[[nodiscard]] LayoutIndex build_index(const Project&, const Node&,
                                      const MeasureMap&, const GlyphMetrics&,
                                      const NotationLayoutOptions&,
                                      NotationLayoutWork*);
void refresh_index_range(const Project&, const Node&, const MeasureMap&,
                         std::size_t, std::size_t, const GlyphMetrics&,
                         const NotationLayoutOptions&, LayoutIndex&,
                         NotationLayoutWork&);
void append_fragment(NotationLayout&, const SystemFragment&);
[[nodiscard]] NotationLayoutResult layout_internal(
    const Project&, NodeId, const GlyphMetrics&, const NotationLayoutOptions&,
    const LayoutIndex&, const std::vector<double>&,
    const std::vector<SystemFragment>*, const std::vector<std::size_t>&,
    std::vector<SystemFragment>*, NotationLayoutWork*);

}  // namespace graphscore
