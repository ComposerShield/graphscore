// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/measure_map.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_layout.hpp>

#include "engraving.hpp"
#include "layout_index.hpp"
#include "measure_math.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

namespace {

[[nodiscard]] double event_spacing(const VoiceEvent&            event,
                                   const GlyphMetrics&          metrics,
                                   const NotationLayoutOptions& options) {
  const double space = options.staff_space;
  if (const auto* rest = std::get_if<Rest>(&event)) {
    return glyph_extent(metrics,
                        smufl_codepoint(rest_glyph(rest->duration.base())),
                        space) +
           static_cast<double>(rest->duration.dots()) * space * 0.7 + space;
  }
  const auto   event_pitches = pitches(event);
  const double head          = glyph_extent(
      metrics, smufl_codepoint(notehead_glyph(event_duration(event).base())),
      space);
  const double accidental = glyph_extent(
      metrics, smufl_codepoint(SmuflGlyph::kAccidentalDoubleFlat), space);
  return head + accidental * static_cast<double>(event_pitches.size()) +
         static_cast<double>(event_duration(event).dots()) * space * 0.7 +
         space;
}

void assign_span_membership(
    const IndexedVoice& voice, NotationEntityId start_id,
    NotationEntityId end_id, NotationEntityId span_id,
    std::vector<std::unordered_set<NotationEntityId>>& by_measure,
    std::unordered_set<std::size_t>&                   membership_out) {
  const auto start = voice.by_id.find(start_id);
  const auto end   = voice.by_id.find(end_id);
  membership_out.clear();
  if (start == voice.by_id.end() || end == voice.by_id.end()) {
    return;
  }
  const std::size_t first =
      std::min(start->second.measure, end->second.measure);
  const std::size_t last = std::max(start->second.measure, end->second.measure);
  for (std::size_t measure = first; measure <= last; ++measure) {
    by_measure[measure].insert(span_id);
    membership_out.insert(measure);
  }
}

// Helper: assign bucket membership for a single-measure entry.
void assign_single_measure_membership(
    const IndexedVoice& voice, NotationEntityId event_id,
    NotationEntityId                                   ref_id,
    std::vector<std::unordered_set<NotationEntityId>>& by_measure,
    std::unordered_set<std::size_t>&                   membership_out) {
  membership_out.clear();
  const auto ev = voice.by_id.find(event_id);
  if (ev != voice.by_id.end()) {
    by_measure[ev->second.measure].insert(ref_id);
    membership_out.insert(ev->second.measure);
  }
}

void rebuild_voice_reference_index(IndexedVoice& indexed,
                                   std::size_t   measure_count) {
  indexed.dynamics.by_measure.assign(measure_count, {});
  indexed.hairpins.by_measure.assign(measure_count, {});
  indexed.slurs.by_measure.assign(measure_count, {});
  indexed.beam_overrides.by_measure.assign(measure_count, {});
  indexed.grace_groups.by_measure.assign(measure_count, {});
  for (auto& [id, entry] : indexed.dynamics.entries) {
    assign_single_measure_membership(indexed, entry.record.at_event, id,
                                     indexed.dynamics.by_measure,
                                     entry.measure_membership);
  }
  for (auto& [id, entry] : indexed.hairpins.entries) {
    assign_span_membership(
        indexed, entry.record.start_event, entry.record.end_event, id,
        indexed.hairpins.by_measure, entry.measure_membership);
  }
  for (auto& [id, entry] : indexed.slurs.entries) {
    assign_span_membership(indexed, entry.record.start_event,
                           entry.record.end_event, id, indexed.slurs.by_measure,
                           entry.measure_membership);
  }
  for (auto& [id, entry] : indexed.beam_overrides.entries) {
    entry.measure_membership.clear();
    const BeamOverride& beam = entry.record;
    for (std::size_t event = 1; event < beam.events.size(); ++event) {
      std::unordered_set<std::size_t> segment;
      assign_span_membership(indexed, beam.events[event - 1],
                             beam.events[event], beam.id,
                             indexed.beam_overrides.by_measure, segment);
      entry.measure_membership.insert(segment.begin(), segment.end());
    }
  }
  for (auto& [id, entry] : indexed.grace_groups.entries) {
    assign_single_measure_membership(indexed, entry.record.principal_event, id,
                                     indexed.grace_groups.by_measure,
                                     entry.measure_membership);
  }
}

void index_voice(const VoiceContent& content, const MeasureMap& map,
                 const GlyphMetrics&          metrics,
                 const NotationLayoutOptions& options, IndexedVoice& indexed,
                 NotationLayoutWork* work) {
  indexed.measures.assign(map.measure_count(), {});
  indexed.by_id.clear();
  Rational onset;
  for (std::size_t event_index = 0; event_index < content.events().size();
       ++event_index) {
    const VoiceEvent& event = content.events()[event_index];
    if (work != nullptr) {
      ++work->event_visits;
    }
    if (const auto measure = map.measure_index_at(onset)) {
      const IndexedEvent record{event_index, onset, *measure, event_id(event),
                                event_spacing(event, metrics, options)};
      indexed.measures[*measure].push_back(record);
      indexed.by_id.emplace(record.id, record);
    }
    onset = onset + event_duration(event).resolved();
  }
  // Rebuild reference families from source content.
  // Each entry gets a monotonic order key matching source order.
  const auto init_family = [&]<typename T>(ReferenceFamily<T>&   family,
                                           const std::vector<T>& source) {
    family.entries.clear();
    family.next_order_key = 0;
    for (const auto& record : source) {
      family.entries.emplace(record.id,
                             typename ReferenceFamily<T>::Entry{
                                 record, family.next_order_key++, {}});
    }
  };  // NOLINT(readability/braces)
  indexed.reverse_refs.clear();
  init_family(indexed.dynamics, content.dynamics());
  init_family(indexed.hairpins, content.hairpins());
  init_family(indexed.slurs, content.slurs());
  init_family(indexed.beam_overrides, content.beam_overrides());
  init_family(indexed.grace_groups, content.grace_groups());
  // Populate reverse dependency map.
  for (const auto& [id, entry] : indexed.dynamics.entries) {
    indexed.reverse_refs[entry.record.at_event].insert(id);
  }
  for (const auto& [id, entry] : indexed.hairpins.entries) {
    indexed.reverse_refs[entry.record.start_event].insert(id);
    indexed.reverse_refs[entry.record.end_event].insert(id);
  }
  for (const auto& [id, entry] : indexed.slurs.entries) {
    indexed.reverse_refs[entry.record.start_event].insert(id);
    indexed.reverse_refs[entry.record.end_event].insert(id);
  }
  for (const auto& [id, entry] : indexed.beam_overrides.entries) {
    for (const NotationEntityId& eid : entry.record.events) {
      indexed.reverse_refs[eid].insert(id);
    }
  }
  for (const auto& [id, entry] : indexed.grace_groups.entries) {
    indexed.reverse_refs[entry.record.principal_event].insert(id);
  }
  rebuild_voice_reference_index(indexed, map.measure_count());
  indexed.predecessor.resize(map.measure_count());
  std::optional<std::pair<std::size_t, Rational>> carry;
  for (std::size_t measure = 0; measure < map.measure_count(); ++measure) {
    indexed.predecessor[measure] = carry;
    if (!indexed.measures[measure].empty()) {
      const IndexedEvent& back = indexed.measures[measure].back();
      carry.emplace(
          back.event_index + 1,
          back.onset +
              event_duration(content.events()[back.event_index]).resolved());
    }
  }
  if (work != nullptr) {
    work->reference_visits +=
        indexed.dynamics.entries.size() + indexed.hairpins.entries.size() +
        indexed.slurs.entries.size() + indexed.beam_overrides.entries.size() +
        indexed.grace_groups.entries.size();
  }
  indexed.diagnostics   = indexed.validation_state.rebuild(content);
  indexed.last_revision = content.capture_revision();
}

}  // namespace

[[nodiscard]] LayoutIndex build_index(const Project& project, const Node& node,
                                      const MeasureMap&            map,
                                      const GlyphMetrics&          metrics,
                                      const NotationLayoutOptions& options,
                                      NotationLayoutWork*          work) {
  LayoutIndex result;
  for (const Track& track : project.active_tracks()) {
    const TrackLane* lane = node.lane(track.id());
    if (lane == nullptr) {
      continue;
    }
    for (const StaveDefinition& stave : track.layout().staves()) {
      IndexedStaff staff;
      staff.stave_id            = stave.id;
      const StaveVoices* voices = lane->stave(stave.id);
      for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
           ++voice_index) {
        if (voices != nullptr) {
          index_voice(voices->voice(*Voice::create(voice_index)), map, metrics,
                      options, staff.voices[voice_index - Voice::kMin], work);
        } else {
          staff.voices[voice_index - Voice::kMin].measures.resize(
              map.measure_count());
        }
      }
      if (const auto* pedals = lane->pedal_spans(stave.id)) {
        const auto& source = *pedals;
        staff.pedals.entries.clear();
        staff.pedals.next_order_key = 0;
        for (const auto& record : source) {
          staff.pedals.entries.emplace(
              record.id, ReferenceFamily<PedalSpan>::Entry{
                             record, staff.pedals.next_order_key++, {}});
        }
        if (work != nullptr) {
          work->reference_visits += source.size();
        }
      }
      staff.pedals.by_measure.assign(map.measure_count(), {});
      for (auto& [id, entry] : staff.pedals.entries) {
        const PedalSpan& pedal  = entry.record;
        const auto       first  = map.measure_index_at(pedal.start);
        const auto       at_end = map.measure_index_at(pedal.end);
        if (!first.has_value()) {
          continue;
        }
        const std::size_t last = at_end.value_or(map.measure_count() - 1);
        entry.measure_membership.clear();
        for (std::size_t measure = *first; measure <= last; ++measure) {
          staff.pedals.by_measure[measure].insert(id);
          entry.measure_membership.insert(measure);
        }
      }
      staff.last_pedal_revision = lane->capture_revision();
      result.staves.push_back(std::move(staff));
    }
  }
  return result;
}

namespace {

void refresh_voice_range(const VoiceContent& content, const MeasureMap& map,
                         std::size_t first, std::size_t last,
                         const GlyphMetrics&          metrics,
                         const NotationLayoutOptions& options,
                         IndexedVoice& indexed, NotationLayoutWork& work) {
  // Resolve the continuation point from direct predecessor metadata,
  // eliminating the hidden O(measures) backward/forward scans that were
  // previously absent from the work counters.
  const auto&       pred        = indexed.predecessor[first];
  const std::size_t begin_index = pred.has_value() ? pred->first : 0;
  const Rational    begin_onset =
      pred.has_value() ? pred->second : map.measure_start(first);

  const std::size_t old_count = [&] {
    std::size_t count = 0;
    for (std::size_t measure = first; measure <= last; ++measure) {
      count += indexed.measures[measure].size();
    }
    return count;
  }();

  // Defect 3: capture old event measures before clearing, so we can
  // detect events that moved to different measures after rebuild.
  // This is essential for context/time-signature changes where the domain
  // VoiceContent revision is unchanged but measure boundaries shift.
  std::unordered_map<NotationEntityId, std::size_t> old_event_measures;
  for (std::size_t measure = first; measure <= last; ++measure) {
    for (const IndexedEvent& event : indexed.measures[measure]) {
      old_event_measures[event.id] = measure;
    }
  }

  for (std::size_t measure = first; measure <= last; ++measure) {
    for (const IndexedEvent& event : indexed.measures[measure]) {
      indexed.by_id.erase(event.id);
    }
    indexed.measures[measure].clear();
  }
  Rational       onset = begin_onset;
  const Rational end   = map.measure_start(last) + map.measure_length(last);
  std::size_t    event_index = begin_index;
  while (event_index < content.events().size() && onset < end) {
    const VoiceEvent& event = content.events()[event_index];
    ++work.event_visits;
    if (const auto measure = map.measure_index_at(onset);
        measure.has_value() && *measure >= first && *measure <= last) {
      const IndexedEvent record{event_index, onset, *measure, event_id(event),
                                event_spacing(event, metrics, options)};
      indexed.measures[*measure].push_back(record);
      indexed.by_id.insert_or_assign(record.id, record);
    }
    onset = onset + event_duration(event).resolved();
    ++event_index;
  }
  const std::size_t new_count = event_index - begin_index;
  if (new_count != old_count) {
    const auto delta = static_cast<std::ptrdiff_t>(new_count) -
                       static_cast<std::ptrdiff_t>(old_count);
    for (std::size_t measure = last + 1; measure < indexed.measures.size();
         ++measure) {
      for (IndexedEvent& event : indexed.measures[measure]) {
        event.event_index = static_cast<std::size_t>(
            static_cast<std::ptrdiff_t>(event.event_index) + delta);
        indexed.by_id.insert_or_assign(event.id, event);
      }
    }
  }

  // Update predecessor metadata for the refreshed range.  When the event
  // count is unchanged (the common case for local-content and span edits),
  // neither event durations nor indices shift, so the carry out of the last
  // affected measure is identical to the old value and we can stop after
  // confirming one trailing measure.
  //
  // When the count changes, every trailing predecessor must be updated
  // because shifted event indices propagate through both nonempty measures
  // (whose own last-event index changed) and empty measures (which just
  // carry the previous value forward).  The cost is O(trailing measures),
  // not O(trailing events); the far more expensive event-index update loop
  // above already accounts for the dominant O(trailing events) cost.
  std::optional<std::pair<std::size_t, Rational>> carry =
      indexed.predecessor[first];
  const bool        count_unchanged = (new_count == old_count);
  const std::size_t predecessor_end =
      count_unchanged ? std::min(last + 1, indexed.measures.size())
                      : indexed.measures.size();
  for (std::size_t measure = first; measure < predecessor_end; ++measure) {
    indexed.predecessor[measure] = carry;
    if (!indexed.measures[measure].empty()) {
      const IndexedEvent& back = indexed.measures[measure].back();
      carry.emplace(
          back.event_index + 1,
          back.onset +
              event_duration(content.events()[back.event_index]).resolved());
    }
  }

  // ---- Reference sync using domain delta instead of size comparisons ----
  const auto delta_opt = content.delta_since(indexed.last_revision);
  VoiceDelta delta;
  bool       full_refresh_refs = false;
  if (!delta_opt.has_value()) {
    full_refresh_refs = true;
  } else {
    delta = *delta_opt;
  }

  // Defect 3: detect events whose measure assignment changed after the
  // rebuild above.  This handles context/time-signature changes where the
  // domain VoiceContent revision is unchanged but events moved between
  // measures.  Union these with the domain-reported changed_event_ids.
  for (const auto& [id, old_measure] : old_event_measures) {
    const auto new_ev = indexed.by_id.find(id);
    if (new_ev != indexed.by_id.end() &&
        new_ev->second.measure != old_measure) {
      if (std::ranges::find(delta.changed_event_ids, id) ==
          delta.changed_event_ids.end()) {
        delta.changed_event_ids.push_back(id);
      }
    }
  }

  // Defect 3: before we cleared the affected measures above, the old event
  // measure assignments are lost from indexed.by_id.  However, for context
  // changes the delta may report empty changed_event_ids even though events
  // moved between measures.  We therefore capture the per-ID measure before
  // clearing and compare with the rebuild result.
  // The old-event capture must happen before the clear above, so we record
  // it here using the domain source — the domain VoiceEvent vector is
  // unchanged by measure-boundary adjustments, so we can reconstruct old
  // assignments from the delta or from pre-clear state.
  // Compute the union of changed_event_ids and IDs whose measure assignment
  // differs before/after rebuild. This is index maintenance, not a total-work
  // metric for the update.

  if (full_refresh_refs || delta.full_reset || delta.event_reorder) {
    // Stale or reordered — full sync and rebuild.
    const auto init_family = [&]<typename T>(ReferenceFamily<T>&   family,
                                             const std::vector<T>& source) {
      family.entries.clear();
      family.next_order_key = 0;
      for (const auto& record : source) {
        family.entries.emplace(record.id,
                               typename ReferenceFamily<T>::Entry{
                                   record, family.next_order_key++, {}});
      }
    };  // NOLINT(readability/braces)
    indexed.reverse_refs.clear();
    init_family(indexed.dynamics, content.dynamics());
    init_family(indexed.hairpins, content.hairpins());
    init_family(indexed.slurs, content.slurs());
    init_family(indexed.beam_overrides, content.beam_overrides());
    init_family(indexed.grace_groups, content.grace_groups());
    for (const auto& [id, entry] : indexed.dynamics.entries) {
      indexed.reverse_refs[entry.record.at_event].insert(id);
    }
    for (const auto& [id, entry] : indexed.hairpins.entries) {
      indexed.reverse_refs[entry.record.start_event].insert(id);
      indexed.reverse_refs[entry.record.end_event].insert(id);
    }
    for (const auto& [id, entry] : indexed.slurs.entries) {
      indexed.reverse_refs[entry.record.start_event].insert(id);
      indexed.reverse_refs[entry.record.end_event].insert(id);
    }
    for (const auto& [id, entry] : indexed.beam_overrides.entries) {
      for (const NotationEntityId& eid : entry.record.events) {
        indexed.reverse_refs[eid].insert(id);
      }
    }
    for (const auto& [id, entry] : indexed.grace_groups.entries) {
      indexed.reverse_refs[entry.record.principal_event].insert(id);
    }
    const std::size_t ref_total =
        indexed.dynamics.entries.size() + indexed.hairpins.entries.size() +
        indexed.slurs.entries.size() + indexed.beam_overrides.entries.size() +
        indexed.grace_groups.entries.size();
    work.reference_visits += ref_total;
    rebuild_voice_reference_index(indexed, map.measure_count());
    indexed.diagnostics = indexed.validation_state.rebuild(content);
    work.reference_visits += ref_total;
  } else {
    // Apply direct ID-keyed operations with targeted bucket membership and
    // reverse-dependency lookup.

    // Targeted remove: erase only from entry's recorded measure_membership.
    const auto remove_ref = [&]<typename T>(ReferenceFamily<T>& family,
                                            NotationEntityId    id,
                                            const auto& remove_rev_events) {
      auto it = family.entries.find(id);
      if (it == family.entries.end())
        return;
      for (const std::size_t m : it->second.measure_membership) {
        family.by_measure[m].erase(id);
      }
      // Remove from reverse_refs.
      for (const NotationEntityId& ev_id :
           remove_rev_events(it->second.record)) {
        auto rev_it = indexed.reverse_refs.find(ev_id);
        if (rev_it != indexed.reverse_refs.end()) {
          rev_it->second.erase(id);
          if (rev_it->second.empty()) {
            indexed.reverse_refs.erase(rev_it);
          }
        }
      }
      family.entries.erase(it);
    };  // NOLINT(readability/braces)

    // Targeted add: compute membership, insert into buckets, populate rev.
    const auto add_ref = [&]<typename T>(ReferenceFamily<T>& family,
                                         const T&            record,
                                         const auto&         add_rev_events,
                                         const auto& compute_membership_fn) {
      std::unordered_set<std::size_t> membership;
      compute_membership_fn(indexed, family.by_measure, record, membership);
      typename ReferenceFamily<T>::Entry new_entry{
          record, family.next_order_key++, std::move(membership)};
      for (const std::size_t m : new_entry.measure_membership) {
        family.by_measure[m].insert(record.id);
      }
      for (const NotationEntityId& ev_id : add_rev_events(new_entry.record)) {
        indexed.reverse_refs[ev_id].insert(record.id);
      }
      family.entries.emplace(record.id, std::move(new_entry));
    };  // NOLINT(readability/braces)

    // Recompute a span reference's bucket membership from scratch.
    const auto recompute_span = [&](auto& entry, NotationEntityId start_event,
                                    NotationEntityId end_event, auto& family) {
      // Erase from old membership.
      for (const std::size_t m : entry.measure_membership) {
        family.by_measure[m].erase(entry.record.id);
      }
      entry.measure_membership.clear();
      // Recompute.
      assign_span_membership(indexed, start_event, end_event, entry.record.id,
                             family.by_measure, entry.measure_membership);
    };

    // Recompute a single-measure reference's bucket membership.
    const auto recompute_single = [&](auto& entry, NotationEntityId event_id,
                                      auto& family) {
      for (const std::size_t m : entry.measure_membership) {
        family.by_measure[m].erase(entry.record.id);
      }
      entry.measure_membership.clear();
      assign_single_measure_membership(indexed, event_id, entry.record.id,
                                       family.by_measure,
                                       entry.measure_membership);
    };

    // Lambda helpers for each family's reverse-dependency events.
    const auto dyn_rev = [](const DynamicMarking& r) {
      return std::vector<NotationEntityId>{r.at_event};
    };
    const auto hp_rev = [](const Hairpin& r) {
      return std::vector<NotationEntityId>{r.start_event, r.end_event};
    };
    const auto sl_rev = [](const Slur& r) {
      return std::vector<NotationEntityId>{r.start_event, r.end_event};
    };
    const auto beam_rev = [](const BeamOverride& r) {
      return std::vector<NotationEntityId>(r.events.begin(), r.events.end());
    };
    const auto grace_rev = [](const GraceGroup& r) {
      return std::vector<NotationEntityId>{r.principal_event};
    };

    // Helpers for computing membership on add.
    const auto dyn_membership =
        [](const IndexedVoice&                                iv,
           std::vector<std::unordered_set<NotationEntityId>>& bm,
           const DynamicMarking& rec, std::unordered_set<std::size_t>& mem) {
          assign_single_measure_membership(iv, rec.at_event, rec.id, bm, mem);
        };
    const auto hp_membership =
        [](const IndexedVoice&                                iv,
           std::vector<std::unordered_set<NotationEntityId>>& bm,
           const Hairpin& rec, std::unordered_set<std::size_t>& mem) {
          assign_span_membership(iv, rec.start_event, rec.end_event, rec.id, bm,
                                 mem);
        };
    const auto sl_membership =
        [](const IndexedVoice&                                iv,
           std::vector<std::unordered_set<NotationEntityId>>& bm,
           const Slur& rec, std::unordered_set<std::size_t>& mem) {
          assign_span_membership(iv, rec.start_event, rec.end_event, rec.id, bm,
                                 mem);
        };
    const auto beam_membership =
        [](const IndexedVoice&                                iv,
           std::vector<std::unordered_set<NotationEntityId>>& bm,
           const BeamOverride& rec, std::unordered_set<std::size_t>& mem) {
          for (std::size_t event = 1; event < rec.events.size(); ++event) {
            std::unordered_set<std::size_t> segment;
            assign_span_membership(iv, rec.events[event - 1], rec.events[event],
                                   rec.id, bm, segment);
            mem.insert(segment.begin(), segment.end());
          }
        };
    const auto grace_membership =
        [](const IndexedVoice&                                iv,
           std::vector<std::unordered_set<NotationEntityId>>& bm,
           const GraceGroup& rec, std::unordered_set<std::size_t>& mem) {
          assign_single_measure_membership(iv, rec.principal_event, rec.id, bm,
                                           mem);
        };

    // Apply dynamics ops.
    for (const auto& op : delta.dynamic_ops) {
      if (op.kind == RefOpKind::kRemove) {
        remove_ref(indexed.dynamics, op.id, dyn_rev);
      } else if (op.kind == RefOpKind::kAdd) {
        add_ref(indexed.dynamics, op.record, dyn_rev, dyn_membership);
      }
    }
    // Apply hairpin ops.
    for (const auto& op : delta.hairpin_ops) {
      if (op.kind == RefOpKind::kRemove) {
        remove_ref(indexed.hairpins, op.id, hp_rev);
      } else if (op.kind == RefOpKind::kAdd) {
        add_ref(indexed.hairpins, op.record, hp_rev, hp_membership);
      }
    }
    // Apply slur ops.
    for (const auto& op : delta.slur_ops) {
      if (op.kind == RefOpKind::kRemove) {
        remove_ref(indexed.slurs, op.id, sl_rev);
      } else if (op.kind == RefOpKind::kAdd) {
        add_ref(indexed.slurs, op.record, sl_rev, sl_membership);
      }
    }
    // Apply beam override ops.
    for (const auto& op : delta.beam_override_ops) {
      if (op.kind == RefOpKind::kRemove) {
        remove_ref(indexed.beam_overrides, op.id, beam_rev);
      } else if (op.kind == RefOpKind::kAdd) {
        add_ref(indexed.beam_overrides, op.record, beam_rev, beam_membership);
      } else if (op.kind == RefOpKind::kUpdate) {
        // In-place update (VoiceContent::replace_beam_override): replace the
        // retained record while preserving its order key and collection
        // identity, so overlapping order-sensitive join/break overrides keep
        // their precedence. Reverse refs and bucket membership are rebuilt
        // from the new event run.
        auto it = indexed.beam_overrides.entries.find(op.id);
        if (it == indexed.beam_overrides.entries.end())
          continue;
        for (const NotationEntityId& ev_id : beam_rev(it->second.record)) {
          auto rev_it = indexed.reverse_refs.find(ev_id);
          if (rev_it != indexed.reverse_refs.end()) {
            rev_it->second.erase(op.id);
            if (rev_it->second.empty()) {
              indexed.reverse_refs.erase(rev_it);
            }
          }
        }
        for (const std::size_t m : it->second.measure_membership) {
          indexed.beam_overrides.by_measure[m].erase(op.id);
        }
        it->second.record = op.record;
        it->second.measure_membership.clear();
        beam_membership(indexed, indexed.beam_overrides.by_measure, op.record,
                        it->second.measure_membership);
        for (const NotationEntityId& ev_id : beam_rev(it->second.record)) {
          indexed.reverse_refs[ev_id].insert(op.id);
        }
      }
    }
    // Apply grace group ops.
    for (const auto& op : delta.grace_group_ops) {
      if (op.kind == RefOpKind::kRemove) {
        remove_ref(indexed.grace_groups, op.id, grace_rev);
      } else if (op.kind == RefOpKind::kAdd) {
        add_ref(indexed.grace_groups, op.record, grace_rev, grace_membership);
      } else if (op.kind == RefOpKind::kUpdate) {
        // Pitch-only in-place update (VoiceContent::set_notehead_pitch for a
        // grace notehead): replace the retained record while preserving its
        // order key and its principal_event (unchanged by a pitch edit), so
        // a grace-notehead move re-renders at the new pitch without
        // reordering unrelated groups.
        auto it = indexed.grace_groups.entries.find(op.id);
        if (it == indexed.grace_groups.entries.end())
          continue;
        for (const std::size_t m : it->second.measure_membership) {
          indexed.grace_groups.by_measure[m].erase(op.id);
        }
        it->second.measure_membership.clear();
        assign_single_measure_membership(indexed, op.record.principal_event,
                                         op.id, indexed.grace_groups.by_measure,
                                         it->second.measure_membership);
        it->second.record = op.record;
      }
    }

    // When events' measure context changes, reindex only unchanged
    // references that touch those events (direct reverse-dependency lookup).
    // We also union in any event whose IndexedEvent measure assignment
    // changed after the rebuild above (Defect 3: context/time-signature
    // changes where the domain VoiceContent revision is unchanged).
    if (!delta.changed_event_ids.empty()) {
      std::unordered_set<NotationEntityId> refs_to_reindex;
      for (const NotationEntityId& eid : delta.changed_event_ids) {
        const auto rev_it = indexed.reverse_refs.find(eid);
        if (rev_it != indexed.reverse_refs.end()) {
          for (const NotationEntityId& ref_id : rev_it->second) {
            refs_to_reindex.insert(ref_id);
          }
        }
      }
      // For each affected reference: recompute bucket membership from
      // current event measures.
      for (const NotationEntityId& ref_id : refs_to_reindex) {
        if (auto* de =
                [&]() -> decltype(&indexed.dynamics.entries.begin()->second) {
              auto it = indexed.dynamics.entries.find(ref_id);
              return it != indexed.dynamics.entries.end() ? &it->second
                                                          : nullptr;
            }()) {
          recompute_single(*de, de->record.at_event, indexed.dynamics);
        }
        if (auto* he =
                [&]() -> decltype(&indexed.hairpins.entries.begin()->second) {
              auto it = indexed.hairpins.entries.find(ref_id);
              return it != indexed.hairpins.entries.end() ? &it->second
                                                          : nullptr;
            }()) {
          recompute_span(*he, he->record.start_event, he->record.end_event,
                         indexed.hairpins);
        }
        if (auto* se =
                [&]() -> decltype(&indexed.slurs.entries.begin()->second) {
              auto it = indexed.slurs.entries.find(ref_id);
              return it != indexed.slurs.entries.end() ? &it->second : nullptr;
            }()) {
          recompute_span(*se, se->record.start_event, se->record.end_event,
                         indexed.slurs);
        }
        if (auto* be = [&]()
                -> decltype(&indexed.beam_overrides.entries.begin()->second) {
              auto it = indexed.beam_overrides.entries.find(ref_id);
              return it != indexed.beam_overrides.entries.end() ? &it->second
                                                                : nullptr;
            }()) {
          for (const std::size_t m : be->measure_membership) {
            indexed.beam_overrides.by_measure[m].erase(ref_id);
          }
          be->measure_membership.clear();
          for (std::size_t event = 1; event < be->record.events.size();
               ++event) {
            std::unordered_set<std::size_t> segment;
            assign_span_membership(indexed, be->record.events[event - 1],
                                   be->record.events[event], ref_id,
                                   indexed.beam_overrides.by_measure, segment);
            be->measure_membership.insert(segment.begin(), segment.end());
          }
        }
        if (auto* ge = [&]()
                -> decltype(&indexed.grace_groups.entries.begin()->second) {
              auto it = indexed.grace_groups.entries.find(ref_id);
              return it != indexed.grace_groups.entries.end() ? &it->second
                                                              : nullptr;
            }()) {
          recompute_single(*ge, ge->record.principal_event,
                           indexed.grace_groups);
        }
      }
      work.reference_visits += refs_to_reindex.size();
    }

    // Count affected reference visits honestly: only ops processed.
    work.reference_visits += delta.dynamic_ops.size() +
                             delta.hairpin_ops.size() + delta.slur_ops.size() +
                             delta.beam_override_ops.size() +
                             delta.grace_group_ops.size();

    // Validation remains exactly equivalent to complete voice validation and
    // may scan retained content; it is outside engraving-fragment counters.
    const auto apply_result = indexed.validation_state.apply(content, delta);
    indexed.diagnostics     = apply_result.diagnostics;
  }

  indexed.last_revision = content.capture_revision();
}

}  // namespace

void refresh_index_range(const Project& project, const Node& node,
                         const MeasureMap& map, std::size_t first,
                         std::size_t last, const GlyphMetrics& metrics,
                         const NotationLayoutOptions& options,
                         LayoutIndex& index, NotationLayoutWork& work) {
  for (const Track& track : project.active_tracks()) {
    const TrackLane* lane = node.lane(track.id());
    if (lane == nullptr) {
      continue;
    }
    for (const StaveDefinition& stave : track.layout().staves()) {
      IndexedStaff*      staff  = indexed_staff(index, stave.id);
      const StaveVoices* voices = lane->stave(stave.id);
      if (staff == nullptr || voices == nullptr) {
        continue;
      }
      for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
           ++voice_index) {
        refresh_voice_range(voices->voice(*Voice::create(voice_index)), map,
                            first, last, metrics, options,
                            staff->voices[voice_index - Voice::kMin], work);
      }
      // Defect 4: for context-sensitive changes that shift measure boundaries,
      // recompute pedal bucket membership for the affected suffix.  Pedals
      // are positional (Rational start/end), not event-based, so measure
      // remapping must happen even when no pedal delta ops exist.
      if (last == map.measure_count() - 1 && map.measure_count() > 0) {
        for (auto& [id, entry] : staff->pedals.entries) {
          // Erase from old membership.
          for (const std::size_t m : entry.measure_membership) {
            staff->pedals.by_measure[m].erase(id);
          }
          entry.measure_membership.clear();
          // Recompute new membership.
          const PedalSpan& pedal = entry.record;
          const auto       pf    = map.measure_index_at(pedal.start);
          if (!pf.has_value())
            continue;
          const std::size_t pe =
              map.measure_index_at(pedal.end).value_or(map.measure_count() - 1);
          for (std::size_t m = *pf; m <= pe; ++m) {
            staff->pedals.by_measure[m].insert(id);
            entry.measure_membership.insert(m);
          }
          ++work.reference_visits;
        }
      }
      // Use domain delta with stave-qualified operation records.
      const auto pedal_delta_opt =
          lane->pedal_delta_since(staff->last_pedal_revision);
      if (!pedal_delta_opt.has_value()) {
        // Stale token: full sync.
        const auto*                  pedals = lane->pedal_spans(stave.id);
        const std::vector<PedalSpan> empty;
        const auto& source = pedals == nullptr ? empty : *pedals;
        staff->pedals.entries.clear();
        staff->pedals.next_order_key = 0;
        for (const auto& record : source) {
          staff->pedals.entries.emplace(
              record.id, ReferenceFamily<PedalSpan>::Entry{
                             record, staff->pedals.next_order_key++, {}});
        }
        work.reference_visits += source.size();
        staff->pedals.by_measure.assign(map.measure_count(), {});
        for (auto& [id, entry] : staff->pedals.entries) {
          const PedalSpan& pedal       = entry.record;
          const auto       pedal_first = map.measure_index_at(pedal.start);
          if (!pedal_first.has_value())
            continue;
          const std::size_t pedal_end_m =
              map.measure_index_at(pedal.end).value_or(map.measure_count() - 1);
          entry.measure_membership.clear();
          for (std::size_t m = *pedal_first; m <= pedal_end_m; ++m) {
            staff->pedals.by_measure[m].insert(id);
            entry.measure_membership.insert(m);
          }
        }
      } else {
        const auto& ops = *pedal_delta_opt;
        if (!ops.empty()) {
          // Apply pedal delta operations with targeted bucket membership.
          for (const PedalDeltaOp& d_op : ops) {
            if (d_op.stave_id != stave.id)
              continue;
            if (d_op.op.kind == RefOpKind::kRemove) {
              auto it = staff->pedals.entries.find(d_op.op.id);
              if (it != staff->pedals.entries.end()) {
                // Targeted erase: only the recorded measure_membership buckets.
                for (const std::size_t m : it->second.measure_membership) {
                  staff->pedals.by_measure[m].erase(d_op.op.id);
                }
                staff->pedals.entries.erase(it);
              }
            } else if (d_op.op.kind == RefOpKind::kAdd) {
              const auto& record = d_op.op.record;
              const auto  pf     = map.measure_index_at(record.start);
              std::unordered_set<std::size_t> membership;
              if (pf.has_value()) {
                const std::size_t pe = map.measure_index_at(record.end)
                                           .value_or(map.measure_count() - 1);
                for (std::size_t m = *pf; m <= pe; ++m) {
                  staff->pedals.by_measure[m].insert(record.id);
                  membership.insert(m);
                }
              }
              staff->pedals.entries.emplace(
                  record.id, ReferenceFamily<PedalSpan>::Entry{
                                 record, staff->pedals.next_order_key++,
                                 std::move(membership)});
            }
          }
          work.reference_visits += ops.size();
        }
      }
      staff->last_pedal_revision = lane->capture_revision();
    }
  }
}

}  // namespace graphscore
