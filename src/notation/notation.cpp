// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace graphscore {
namespace {

constexpr char32_t kTimeZero = U'\uE080';

// Shared hit-region id suffixes (the "role" half of make_id's "root/role"
// scheme, before add_glyph/add_hit append their own trailing "/hit"). The
// discriminating information for a hit is in HitResult::id, not in
// HitRegion::semantic_id, which several distinct hit sources for the same
// entity intentionally share (see resolve_hit_entity below). Defining each
// suffix once and using the same constant at both the emitting make_id(...)
// call and any place that later inspects the id keeps a future rename of
// one of these regions from silently producing a wrong-but-plausible hit
// id: renaming the literal here changes every call site at once, rather
// than requiring an emitter and a separate ad hoc string literal elsewhere
// to be edited in lockstep. Only the suffixes actually consumed today are
// named; dot/accidental/flag regions stay inline literals (see
// resolve_hit_entity's comment).
//
// kHitRoleArticulation, kHitRoleTie, and kHitRoleTupletDigit are this same
// convention applied to the three kMarking hit families that carry no
// record id of their own (articulation, tie, tuplet number): their
// semantic_id is an event id, the same space kNotehead/kEvent hits already
// search, so resolve_selection_at tells them apart by the shape of hit.id
// itself -- see hit_id_numeric_suffix/hit_id_is_tie_segment below -- rather
// than by semantic_id or by a project-side lookup, which is how the other
// four kMarking kinds (dynamic, hairpin, slur, pedal span) are told apart
// instead.
//
// kHitSuffixNoteheadColumn is named for a third reason: nothing inspects it
// today, but hit_id_ends_with(id, kHitSuffixNotehead) must provably *not*
// match a notehead-column id -- that helper anchors its match at a "/" path
// boundary precisely so a longer segment ending in "notehead" cannot satisfy
// the shorter suffix, and naming both here keeps the two literals visible
// side by side rather than leaving the non-collision to an inline string.
constexpr std::string_view kHitSuffixNotehead       = "notehead";
constexpr std::string_view kHitSuffixNoteheadColumn = "notehead-column";
constexpr std::string_view kHitSuffixGraceNotehead  = "grace-notehead";
constexpr std::string_view kHitSuffixStem           = "stem";
constexpr std::string_view kHitSuffixRest           = "rest";
constexpr std::string_view kHitRoleArticulation     = "articulation";
constexpr std::string_view kHitRoleTie              = "tie";
constexpr std::string_view kHitRoleTupletDigit      = "tuplet/digit";
constexpr std::string_view kHitRoleStaffMeasure     = "staff-measure";

// Every hit-region priority the engraver emits, named so that no bare
// integer reaches a HitRegion::priority anywhere in this file and the whole
// ladder can be read (and re-ordered) in one place. HitRegion::priority in
// graphscore_notation.hpp documents what each rank means and, in
// particular, why kHitPriorityStaffMeasure and kHitPriorityNoteheadColumn
// are each a rank of its own that nothing else may be moved into.
constexpr int kHitPrioritySystem         = 0;
constexpr int kHitPriorityMeasure        = 1;
constexpr int kHitPriorityStaff          = 2;
constexpr int kHitPriorityVoice          = 3;
constexpr int kHitPriorityStaffMeasure   = 4;
constexpr int kHitPriorityNoteheadColumn = 5;
constexpr int kHitPriorityGlyph          = 6;
constexpr int kHitPrioritySpanSegment    = 7;
constexpr int kHitPriorityNotehead       = 8;

[[nodiscard]] NotationId make_id(const std::string& root,
                                 const std::string& role) {
  return NotationId{root + "/" + role};
}

template <typename Tag>
[[nodiscard]] NotationId make_id(const StrongId<Tag>& id,
                                 const std::string&   role) {
  return make_id(id.to_string(), role);
}

// The id shape a staff-measure HitRegion's own semantic_id is built with,
// shared between layout_internal's emission of that region and
// resolve_staff_measure_hit's later recovery of the (staff, measure
// ordinal) pair a HitResult names -- the same "define once, compare by
// exact string equality, never parse" convention kHitRoleArticulation/
// kHitRoleTie/kHitRoleTupletDigit document above. `staff_id` is
// StaffSystemLayout::id (already unique per stave per system); appending
// the measure's own global ordinal makes the combination unique per
// (staff, measure) pair without needing any information hit_test's own
// bounds-based resolution does not already guarantee.
[[nodiscard]] NotationId staff_measure_semantic_id(const NotationId& staff_id,
                                                   std::size_t       ordinal) {
  return make_id(staff_id.value, std::string(kHitRoleStaffMeasure) + "/" +
                                     std::to_string(ordinal));
}

[[nodiscard]] bool finite_rect(const NotationRect& rect) noexcept {
  return std::isfinite(rect.x) && std::isfinite(rect.y) &&
         std::isfinite(rect.width) && std::isfinite(rect.height) &&
         rect.width >= 0.0 && rect.height >= 0.0 &&
         std::isfinite(rect.x + rect.width) &&
         std::isfinite(rect.y + rect.height);
}

[[nodiscard]] bool finite_point(NotationPoint point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] bool bounded_point(NotationPoint point) noexcept {
  return finite_point(point) &&
         std::abs(point.x) <= NotationLayoutOptions::kMaximumCoordinate &&
         std::abs(point.y) <= NotationLayoutOptions::kMaximumCoordinate;
}

[[nodiscard]] bool bounded_rect(const NotationRect& rect) noexcept {
  return finite_rect(rect) && bounded_point({rect.x, rect.y}) &&
         bounded_point({rect.x + rect.width, rect.y + rect.height});
}

[[nodiscard]] bool valid_metric(const GlyphMetricsValue& metric) noexcept {
  return finite_rect(metric.bounds) && std::isfinite(metric.advance) &&
         metric.advance >= 0.0;
}

[[nodiscard]] char32_t clef_glyph(Clef clef) noexcept {
  switch (clef) {
    case Clef::kTreble:
      return smufl_codepoint(SmuflGlyph::kGClef);
    case Clef::kBass:
      return smufl_codepoint(SmuflGlyph::kFClef);
    case Clef::kAlto:
    case Clef::kTenor:
      return smufl_codepoint(SmuflGlyph::kCClef);
  }
}

[[nodiscard]] NotationRect translated(const NotationRect& rect,
                                      NotationPoint       origin) noexcept {
  return NotationRect{origin.x + rect.x, origin.y + rect.y, rect.width,
                      rect.height};
}

struct LayoutBuilder {
  const NodeTimeline&          timeline;
  const GlyphMetrics&          metrics;
  const NotationLayoutOptions& options;
  NotationLayout               output;
  NotationLayoutError          error = NotationLayoutError::kNone;

  [[nodiscard]] std::optional<double> add_glyph(
      const NotationId& id, char32_t code_point, NotationPoint origin,
      std::optional<NotationId> semantic_id = std::nullopt,
      double                    scale       = 1.0) {
    const double            glyph_space = options.staff_space * scale;
    const GlyphMetricsValue glyph =
        metrics.glyph_metrics(code_point, glyph_space);
    if (!valid_metric(glyph)) {
      error = NotationLayoutError::kInvalidMetrics;
      return std::nullopt;
    }
    const NotationRect hit_bounds = translated(glyph.bounds, origin);
    if (!finite_point(origin) || !finite_rect(hit_bounds)) {
      error = NotationLayoutError::kInvalidGeometry;
      return std::nullopt;
    }
    output.commands.emplace_back(
        GlyphCommand{id, code_point, origin, glyph_space});
    if (semantic_id.has_value()) {
      // kHitPriorityGlyph: see HitRegion::priority for the whole ladder.
      // Callers that want a different role or priority overwrite
      // hit_regions.back() immediately after this returns.
      output.hit_regions.push_back(HitRegion{make_id(id.value, "hit"),
                                             *semantic_id, HitRole::kEvent,
                                             hit_bounds, kHitPriorityGlyph});
    }
    return glyph.advance;
  }

  void add_line(NotationId id, NotationPoint from, NotationPoint to,
                double width) {
    output.commands.emplace_back(LineCommand{std::move(id), from, to, width});
  }

  void add_path(NotationId id, std::vector<PathElement> elements, double width,
                bool filled = false) {
    output.commands.emplace_back(
        PathCommand{std::move(id), std::move(elements), width, filled});
  }

  void add_hit(const NotationId& id, const NotationId& semantic_id,
               HitRole role, NotationRect bounds,
               int priority = kHitPriorityGlyph) {
    output.hit_regions.push_back(HitRegion{
        make_id(id.value, "hit"), semantic_id, role, bounds, priority});
  }
};

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

// Stable ID-keyed reference-family storage.  Each entry carries a
// monotonic order key (never reused) for deterministic enumeration, and
// the exact set of measure indices where it currently resides so that
// targeted erase touches only those buckets — no scan over by_measure.
// System collection deduplicates IDs then sorts by retained order key.
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
  std::vector<std::vector<IndexedEvent>>             measures;
  std::unordered_map<NotationEntityId, IndexedEvent> by_id;
  ReferenceFamily<DynamicMarking>                    dynamics;
  ReferenceFamily<Hairpin>                           hairpins;
  ReferenceFamily<Slur>                              slurs;
  ReferenceFamily<BeamOverride>                      beam_overrides;
  ReferenceFamily<GraceGroup>                        grace_groups;
  // Reverse dependency: event_id → set of reference IDs that point at it.
  // Maintained during full build and every add/remove/reindex operation.
  std::unordered_map<NotationEntityId, std::unordered_set<NotationEntityId>>
                                  reverse_refs;
  std::vector<NotationDiagnostic> diagnostics;
  // predecessor[m] = (next_event_index, next_onset) for the continuation
  // point after the last event before measure m.  nullopt when no events
  // precede m, in which case indexing restarts from the absolute beginning
  // or measure_start(first).
  std::vector<std::optional<std::pair<std::size_t, Rational>>> predecessor;
  // Domain revision tracking for incremental refresh.
  VoiceRevision        last_revision;
  VoiceValidationState validation_state;
};

struct IndexedStaff {
  StaveId                     stave_id;
  std::array<IndexedVoice, 4> voices;
  ReferenceFamily<PedalSpan>  pedals;
  VoiceRevision               last_pedal_revision;
};

struct LayoutIndex {
  std::vector<IndexedStaff> staves;
};

[[nodiscard]] SmuflGlyph notehead_glyph(NoteValue value) noexcept;
[[nodiscard]] SmuflGlyph rest_glyph(NoteValue value) noexcept;
[[nodiscard]] std::vector<std::pair<NotationEntityId, SpelledPitch>> pitches(
    const VoiceEvent& event);

[[nodiscard]] const IndexedStaff* indexed_staff(const LayoutIndex& index,
                                                StaveId            stave_id) {
  const auto found =
      std::ranges::find(index.staves, stave_id, &IndexedStaff::stave_id);
  return found == index.staves.end() ? nullptr : &*found;
}

[[nodiscard]] IndexedStaff* indexed_staff(LayoutIndex& index,
                                          StaveId      stave_id) {
  const auto found =
      std::ranges::find(index.staves, stave_id, &IndexedStaff::stave_id);
  return found == index.staves.end() ? nullptr : &*found;
}

[[nodiscard]] double glyph_extent(const GlyphMetrics& metrics, char32_t glyph,
                                  double staff_space) {
  const GlyphMetricsValue value = metrics.glyph_metrics(glyph, staff_space);
  return std::max(value.advance, value.bounds.x + value.bounds.width) -
         std::min(0.0, value.bounds.x);
}

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

// Helper: assign bucket membership for a span and record it in the entry.
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
      }
    }
    // Apply grace group ops.
    for (const auto& op : delta.grace_group_ops) {
      if (op.kind == RefOpKind::kRemove) {
        remove_ref(indexed.grace_groups, op.id, grace_rev);
      } else if (op.kind == RefOpKind::kAdd) {
        add_ref(indexed.grace_groups, op.record, grace_rev, grace_membership);
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

void append_fragment(NotationLayout& output, const SystemFragment& fragment) {
  output.systems.push_back(fragment.system);
  output.commands.insert(output.commands.end(), fragment.commands.begin(),
                         fragment.commands.end());
  output.hit_regions.insert(output.hit_regions.end(),
                            fragment.hit_regions.begin(),
                            fragment.hit_regions.end());
  output.diagnostics.insert(output.diagnostics.end(),
                            fragment.diagnostics.begin(),
                            fragment.diagnostics.end());
}

[[nodiscard]] double compute_measure_width(
    std::size_t index, const MeasureMap& measures,
    const LayoutIndex& layout_index, const GlyphMetrics& metrics,
    const NotationLayoutOptions& options) {
  double rhythmic_width = 0.0;
  for (const IndexedStaff& staff : layout_index.staves) {
    for (const IndexedVoice& voice : staff.voices) {
      double voice_width = 0.0;
      for (const IndexedEvent& event : voice.measures[index]) {
        voice_width += event.spacing;
      }
      rhythmic_width = std::max(rhythmic_width, voice_width);
    }
  }
  const Measure& measure = measures.measure(index);
  const double   clef_width =
      std::max({glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kGClef),
                             options.staff_space),
                glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kCClef),
                             options.staff_space),
                glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kFClef),
                             options.staff_space),
                options.staff_space * 3.0});
  const double accidental_width = std::max(
      glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kAccidentalSharp),
                   options.staff_space),
      glyph_extent(metrics, smufl_codepoint(SmuflGlyph::kAccidentalFlat),
                   options.staff_space));
  const auto digits_width = [&](std::uint16_t value) {
    double result = 0.0;
    for (const char digit : std::to_string(value)) {
      result +=
          glyph_extent(metrics, kTimeZero + static_cast<char32_t>(digit - '0'),
                       options.staff_space);
    }
    return result;
  };
  const double signature_width =
      options.staff_space + clef_width +
      accidental_width *
          (std::abs(static_cast<int>(measure.key_signature.fifths())) +
           (index > 0 && measure.key_signature !=
                             measures.measure(index - 1).key_signature
                ? std::abs(static_cast<int>(
                      measures.measure(index - 1).key_signature.fifths()))
                : 0)) +
      std::max(digits_width(measure.time_signature.numerator()),
               digits_width(measure.time_signature.denominator())) +
      options.staff_space;
  return std::max(
      {options.minimum_measure_width,
       options.whole_note_spacing * measures.measure_length(index).to_double(),
       signature_width + rhythmic_width + options.staff_space * 2.0});
}

[[nodiscard]] std::vector<double> measure_widths(
    const MeasureMap& measures, const LayoutIndex& layout_index,
    const GlyphMetrics& metrics, const NotationLayoutOptions& options) {
  std::vector<double> widths;
  widths.reserve(measures.measure_count());
  for (std::size_t index = 0; index < measures.measure_count(); ++index) {
    widths.push_back(
        compute_measure_width(index, measures, layout_index, metrics, options));
  }
  return widths;
}

[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> system_ranges(
    const std::vector<double>& widths, double content_width) {
  std::vector<std::pair<std::size_t, std::size_t>> ranges;
  std::size_t                                      first = 0;
  while (first < widths.size()) {
    std::size_t end  = first;
    double      used = 0.0;
    while (end < widths.size() &&
           (end == first || used + widths[end] <= content_width)) {
      used += widths[end];
      ++end;
    }
    ranges.emplace_back(first, end);
    first = end;
  }
  return ranges;
}

[[nodiscard]] double event_y(const Voice& voice, double staff_top,
                             double staff_space) noexcept {
  constexpr double kVoiceOffsets[] = {2.0, 1.5, 2.5, 1.0};
  return staff_top + kVoiceOffsets[voice.index() - Voice::kMin] * staff_space;
}

// The horizontal space `position_x`/`time_at_x` reserve at the head of a
// measure for its clef/key/time-signature glyphs, before the rhythmic span
// begins. Shared so the pointer-entry preview's x<->time mapping stays
// exactly reproducible from the layout it reads, never a separate
// approximation of it.
[[nodiscard]] double measure_leading_width(const MeasureMap& measures,
                                           std::size_t       measure_index,
                                           double            measure_width,
                                           double staff_space) noexcept {
  const Measure& measure       = measures.measure(measure_index);
  const double   digit_columns = static_cast<double>(
      std::max(std::to_string(measure.time_signature.numerator()).size(),
                 std::to_string(measure.time_signature.denominator()).size()));
  return std::min(
      measure_width - staff_space * 2.0,
      staff_space *
          (6.5 +
           1.5 * (std::abs(static_cast<int>(measure.key_signature.fifths())) +
                  (measure_index > 0 &&
                           measure.key_signature !=
                               measures.measure(measure_index - 1).key_signature
                       ? std::abs(static_cast<int>(
                             measures.measure(measure_index - 1)
                                 .key_signature.fifths()))
                       : 0)) +
           1.5 * digit_columns));
}

[[nodiscard]] double position_x(const MeasureMap& measures,
                                std::size_t measure_index, double measure_width,
                                Rational position, double measure_x,
                                double staff_space) noexcept {
  const Rational within = position - measures.measure_start(measure_index);
  const double   fraction =
      within.to_double() / measures.measure_length(measure_index).to_double();
  const double leading = measure_leading_width(measures, measure_index,
                                               measure_width, staff_space);
  const double rhythmic_width =
      std::max(staff_space, measure_width - leading - staff_space);
  return measure_x + leading + rhythmic_width * fraction;
}

[[nodiscard]] double position_x(const MeasureMap&          measures,
                                const std::vector<double>& widths,
                                std::size_t measure_index, Rational position,
                                double measure_x, double staff_space) noexcept {
  return position_x(measures, measure_index, widths[measure_index], position,
                    measure_x, staff_space);
}

// The exact inverse of position_x(): the musical time at horizontal position
// `x` within the measure at `measure_index`, clamped to the measure's own
// rhythmic span (never before/after it, matching position_x's own domain).
[[nodiscard]] double time_at_x(const MeasureMap& measures,
                               std::size_t measure_index, double x,
                               double measure_x, double measure_width,
                               double staff_space) noexcept {
  const double leading = measure_leading_width(measures, measure_index,
                                               measure_width, staff_space);
  const double rhythmic_width =
      std::max(staff_space, measure_width - leading - staff_space);
  const double fraction =
      rhythmic_width > 0.0
          ? std::clamp((x - measure_x - leading) / rhythmic_width, 0.0, 1.0)
          : 0.0;
  return measures.measure_start(measure_index).to_double() +
         fraction * measures.measure_length(measure_index).to_double();
}

[[nodiscard]] bool add_signature_glyphs(
    LayoutBuilder& builder, const Measure& measure, Clef clef,
    const NotationId& staff_id, NotationPoint origin, std::size_t ordinal,
    bool emit_clef, bool emit_key, bool emit_time,
    std::optional<KeySignature> cancelled_key = std::nullopt) {
  const std::string ordinal_text = std::to_string(ordinal);
  double            x            = origin.x + builder.options.staff_space;
  char32_t          previous     = U'\0';
  const auto        place = [&](const NotationId& id, char32_t glyph, double y,
                         double minimum_advance) {
    if (previous != U'\0') {
      const double kerning =
          builder.metrics.kerning(previous, glyph, builder.options.staff_space);
      if (!std::isfinite(kerning)) {
        builder.error = NotationLayoutError::kInvalidMetrics;
        return false;
      }
      x += kerning;
    }
    const std::optional<double> advance =
        builder.add_glyph(id, glyph, NotationPoint{x, y});
    if (!advance.has_value()) {
      return false;
    }
    x += std::max(*advance, minimum_advance);
    previous = glyph;
    return true;
  };
  if (emit_clef &&
      !place(make_id(staff_id.value, "measure/" + ordinal_text + "/clef"),
             clef_glyph(clef), origin.y, builder.options.staff_space * 3.0)) {
    return false;
  }
  const std::uint8_t encoded_fifths =
      static_cast<std::uint8_t>(measure.key_signature.fifths());
  const int           fifths = encoded_fifths <= 7
                                   ? static_cast<int>(encoded_fifths)
                                   : static_cast<int>(encoded_fifths) - 256;
  const std::uint16_t fifth_count =
      static_cast<std::uint16_t>(fifths < 0 ? -fifths : fifths);
  constexpr std::array<std::array<double, 7>, 4> kSharpPositions = {{
      {0.0, 1.5, -0.5, 1.0, 2.5, 0.5, 2.0},
      {1.0, 2.5, 0.5, 2.0, 3.5, 1.5, 3.0},
      {1.5, 3.0, 1.0, 2.5, 4.0, 2.0, 3.5},
      {0.5, 2.0, 0.0, 1.5, 3.0, 1.0, 2.5},
  }};
  constexpr std::array<std::array<double, 7>, 4> kFlatPositions  = {{
      {2.0, 0.5, 2.5, 1.0, 3.0, 1.5, 3.5},
      {3.0, 1.5, 3.5, 2.0, 4.0, 2.5, 4.5},
      {3.5, 2.0, 4.0, 2.5, 4.5, 3.0, 5.0},
      {2.5, 1.0, 3.0, 1.5, 3.5, 2.0, 4.0},
  }};
  const auto clef_index = static_cast<std::size_t>(clef);
  if (cancelled_key.has_value()) {
    const int old_fifths = [&] {
      const std::uint8_t encoded =
          static_cast<std::uint8_t>(cancelled_key->fifths());
      return encoded <= 7 ? static_cast<int>(encoded)
                          : static_cast<int>(encoded) - 256;
    }();
    const std::size_t old_count =
        static_cast<std::size_t>(std::abs(old_fifths));
    for (std::size_t index = 0; index < old_count; ++index) {
      if (!place(make_id(staff_id.value, "measure/" + ordinal_text +
                                             "/key-cancel/" +
                                             std::to_string(index)),
                 smufl_codepoint(SmuflGlyph::kAccidentalNatural),
                 origin.y + builder.options.staff_space *
                                (old_fifths < 0
                                     ? kFlatPositions[clef_index][index]
                                     : kSharpPositions[clef_index][index]),
                 builder.options.staff_space)) {
        return false;
      }
    }
  }
  for (std::uint16_t index = 0; emit_key && index < fifth_count; ++index) {
    const char32_t glyph = fifths < 0
                               ? smufl_codepoint(SmuflGlyph::kAccidentalFlat)
                               : smufl_codepoint(SmuflGlyph::kAccidentalSharp);
    if (!place(make_id(staff_id.value, "measure/" + ordinal_text + "/key/" +
                                           std::to_string(index)),
               glyph,
               origin.y + builder.options.staff_space *
                              (fifths < 0 ? kFlatPositions[clef_index][index]
                                          : kSharpPositions[clef_index][index]),
               builder.options.staff_space)) {
      return false;
    }
  }
  const auto add_number = [&](std::uint16_t number, const std::string& role,
                              double y) {
    const std::string digits = std::to_string(number);
    for (std::size_t index = 0; index < digits.size(); ++index) {
      const auto  digit      = static_cast<char32_t>(digits[index] - '0');
      std::string glyph_role = "measure/" + ordinal_text;
      glyph_role.append("/time/").append(role).append("/").append(
          std::to_string(index));
      if (!place(make_id(staff_id.value, glyph_role), kTimeZero + digit, y,
                 builder.options.staff_space)) {
        return false;
      }
    }
    return true;
  };
  if (!emit_time) {
    return true;
  }
  const double   time_x                = x;
  const char32_t signature_predecessor = previous;
  if (!add_number(measure.time_signature.numerator(), "numerator", origin.y)) {
    return false;
  }
  x        = time_x;
  previous = signature_predecessor;
  return add_number(measure.time_signature.denominator(), "denominator",
                    origin.y + builder.options.staff_space * 2.0);
}

[[nodiscard]] int diatonic_index(const SpelledPitch& pitch) noexcept {
  constexpr std::array<int, 7> kFromC = {5, 6, 0, 1, 2, 3, 4};
  return (static_cast<int>(pitch.octave()) + 1) * 7 +
         kFromC[static_cast<std::size_t>(pitch.letter())];
}

[[nodiscard]] int clef_middle_line(Clef clef) noexcept {
  switch (clef) {
    case Clef::kTreble:
      return 41;  // B4
    case Clef::kBass:
      return 29;  // D3
    case Clef::kAlto:
      return 35;  // C4
    case Clef::kTenor:
      return 33;  // A3
  }
}

[[nodiscard]] double pitch_y(const SpelledPitch& pitch, Clef clef, double top,
                             double space) noexcept {
  return top +
         static_cast<double>(clef_middle_line(clef) - diatonic_index(pitch)) *
             space * 0.5 +
         space * 2.0;
}

// The exact inverse of pitch_y(): the natural diatonic staff step nearest
// `y`, spelled with Accidental::kNatural (a staff position selects a step,
// never an accidental). Returns std::nullopt rather than a clamped value
// when the nearest step's octave falls outside SpelledPitch's valid
// [kMinOctave, kMaxOctave] range, when `space` is not strictly positive, or
// when any input is non-finite.
[[nodiscard]] std::optional<SpelledPitch> spelled_pitch_at(
    double y, Clef clef, double top, double space) noexcept {
  if (!(space > 0.0) || !std::isfinite(y) || !std::isfinite(top)) {
    return std::nullopt;
  }
  const double raw = static_cast<double>(clef_middle_line(clef)) -
                     (y - top - space * 2.0) * 2.0 / space;
  if (!std::isfinite(raw) || std::abs(raw) > 1e6) {
    return std::nullopt;
  }
  const std::int64_t step         = std::llround(raw);
  std::int64_t       octave_plus1 = step / 7;
  std::int64_t       letter_index = step % 7;
  if (letter_index < 0) {
    --octave_plus1;
    letter_index += 7;
  }
  const std::int64_t octave = octave_plus1 - 1;
  if (octave < SpelledPitch::kMinOctave || octave > SpelledPitch::kMaxOctave) {
    return std::nullopt;
  }
  constexpr std::array<Letter, 7> kLettersFromC = {
      Letter::kC, Letter::kD, Letter::kE, Letter::kF,
      Letter::kG, Letter::kA, Letter::kB};
  return SpelledPitch::create(
      kLettersFromC[static_cast<std::size_t>(letter_index)],
      static_cast<std::int8_t>(octave), Accidental::kNatural);
}

[[nodiscard]] SmuflGlyph notehead_glyph(NoteValue value) noexcept {
  if (value == NoteValue::kWhole) {
    return SmuflGlyph::kNoteheadWhole;
  }
  if (value == NoteValue::kHalf) {
    return SmuflGlyph::kNoteheadHalf;
  }
  return SmuflGlyph::kNoteheadBlack;
}

[[nodiscard]] SmuflGlyph rest_glyph(NoteValue value) noexcept {
  constexpr std::array<SmuflGlyph, 7> kRests = {
      SmuflGlyph::kRestWhole,  SmuflGlyph::kRestHalf, SmuflGlyph::kRestQuarter,
      SmuflGlyph::kRestEighth, SmuflGlyph::kRest16th, SmuflGlyph::kRest32nd,
      SmuflGlyph::kRest64th};
  return kRests[static_cast<std::size_t>(value)];
}

[[nodiscard]] SmuflGlyph accidental_glyph(Accidental accidental) noexcept {
  constexpr std::array<SmuflGlyph, 5> kAccidentals = {
      SmuflGlyph::kAccidentalDoubleFlat, SmuflGlyph::kAccidentalFlat,
      SmuflGlyph::kAccidentalNatural, SmuflGlyph::kAccidentalSharp,
      SmuflGlyph::kAccidentalDoubleSharp};
  return kAccidentals[static_cast<std::size_t>(
      static_cast<int>(accidental) -
      static_cast<int>(Accidental::kDoubleFlat))];
}

[[nodiscard]] Accidental key_accidental(const KeySignature& key,
                                        Letter              letter) noexcept {
  constexpr std::array<Letter, 7> kSharps = {Letter::kF, Letter::kC, Letter::kG,
                                             Letter::kD, Letter::kA, Letter::kE,
                                             Letter::kB};
  constexpr std::array<Letter, 7> kFlats  = {Letter::kB, Letter::kE, Letter::kA,
                                             Letter::kD, Letter::kG, Letter::kC,
                                             Letter::kF};
  const std::uint8_t              encoded_fifths_key =
      static_cast<std::uint8_t>(key.fifths());
  const int fifths = encoded_fifths_key <= 7
                         ? static_cast<int>(encoded_fifths_key)
                         : static_cast<int>(encoded_fifths_key) - 256;
  if (fifths > 0 && std::ranges::find(kSharps.begin(), kSharps.begin() + fifths,
                                      letter) != kSharps.begin() + fifths) {
    return Accidental::kSharp;
  }
  if (fifths < 0 && std::ranges::find(kFlats.begin(), kFlats.begin() - fifths,
                                      letter) != kFlats.begin() - fifths) {
    return Accidental::kFlat;
  }
  return Accidental::kNatural;
}

[[nodiscard]] bool stem_up_for(Voice voice, StemDirection override) noexcept {
  if (override != StemDirection::kAuto) {
    return override == StemDirection::kUp;
  }
  // Voices 1/3 are the upper pair and 2/4 the lower pair. This remains
  // semantic policy even when a crossing voice happens to sit on the other
  // side of the middle line; explicit domain overrides always win.
  return voice.index() == 1 || voice.index() == 3;
}

struct EventPlacement {
  const VoiceEvent* event = nullptr;
  Voice             voice;
  Rational          onset;
  std::size_t       measure  = 0;
  double            x        = 0.0;
  double            anchor_y = 0.0;
  bool              stem_up  = true;
};

[[nodiscard]] double event_anchor_y(const VoiceEvent& event, Voice voice,
                                    Clef clef, double staff_top, double space) {
  if (const auto* note = std::get_if<Note>(&event)) {
    return pitch_y(note->pitch, clef, staff_top, space);
  }
  if (const auto* chord = std::get_if<Chord>(&event);
      chord != nullptr && !chord->notes.empty()) {
    const bool up = stem_up_for(voice, chord->stem);
    double result = pitch_y(chord->notes.front().pitch, clef, staff_top, space);
    for (const ChordNote& chord_note : chord->notes) {
      const double y = pitch_y(chord_note.pitch, clef, staff_top, space);
      result         = up ? std::min(result, y) : std::max(result, y);
    }
    return result;
  }
  return event_y(voice, staff_top, space);
}

[[nodiscard]] std::vector<EventPlacement> placements_for_system(
    const NodeTimeline& timeline, StaveId stave_id, const StaveVoices& voices,
    const IndexedStaff& indexed, const StaffSystemLayout& staff,
    const std::vector<double>&        widths,
    const std::vector<MeasureLayout>& measures) {
  std::vector<EventPlacement> placements;
  const MeasureMap&           measure_map = timeline.measures();
  const ClefLane*             clefs       = timeline.clef_lane(stave_id);
  for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
    const auto voice_value = Voice::create(index);
    if (!voice_value.has_value()) {
      continue;
    }
    const Voice voice  = *voice_value;
    const auto& events = voices.voice(voice).events();
    for (std::size_t measure = measures.front().ordinal;
         measure <= measures.back().ordinal; ++measure) {
      for (const IndexedEvent& record :
           indexed.voices[index - Voice::kMin].measures[measure]) {
        const VoiceEvent& event = events[record.event_index];
        const auto        local = measure - measures.front().ordinal;
        const Clef        clef =
            clefs == nullptr ? Clef::kTreble : clefs->clef_at(record.onset);
        const double staff_space = staff.bounds.height / 4.0;
        const double y =
            event_anchor_y(event, voice, clef, staff.bounds.y, staff_space);
        placements.push_back(EventPlacement{
            &event, voice, record.onset, measure,
            position_x(measure_map, widths, measure, record.onset,
                       measures[local].bounds.x, staff_space),
            y, stem_up_for(voice, event_stem(event))});
      }
    }
  }
  std::ranges::sort(placements, [](const auto& left, const auto& right) {
    if (left.onset != right.onset) {
      return left.onset < right.onset;
    }
    return left.voice.index() < right.voice.index();
  });
  return placements;
}

[[nodiscard]] std::vector<std::pair<NotationEntityId, SpelledPitch>> pitches(
    const VoiceEvent& event) {
  std::vector<std::pair<NotationEntityId, SpelledPitch>> result;
  if (const auto* note = std::get_if<Note>(&event)) {
    result.emplace_back(note->id, note->pitch);
  } else if (const auto* chord = std::get_if<Chord>(&event)) {
    result.reserve(chord->notes.size());
    for (const ChordNote& chord_note : chord->notes) {
      result.emplace_back(chord_note.id, chord_note.pitch);
    }
  }
  std::ranges::sort(result, [](const auto& left, const auto& right) {
    const int left_index  = diatonic_index(left.second);
    const int right_index = diatonic_index(right.second);
    return left_index == right_index
               ? left.first.to_string() < right.first.to_string()
               : left_index < right_index;
  });
  return result;
}

void add_ledger_lines(LayoutBuilder& builder, const NotationEntityId& id,
                      double x, double y, double staff_top) {
  const double space   = builder.options.staff_space;
  int          ordinal = 0;
  for (double line_y = staff_top - space; y <= line_y + space * 0.25;
       line_y -= space) {
    builder.add_line(make_id(id, "ledger/above/" + std::to_string(ordinal++)),
                     {x - space * 0.85, line_y}, {x + space * 0.85, line_y},
                     space * 0.12);
  }
  ordinal = 0;
  for (double line_y = staff_top + space * 5.0; y >= line_y - space * 0.25;
       line_y += space) {
    builder.add_line(make_id(id, "ledger/below/" + std::to_string(ordinal++)),
                     {x - space * 0.85, line_y}, {x + space * 0.85, line_y},
                     space * 0.12);
  }
}

[[nodiscard]] SmuflGlyph articulation_glyph(Articulation articulation) {
  switch (articulation) {
    case Articulation::kAccent:
      return SmuflGlyph::kArticAccentAbove;
    case Articulation::kMarcato:
      return SmuflGlyph::kArticMarcatoAbove;
    case Articulation::kStaccato:
      return SmuflGlyph::kArticStaccatoAbove;
    case Articulation::kStaccatissimo:
      return SmuflGlyph::kArticStaccatissimoAbove;
    case Articulation::kTenuto:
      return SmuflGlyph::kArticTenutoAbove;
  }
}

[[nodiscard]] std::vector<SmuflGlyph> dynamic_glyphs(Dynamic dynamic) {
  using enum SmuflGlyph;
  switch (dynamic) {
    case Dynamic::kPpp:
      return {kDynamicP, kDynamicP, kDynamicP};
    case Dynamic::kPp:
      return {kDynamicP, kDynamicP};
    case Dynamic::kP:
      return {kDynamicP};
    case Dynamic::kMp:
      return {kDynamicM, kDynamicP};
    case Dynamic::kMf:
      return {kDynamicM, kDynamicF};
    case Dynamic::kF:
      return {kDynamicF};
    case Dynamic::kFf:
      return {kDynamicF, kDynamicF};
    case Dynamic::kFff:
      return {kDynamicF, kDynamicF, kDynamicF};
  }
}

void add_span_segment(LayoutBuilder& builder, const NotationEntityId& id,
                      const NotationId& semantic, const SystemLayout& system,
                      NotationPoint from, NotationPoint to, double lane,
                      const std::string& role, bool wedge, bool reverse) {
  const std::string segment_role =
      role + "/segment/system-" + std::to_string(system.first_measure);
  const NotationId segment = make_id(id, segment_role);
  builder.output.commands.emplace_back(
      ClipCommand{make_id(segment.value, "clip/begin"), system.bounds, true});
  if (wedge) {
    const double open       = builder.options.staff_space * 0.7;
    const double left_open  = reverse ? open : 0.0;
    const double right_open = reverse ? 0.0 : open;
    builder.add_line(make_id(segment.value, "upper"),
                     {from.x, lane - left_open}, {to.x, lane - right_open},
                     builder.options.staff_space * 0.1);
    builder.add_line(make_id(segment.value, "lower"),
                     {from.x, lane + left_open}, {to.x, lane + right_open},
                     builder.options.staff_space * 0.1);
  } else {
    const double arch = builder.options.staff_space * 1.2;
    builder.add_path(make_id(segment.value, "curve"),
                     {{PathVerb::kMove, {}, {}, from},
                      {PathVerb::kCubic,
                       {from.x + (to.x - from.x) / 3.0, lane - arch},
                       {from.x + (to.x - from.x) * 2.0 / 3.0, lane - arch},
                       to}},
                     builder.options.staff_space * 0.12);
  }
  builder.output.commands.emplace_back(
      ClipCommand{make_id(segment.value, "clip/end"), system.bounds, false});
  builder.add_hit(
      segment, semantic, HitRole::kMarking,
      {std::min(from.x, to.x), lane - builder.options.staff_space * 2.0,
       std::abs(to.x - from.x), builder.options.staff_space * 4.0},
      kHitPrioritySpanSegment);
}

// Collects unique reference IDs for a system's measure range from
// ID-keyed measure buckets.  Returns IDs sorted by monotonic order key
// for deterministic iteration matching source order.
template <typename Record>
[[nodiscard]] std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<Record>&    family,
    const std::vector<MeasureLayout>& measures) {
  std::unordered_set<NotationEntityId> seen;
  std::vector<NotationEntityId>        result;
  for (std::size_t measure = measures.front().ordinal;
       measure <= measures.back().ordinal; ++measure) {
    for (const NotationEntityId& id : family.by_measure[measure]) {
      if (seen.insert(id).second) {
        result.push_back(id);
      }
    }
  }
  std::ranges::sort(
      result, [&](const NotationEntityId& a, const NotationEntityId& b) {
        return family.entries.at(a).order_key < family.entries.at(b).order_key;
      });
  return result;
}

[[nodiscard]] bool add_rhythm(LayoutBuilder&                    builder,
                              const SystemLayout&               system,
                              const StaffSystemLayout&          staff,
                              const StaveVoices&                voices,
                              const IndexedStaff&               indexed,
                              const std::vector<double>&        widths,
                              const std::vector<MeasureLayout>& measures) {
  const double      space       = builder.options.staff_space;
  const MeasureMap& measure_map = builder.timeline.measures();
  auto              placements =
      placements_for_system(builder.timeline, staff.stave_id, voices, indexed,
                            staff, widths, measures);
  std::unordered_map<NotationEntityId, EventPlacement> all_events;
  for (const EventPlacement& placement : placements) {
    all_events.emplace(event_id(*placement.event), placement);
  }
  const auto resolve_event = [&](Voice            voice,
                                 NotationEntityId id) -> const EventPlacement* {
    if (const auto found = all_events.find(id); found != all_events.end()) {
      return &found->second;
    }
    const IndexedVoice& indexed_voice =
        indexed.voices[voice.index() - Voice::kMin];
    const auto record = indexed_voice.by_id.find(id);
    if (record == indexed_voice.by_id.end()) {
      return nullptr;
    }
    const VoiceEvent& event =
        voices.voice(voice).events()[record->second.event_index];
    const ClefLane* lane = builder.timeline.clef_lane(staff.stave_id);
    const Clef      clef =
        lane == nullptr ? Clef::kTreble : lane->clef_at(record->second.onset);
    const auto [inserted, unused] = all_events.emplace(
        id, EventPlacement{
                &event, voice, record->second.onset, record->second.measure,
                0.0, event_anchor_y(event, voice, clef, staff.bounds.y, space),
                stem_up_for(voice, event_stem(event))});
    (void)unused;
    return &inserted->second;
  };

  // Automatic beams remain inside a beat and measure. Manual joins/breaks
  // deterministically override that decision for each adjacent listed pair.
  std::vector<std::pair<NotationEntityId, NotationEntityId>> beam_pairs;
  const auto insert_beam = [&beam_pairs](const auto& pair) {
    if (std::ranges::find(beam_pairs, pair) == beam_pairs.end()) {
      beam_pairs.push_back(pair);
    }
  };
  for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
       ++voice_index) {
    const Voice         voice   = *Voice::create(voice_index);
    const VoiceContent& content = voices.voice(voice);
    const IndexedVoice& voice_indexed =
        indexed.voices[voice_index - Voice::kMin];
    const VoiceEvent* previous = nullptr;
    Rational          previous_onset;
    if (measures.front().ordinal > 0) {
      for (std::size_t ordinal = measures.front().ordinal; ordinal > 0;
           --ordinal) {
        if (!voice_indexed.measures[ordinal - 1].empty()) {
          const IndexedEvent& record =
              voice_indexed.measures[ordinal - 1].back();
          previous       = &content.events()[record.event_index];
          previous_onset = record.onset;
          break;
        }
      }
    }
    for (std::size_t ordinal = measures.front().ordinal;
         ordinal <= measures.back().ordinal; ++ordinal) {
      for (const IndexedEvent& record : voice_indexed.measures[ordinal]) {
        const VoiceEvent& event = content.events()[record.event_index];
        const Rational    onset = record.onset;
        if (previous != nullptr && event_is_beamable(*previous) &&
            event_is_beamable(event)) {
          const auto left_measure =
              measure_map.measure_index_at(previous_onset);
          const auto right_measure = measure_map.measure_index_at(onset);
          if (left_measure == right_measure && left_measure.has_value()) {
            const TimeSignature meter =
                measure_map.measure(*left_measure).time_signature;
            const bool compound = meter.numerator() > 3 &&
                                  meter.numerator() % 3 == 0 &&
                                  meter.denominator() == 8;
            const Rational beat =
                compound ? *Rational::create(3, 8)
                         : *Rational::create(1, meter.denominator());
            const int left_beat = static_cast<int>(
                ((previous_onset - measure_map.measure_start(*left_measure)) /
                 beat)
                    .to_double());
            const int right_beat = static_cast<int>(
                ((onset - measure_map.measure_start(*right_measure)) / beat)
                    .to_double());
            if (left_beat == right_beat) {
              insert_beam(std::pair{event_id(*previous), event_id(event)});
            }
          }
        }
        previous       = &event;
        previous_onset = onset;
      }
    }
    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.beam_overrides, measures)) {
      const BeamOverride& override =
          voice_indexed.beam_overrides.entries.at(id).record;
      if (std::ranges::any_of(
              voice_indexed.diagnostics, [&](const auto& diagnostic) {
                return diagnostic.entity_id == override.id &&
                       diagnostic.code ==
                           NotationDiagnosticCode::kInvalidBeamOverride;
              })) {
        continue;
      }
      for (std::size_t index = 1; index < override.events.size(); ++index) {
        const auto pair =
            std::pair{override.events[index - 1], override.events[index]};
        if (override.kind == BeamOverride::Kind::kJoin) {
          insert_beam(pair);
        } else {
          const auto found = std::ranges::find(beam_pairs, pair);
          if (found != beam_pairs.end()) {
            beam_pairs.erase(found);
          }
        }
      }
    }
  }

  std::size_t group_begin        = 0;
  std::size_t accidental_measure = std::numeric_limits<std::size_t>::max();
  std::map<int, Accidental> accidental_state;
  while (group_begin < placements.size()) {
    std::size_t group_end = group_begin + 1;
    while (group_end < placements.size() &&
           placements[group_end].onset == placements[group_begin].onset) {
      ++group_end;
    }
    std::vector<std::vector<double>> accidental_columns;
    if (placements[group_begin].measure != accidental_measure) {
      accidental_measure = placements[group_begin].measure;
      accidental_state.clear();
    }
    for (std::size_t event_index = group_begin; event_index < group_end;
         ++event_index) {
      const EventPlacement&  placed        = placements[event_index];
      const VoiceEvent&      event         = *placed.event;
      const auto             event_pitches = pitches(event);
      const NotationEntityId entity        = event_id(event);
      const NotationId       semantic{entity.to_string()};
      if (const auto* rest = std::get_if<Rest>(&event)) {
        if (!builder
                 .add_glyph(
                     make_id(rest->id,
                             "voice/" + std::to_string(placed.voice.index()) +
                                 "/" + std::string{kHitSuffixRest}),
                     smufl_codepoint(rest_glyph(rest->duration.base())),
                     {placed.x, placed.anchor_y}, semantic)
                 .has_value()) {
          return false;
        }
        for (std::uint8_t dot = 0; dot < rest->duration.dots(); ++dot) {
          if (!builder
                   .add_glyph(make_id(rest->id, "dot/" + std::to_string(dot)),
                              smufl_codepoint(SmuflGlyph::kAugmentationDot),
                              {placed.x + space * (1.2 + dot * 0.65),
                               placed.anchor_y - space * 0.25},
                              semantic)
                   .has_value()) {
            return false;
          }
        }
      } else {
        const ClefLane* lane = builder.timeline.clef_lane(staff.stave_id);
        const Clef      clef =
            lane == nullptr ? Clef::kTreble : lane->clef_at(placed.onset);
        // Half-extents of one notehead's own hit region, named rather than
        // written inline because the stemless notehead-column region below
        // is defined as exactly the union of them.
        constexpr double    kNoteheadHitHalfWidth  = 0.6;
        constexpr double    kNoteheadHitHalfHeight = 0.225;
        std::vector<double> head_ys;
        double      column_min_x = std::numeric_limits<double>::infinity();
        double      column_max_x = -std::numeric_limits<double>::infinity();
        std::size_t seconds_run  = 0;
        for (std::size_t note_index = 0; note_index < event_pitches.size();
             ++note_index) {
          const auto& [note_id, pitch] = event_pitches[note_index];
          const double y      = pitch_y(pitch, clef, staff.bounds.y, space);
          double       head_x = placed.x;
          if (note_index > 0 && std::abs(y - head_ys.back()) <= space * 0.55) {
            ++seconds_run;
          } else {
            seconds_run = 0;
          }
          if (seconds_run % 2 == 1) {
            head_x += (placed.stem_up ? -1.0 : 1.0) * space * 0.75;
          }
          const bool voice_collision = std::any_of(
              placements.begin() + static_cast<std::ptrdiff_t>(group_begin),
              placements.begin() + static_cast<std::ptrdiff_t>(group_end),
              [&](const EventPlacement& other) {
                if (&other == &placed ||
                    std::holds_alternative<Rest>(*other.event)) {
                  return false;
                }
                const Clef other_clef = lane == nullptr
                                            ? Clef::kTreble
                                            : lane->clef_at(other.onset);
                return std::ranges::any_of(
                    pitches(*other.event), [&](const auto& other_pitch) {
                      return std::abs(pitch_y(other_pitch.second, other_clef,
                                              staff.bounds.y, space) -
                                      y) < space * 0.7;
                    });
              });
          if (voice_collision) {
            head_x += placed.stem_up ? -space * 0.22 : space * 0.22;
          }
          const NotationId head_id =
              make_id(note_id, std::string{kHitSuffixNotehead});
          if (!builder
                   .add_glyph(head_id,
                              smufl_codepoint(
                                  notehead_glyph(event_duration(event).base())),
                              {head_x, y}, NotationId{note_id.to_string()})
                   .has_value()) {
            return false;
          }
          if (!builder.output.hit_regions.empty()) {
            builder.output.hit_regions.back().role     = HitRole::kNotehead;
            builder.output.hit_regions.back().priority = kHitPriorityNotehead;
            builder.output.hit_regions.back().bounds   = {
                head_x - space * kNoteheadHitHalfWidth,
                y - space * kNoteheadHitHalfHeight,
                space * kNoteheadHitHalfWidth * 2.0,
                space * kNoteheadHitHalfHeight * 2.0};
          }
          column_min_x = std::min(column_min_x, head_x);
          column_max_x = std::max(column_max_x, head_x);
          add_ledger_lines(builder, note_id, head_x, y, staff.bounds.y);
          const int        pitch_key = diatonic_index(pitch);
          const auto       state     = accidental_state.find(pitch_key);
          const Accidental prevailing =
              state == accidental_state.end()
                  ? key_accidental(
                        measure_map.measure(placed.measure).key_signature,
                        pitch.letter())
                  : state->second;
          if (pitch.accidental() != prevailing) {
            const auto overlaps_accidental = [&](double occupied_y) {
              return std::abs(y - occupied_y) < space * 1.6;
            };
            std::size_t column = 0;
            while (column < accidental_columns.size() &&
                   std::ranges::any_of(accidental_columns[column],
                                       overlaps_accidental)) {
              ++column;
            }
            if (column == accidental_columns.size()) {
              accidental_columns.push_back({y});
            } else {
              accidental_columns[column].push_back(y);
            }
            const double accidental_x =
                head_x - space * (1.3 + static_cast<double>(column) * 1.05);
            if (!builder
                     .add_glyph(
                         make_id(note_id,
                                 "accidental/column-" + std::to_string(column)),
                         smufl_codepoint(accidental_glyph(pitch.accidental())),
                         {accidental_x, y}, NotationId{note_id.to_string()})
                     .has_value()) {
              return false;
            }
          }
          accidental_state.insert_or_assign(pitch_key, pitch.accidental());
          for (std::uint8_t dot = 0; dot < event_duration(event).dots();
               ++dot) {
            if (!builder
                     .add_glyph(make_id(note_id, "dot/" + std::to_string(dot)),
                                smufl_codepoint(SmuflGlyph::kAugmentationDot),
                                {head_x + space * (1.2 + dot * 0.65),
                                 y - space * 0.25},
                                NotationId{note_id.to_string()})
                     .has_value()) {
              return false;
            }
          }
          head_ys.push_back(y);
        }
        const double stem_x =
            placed.x + (placed.stem_up ? space * 0.65 : -space * 0.65);
        const double head_y   = placed.stem_up
                                    ? *std::ranges::min_element(head_ys)
                                    : *std::ranges::max_element(head_ys);
        const double stem_end = head_y + (placed.stem_up ? -3.5 : 3.5) * space;
        if (event_duration(event).base() != NoteValue::kWhole) {
          const NotationId stem_id =
              make_id(entity, std::string{kHitSuffixStem});
          builder.add_line(stem_id, {stem_x, head_y}, {stem_x, stem_end},
                           space * 0.12);
          // A stem's own drawn width (space * 0.12) is too thin to click
          // comfortably; widen the hit target to half a staff-space while
          // staying well short of the notehead's own tightened width
          // (space * 1.2, kHitPriorityNotehead) so an overlap between the
          // two -- the stem sits right at that notehead's edge, space * 0.65
          // out from its head_x versus the notehead's own space * 0.6
          // half-width -- is resolved by priority, not accidentally avoided
          // by geometry.
          // Role kEvent (not a new HitRole) matches the existing rest/flag
          // convention: kEvent already means "the whole event", which is
          // exactly what clicking a stem (as opposed to one notehead of a
          // chord) selects.
          constexpr double kStemHitHalfWidth = 0.25;
          const double     stem_hit_width    = space * kStemHitHalfWidth * 2.0;
          const double     stem_top          = std::min(head_y, stem_end);
          const double     stem_height       = std::abs(stem_end - head_y);
          builder.add_hit(stem_id, semantic, HitRole::kEvent,
                          NotationRect{stem_x - space * kStemHitHalfWidth,
                                       stem_top, stem_hit_width, stem_height},
                          kHitPriorityGlyph);
        } else {
          // NoteValue::kWhole is the longest value the enum has (there is no
          // breve or longa), so "not stemmed" and "whole" are the same
          // predicate here and this else is exactly the stemless case. A
          // stemless event has no stem region, and so -- before this region
          // existed -- no kEvent geometry at all: a whole-note chord could
          // only ever be clicked into a one-notehead NoteheadSet, never a
          // ChordSet.
          //
          // The substitute is the bounding box of the event's own noteheads,
          // i.e. the union of their own hit regions, which for a chord
          // additionally covers the vertical gaps between them.
          //
          // kHitPriorityNoteheadColumn is a rank of its own, strictly above
          // the containers (so a click in the column beats the blank-staff
          // insertion caret) and strictly below every engraved object (so
          // each notehead, and each per-notehead accidental and augmentation
          // dot the column necessarily overlaps, keeps selecting its own
          // notehead). Neither of the two cheaper separations works here:
          // the equal-priority smaller-area tie-break would decide those
          // overlaps by the font's glyph metrics, and geometry cannot
          // separate them at all, because the seconds rule (space * 0.75)
          // and the voice-collision rule (space * 0.22) both displace a
          // notehead horizontally and can carry column_min_x/column_max_x
          // out past an accidental's own space * 1.3 or a dot's space * 1.2
          // placement offset. Note that a tie's span segment ranks above the
          // column, and its region is tall enough to blanket one, so a tied
          // stemless chord resolves to that tie instead -- see
          // resolve_selection_at's contract comment.
          //
          // head_ys is non-empty here for the same reason the head_y above
          // may dereference min_element/max_element on it unconditionally.
          const NotationId column_id =
              make_id(entity, std::string{kHitSuffixNoteheadColumn});
          const double column_top = *std::ranges::min_element(head_ys) -
                                    space * kNoteheadHitHalfHeight;
          const double column_bottom = *std::ranges::max_element(head_ys) +
                                       space * kNoteheadHitHalfHeight;
          builder.add_hit(
              column_id, semantic, HitRole::kEvent,
              NotationRect{column_min_x - space * kNoteheadHitHalfWidth,
                           column_top,
                           column_max_x - column_min_x +
                               space * kNoteheadHitHalfWidth * 2.0,
                           column_bottom - column_top},
              kHitPriorityNoteheadColumn);
        }
        const bool beamed =
            std::ranges::any_of(beam_pairs, [&](const auto& pair) {
              return pair.first == entity || pair.second == entity;
            });
        if (event_is_beamable(event) && !beamed) {
          const auto level =
              static_cast<std::size_t>(event_duration(event).base()) -
              static_cast<std::size_t>(NoteValue::kEighth);
          const auto     base = placed.stem_up ? SmuflGlyph::kFlag8thUp
                                               : SmuflGlyph::kFlag8thDown;
          const char32_t flag = smufl_codepoint(
              static_cast<SmuflGlyph>(static_cast<std::uint8_t>(base) + level));
          if (!builder
                   .add_glyph(make_id(entity, "flag"), flag, {stem_x, stem_end},
                              semantic)
                   .has_value()) {
            return false;
          }
        }
        if (const auto* articulations = event_articulations(event)) {
          const auto duration_count =
              std::ranges::count_if(*articulations, is_duration_articulation);
          for (std::size_t index = 0; index < articulations->size(); ++index) {
            if (duration_count > 1 &&
                is_duration_articulation((*articulations)[index])) {
              continue;
            }
            const double y = head_y + (placed.stem_up ? 1.0 : -1.0) * space *
                                          (2.0 + static_cast<double>(index));
            const NotationId id =
                make_id(entity, std::string(kHitRoleArticulation) + "/" +
                                    std::to_string(index));
            if (!builder
                     .add_glyph(id,
                                smufl_codepoint(articulation_glyph(
                                    (*articulations)[index])),
                                {placed.x, y}, semantic)
                     .has_value()) {
              return false;
            }
            builder.output.hit_regions.back().role = HitRole::kMarking;
          }
        }
      }
    }
    group_begin = group_end;
  }

  for (const auto& [left_id, right_id] : beam_pairs) {
    const auto left_found = std::ranges::find_if(
        placements,
        [&](const auto& value) { return event_id(*value.event) == left_id; });
    const auto right_found = std::ranges::find_if(
        placements,
        [&](const auto& value) { return event_id(*value.event) == right_id; });
    const EventPlacement* left =
        left_found == placements.end() ? nullptr : &*left_found;
    const EventPlacement* right =
        right_found == placements.end() ? nullptr : &*right_found;
    for (std::uint8_t voice_index = Voice::kMin;
         voice_index <= Voice::kMax && (left == nullptr || right == nullptr);
         ++voice_index) {
      const Voice voice = *Voice::create(voice_index);
      if (left == nullptr) {
        left = resolve_event(voice, left_id);
      }
      if (right == nullptr) {
        right = resolve_event(voice, right_id);
      }
    }
    if (left == nullptr || right == nullptr) {
      continue;
    }
    const Rational beam_system_start =
        measure_map.measure_start(measures.front().ordinal);
    const Rational beam_system_end =
        measure_map.measure_start(measures.back().ordinal) +
        measure_map.measure_length(measures.back().ordinal);
    if (right->onset < beam_system_start || left->onset >= beam_system_end) {
      continue;
    }
    const bool   up     = left->stem_up;
    const double left_y = left->anchor_y + (up ? -3.5 : 3.5) * space;
    const double natural_right_y =
        right->anchor_y + (right->stem_up ? -3.5 : 3.5) * space;
    const double right_y =
        left_y + std::clamp(natural_right_y - left_y, -space, space);
    const double right_stem_x =
        (right->onset >= beam_system_end
             ? measures.back().bounds.x + measures.back().bounds.width -
                   space * 0.5
             : right->x + (right->stem_up ? 0.65 : -0.65) * space);
    if (natural_right_y != right_y) {
      builder.add_line(
          make_id(right_id, "beam-stem-extension/from/" + left_id.to_string()),
          {right_stem_x, natural_right_y}, {right_stem_x, right_y},
          space * 0.12);
    }
    const std::size_t levels =
        std::min(
            static_cast<std::size_t>(event_duration(*left->event).base()),
            static_cast<std::size_t>(event_duration(*right->event).base())) -
        static_cast<std::size_t>(NoteValue::kEighth) + 1;
    for (std::size_t level = 0; level < levels; ++level) {
      const double offset =
          (up ? 1.0 : -1.0) * static_cast<double>(level) * space * 0.65;
      builder.add_line(make_id(left_id, "beam/to/" + right_id.to_string() +
                                            "/level/" + std::to_string(level)),
                       {left->onset < beam_system_start
                            ? measures.front().bounds.x + space * 0.5
                            : left->x + (up ? 0.65 : -0.65) * space,
                        left_y + offset},
                       {right_stem_x, right_y + offset}, space * 0.5);
    }
  }

  const Rational system_start =
      measure_map.measure_start(measures.front().ordinal);
  const auto&    last_measure = measures.back();
  const Rational system_end = measure_map.measure_start(last_measure.ordinal) +
                              measure_map.measure_length(last_measure.ordinal);
  const double left_x = measures.front().bounds.x + space * 0.5;
  const double right_x =
      last_measure.bounds.x + last_measure.bounds.width - space * 0.5;
  const auto span_x = [&](Rational position) {
    if (position <= system_start) {
      return left_x;
    }
    if (position >= system_end) {
      return right_x;
    }
    const auto measure = measure_map.measure_index_at(position);
    const auto local   = *measure - measures.front().ordinal;
    return position_x(measure_map, widths, *measure, position,
                      measures[local].bounds.x, space);
  };

  struct LaneUse {
    double      from = 0.0;
    double      to   = 0.0;
    std::size_t lane = 0;
  };

  std::vector<LaneUse> below_uses;
  std::vector<LaneUse> above_uses;
  const auto allocate_lane = [](std::vector<LaneUse>& uses, double from,
                                double to) {
    std::size_t lane = 0;
    while (std::ranges::any_of(uses, [&](const LaneUse& use) {
      return use.lane == lane && use.from < to && from < use.to;
    })) {
      ++lane;
    }
    uses.push_back({from, to, lane});
    return lane;
  };

  for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
       ++voice_index) {
    const Voice         voice   = *Voice::create(voice_index);
    const VoiceContent& content = voices.voice(voice);
    const IndexedVoice& voice_indexed =
        indexed.voices[voice_index - Voice::kMin];
    if (measures.front().ordinal == 0) {
      for (const NotationDiagnostic& diagnostic : voice_indexed.diagnostics) {
        builder.output.diagnostics.push_back(
            {diagnostic.entity_id,
             "omitted-invalid-reference:" +
                 std::to_string(static_cast<int>(diagnostic.code))});
      }
    }
    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.dynamics, measures)) {
      const DynamicMarking& dynamic =
          voice_indexed.dynamics.entries.at(id).record;
      const EventPlacement* at = resolve_event(voice, dynamic.at_event);
      if (at == nullptr || at->onset < system_start ||
          at->onset >= system_end) {
        continue;
      }
      double            x          = span_x(at->onset);
      const auto        glyphs     = dynamic_glyphs(dynamic.value);
      const std::size_t local_lane = allocate_lane(
          below_uses, x - space * 0.3,
          x + static_cast<double>(glyphs.size()) * space + space * 0.3);
      const double y = staff.bounds.y + staff.bounds.height +
                       space * (2.0 + static_cast<double>(local_lane) * 1.6);
      const NotationId semantic{dynamic.id.to_string()};
      for (std::size_t index = 0; index < glyphs.size(); ++index) {
        if (!builder
                 .add_glyph(
                     make_id(dynamic.id, "glyph/" + std::to_string(index)),
                     smufl_codepoint(glyphs[index]), {x, y}, semantic)
                 .has_value()) {
          return false;
        }
        builder.output.hit_regions.back().role = HitRole::kMarking;
        x += space;
      }
    }
    const auto add_event_span = [&](const auto& span, const std::string& role,
                                    std::size_t stack, bool wedge,
                                    bool reverse) {
      const EventPlacement* start = resolve_event(voice, span.start_event);
      const EventPlacement* end   = resolve_event(voice, span.end_event);
      if (start == nullptr || end == nullptr || !(start->onset < end->onset) ||
          end->onset < system_start || start->onset >= system_end) {
        return;
      }
      if (role == "slur" && (std::holds_alternative<Rest>(*start->event) ||
                             std::holds_alternative<Rest>(*end->event))) {
        return;
      }
      const double          from_x = span_x(start->onset);
      const double          to_x   = span_x(end->onset);
      std::vector<LaneUse>& uses   = role == "slur" ? above_uses : below_uses;
      const std::size_t     local_lane = allocate_lane(uses, from_x, to_x);
      const double          lane =
          role == "slur"
                       ? staff.bounds.y -
                    space * (2.0 + static_cast<double>(local_lane) * 1.6)
                       : staff.bounds.y + staff.bounds.height +
                    space * (4.0 + static_cast<double>(local_lane) * 1.6);
      (void)stack;
      add_span_segment(builder, span.id, NotationId{span.id.to_string()},
                       system, {from_x, lane}, {to_x, lane}, lane, role, wedge,
                       reverse);
    };
    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.hairpins, measures)) {
      const Hairpin& hairpin = voice_indexed.hairpins.entries.at(id).record;
      add_event_span(hairpin, "hairpin", 0, true,
                     hairpin.direction == HairpinDirection::kDiminuendo);
    }
    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.slurs, measures)) {
      const Slur& slur = voice_indexed.slurs.entries.at(id).record;
      add_event_span(slur, "slur", 0, false, false);
    }

    const auto&               events = content.events();
    std::vector<IndexedEvent> local_events;
    if (measures.front().ordinal > 0 &&
        !voice_indexed.measures[measures.front().ordinal - 1].empty()) {
      local_events.push_back(
          voice_indexed.measures[measures.front().ordinal - 1].back());
    }
    for (std::size_t ordinal = measures.front().ordinal;
         ordinal <= measures.back().ordinal; ++ordinal) {
      local_events.insert(local_events.end(),
                          voice_indexed.measures[ordinal].begin(),
                          voice_indexed.measures[ordinal].end());
    }
    for (const IndexedEvent& record : local_events) {
      const VoiceEvent& event = events[record.event_index];
      if (record.event_index + 1 < events.size()) {
        const auto source_pitches = pitches(event);
        const auto target_pitches = pitches(events[record.event_index + 1]);
        for (const auto& [note_id, pitch] : source_pitches) {
          const bool tied = std::visit(
              [&](const auto& concrete) {
                using Event = std::decay_t<decltype(concrete)>;
                if constexpr (std::is_same_v<Event, Note>) {
                  return concrete.id == note_id && concrete.tied_to_next;
                } else if constexpr (std::is_same_v<Event, Chord>) {
                  const auto found = std::ranges::find(concrete.notes, note_id,
                                                       &ChordNote::id);
                  return found != concrete.notes.end() && found->tied_to_next;
                }
                return false;
              },
              event);
          if (!tied ||
              !std::ranges::any_of(target_pitches, [&](const auto& item) {
                return item.second == pitch;
              })) {
            continue;
          }
          const Rational end_onset =
              record.onset + event_duration(event).resolved();
          if (end_onset >= system_start && record.onset < system_end) {
            const ClefLane* lane = builder.timeline.clef_lane(staff.stave_id);
            const Clef      clef =
                lane == nullptr ? Clef::kTreble : lane->clef_at(record.onset);
            const double y =
                pitch_y(pitch, clef, staff.bounds.y, space) + space;
            add_span_segment(builder, note_id, NotationId{note_id.to_string()},
                             system, {span_x(record.onset), y},
                             {span_x(end_onset), y}, y,
                             std::string(kHitRoleTie), false, false);
          }
        }
      }
    }

    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.grace_groups, measures)) {
      const GraceGroup& group =
          voice_indexed.grace_groups.entries.at(id).record;
      const EventPlacement* principal =
          resolve_event(voice, group.principal_event);
      if (principal == nullptr || principal->onset < system_start ||
          principal->onset >= system_end ||
          std::holds_alternative<Rest>(*principal->event)) {
        continue;
      }
      const ClefLane* lane = builder.timeline.clef_lane(staff.stave_id);
      const Clef      clef =
          lane == nullptr ? Clef::kTreble : lane->clef_at(principal->onset);
      for (std::size_t index = 0; index < group.notes.size(); ++index) {
        const GraceNote& grace = group.notes[index];
        const double distance = static_cast<double>(group.notes.size() - index);
        const double x =
            span_x(principal->onset) - space * (2.0 + distance * 1.3);
        const double y = pitch_y(grace.pitch, clef, staff.bounds.y, space);
        if (!builder
                 .add_glyph(
                     make_id(grace.id, std::string{kHitSuffixGraceNotehead}),
                     smufl_codepoint(notehead_glyph(grace.duration.base())),
                     {x, y}, NotationId{grace.id.to_string()}, 0.65)
                 .has_value()) {
          return false;
        }
        builder.output.hit_regions.back().role     = HitRole::kNotehead;
        builder.output.hit_regions.back().priority = kHitPriorityNotehead;
        const double grace_space                   = space * 0.65;
        const double stem_x                        = x + grace_space * 0.65;
        const double stem_end                      = y - grace_space * 3.5;
        if (grace.duration.base() != NoteValue::kWhole) {
          builder.add_line(make_id(grace.id, "grace-stem"), {stem_x, y},
                           {stem_x, stem_end}, grace_space * 0.12);
        }
        if (grace.duration.base() >= NoteValue::kEighth) {
          const auto level = static_cast<std::size_t>(grace.duration.base()) -
                             static_cast<std::size_t>(NoteValue::kEighth);
          const char32_t flag = smufl_codepoint(static_cast<SmuflGlyph>(
              static_cast<std::uint8_t>(SmuflGlyph::kFlag8thUp) + level));
          if (!builder
                   .add_glyph(make_id(grace.id, "grace-flag"), flag,
                              {stem_x, stem_end}, std::nullopt, 0.65)
                   .has_value()) {
            return false;
          }
        }
        if (grace.pitch.accidental() !=
            key_accidental(
                measure_map.measure(principal->measure).key_signature,
                grace.pitch.letter())) {
          if (!builder
                   .add_glyph(make_id(grace.id, "grace-accidental"),
                              smufl_codepoint(
                                  accidental_glyph(grace.pitch.accidental())),
                              {x - grace_space * 1.5, y}, std::nullopt, 0.65)
                   .has_value()) {
            return false;
          }
        }
        for (std::uint8_t dot = 0; dot < grace.duration.dots(); ++dot) {
          if (!builder
                   .add_glyph(
                       make_id(grace.id, "grace-dot/" + std::to_string(dot)),
                       smufl_codepoint(SmuflGlyph::kAugmentationDot),
                       {x + grace_space * (1.2 + dot * 0.65),
                        y - grace_space * 0.25},
                       std::nullopt, 0.65)
                   .has_value()) {
            return false;
          }
        }
        int ledger = 0;
        for (double ledger_y = staff.bounds.y - space;
             y <= ledger_y + space * 0.25; ledger_y -= space) {
          builder.add_line(make_id(grace.id, "grace-ledger/above/" +
                                                 std::to_string(ledger++)),
                           {x - grace_space * 0.85, ledger_y},
                           {x + grace_space * 0.85, ledger_y},
                           grace_space * 0.12);
        }
        ledger = 0;
        for (double ledger_y = staff.bounds.y + space * 5.0;
             y >= ledger_y - space * 0.25; ledger_y += space) {
          builder.add_line(make_id(grace.id, "grace-ledger/below/" +
                                                 std::to_string(ledger++)),
                           {x - grace_space * 0.85, ledger_y},
                           {x + grace_space * 0.85, ledger_y},
                           grace_space * 0.12);
        }
        if (grace.slashed) {
          builder.add_line(make_id(grace.id, "slash"),
                           {x - grace_space * 0.4, y + grace_space},
                           {x + grace_space * 0.8, y - grace_space},
                           grace_space * 0.12);
        }
      }
    }

    std::size_t local_index = 0;
    while (local_index < local_events.size()) {
      const IndexedEvent& first_record = local_events[local_index];
      const auto          ratio =
          event_duration(events[first_record.event_index]).tuplet();
      const Rational run_start = first_record.onset;
      std::size_t    end       = local_index + 1;
      Rational       run_end =
          run_start +
          event_duration(events[first_record.event_index]).resolved();
      while (ratio.has_value() && end < local_events.size() &&
             event_duration(events[local_events[end].event_index]).tuplet() ==
                 ratio) {
        run_end =
            run_end +
            event_duration(events[local_events[end].event_index]).resolved();
        ++end;
      }
      const auto is_incomplete_tuplet_diagnostic = [&](const auto& diagnostic) {
        return diagnostic.entity_id == first_record.id &&
               diagnostic.code ==
                   NotationDiagnosticCode::kIncompleteTupletGroup;
      };
      if (ratio.has_value() && run_end > system_start &&
          run_start < system_end &&
          !std::ranges::any_of(voice_indexed.diagnostics,
                               is_incomplete_tuplet_diagnostic)) {
        const NotationEntityId id = first_record.id;
        const double y = staff.bounds.y - space * (2.5 + voice_index * 0.7);
        builder.add_line(make_id(id, "tuplet/bracket"), {span_x(run_start), y},
                         {span_x(run_end), y}, space * 0.1);
        const std::string number = std::to_string(ratio->played());
        double            x      = (span_x(run_start) + span_x(run_end)) * 0.5 -
                   static_cast<double>(number.size()) * space * 0.4;
        for (std::size_t digit = 0; digit < number.size(); ++digit) {
          const char32_t code = smufl_codepoint(SmuflGlyph::kTupletDigit0) +
                                static_cast<char32_t>(number[digit] - '0');
          if (!builder
                   .add_glyph(make_id(id, std::string(kHitRoleTupletDigit) +
                                              "/" + std::to_string(digit)),
                              code, {x, y}, NotationId{id.to_string()})
                   .has_value()) {
            return false;
          }
          builder.output.hit_regions.back().role = HitRole::kMarking;
          x += space * 0.8;
        }
      }
      local_index = end;
    }
  }
  return true;
}

[[nodiscard]] bool add_pedal_spans(LayoutBuilder&                builder,
                                   const SystemLayout&           system,
                                   const StaffSystemLayout&      staff,
                                   const std::vector<PedalSpan>& spans) {
  const MeasureMap& map   = builder.timeline.measures();
  const Rational    start = map.measure_start(system.measures.front().ordinal);
  const MeasureLayout& last = system.measures.back();
  const Rational       end =
      map.measure_start(last.ordinal) + map.measure_length(last.ordinal);
  const double space = builder.options.staff_space;
  const auto   x_for = [&](Rational position) {
    if (position <= start) {
      return system.measures.front().bounds.x + space * 0.5;
    }
    if (position >= end) {
      return last.bounds.x + last.bounds.width - space * 0.5;
    }
    const std::size_t    measure = *map.measure_index_at(position);
    const MeasureLayout& layout =
        system.measures[measure - system.first_measure];
    const double fraction =
        ((position - map.measure_start(measure)).to_double() /
         map.measure_length(measure).to_double());
    return layout.bounds.x + layout.bounds.width * fraction;
  };
  if (system.first_measure == 0) {
    for (const NotationDiagnostic& diagnostic :
         validate_pedal_spans(spans, builder.timeline.node_end())) {
      builder.output.diagnostics.push_back(
          {diagnostic.entity_id,
           "omitted-invalid-pedal:" +
               std::to_string(static_cast<int>(diagnostic.code))});
    }
  }
  std::vector<std::pair<Rational, Rational>> lanes;
  for (const PedalSpan& span : spans) {
    if (!(span.start < span.end) || span.start < Rational(0) ||
        span.end > builder.timeline.node_end() || span.end <= start ||
        span.start >= end) {
      continue;
    }
    std::size_t lane = 0;
    while (lane < lanes.size() && lanes[lane].first < span.end &&
           span.start < lanes[lane].second) {
      ++lane;
    }
    if (lane == lanes.size()) {
      lanes.emplace_back(span.start, span.end);
    } else {
      lanes[lane] = {span.start, span.end};
    }
    const double y = staff.bounds.y + staff.bounds.height +
                     space * (7.0 + static_cast<double>(lane) * 1.6);
    const NotationId  semantic{span.id.to_string()};
    const std::string role =
        "pedal/segment/system-" + std::to_string(system.first_measure);
    const NotationId segment = make_id(span.id, role);
    builder.output.commands.emplace_back(
        ClipCommand{make_id(segment.value, "clip/begin"), system.bounds, true});
    if (span.start >= start) {
      if (!builder
               .add_glyph(make_id(segment.value, "down"),
                          smufl_codepoint(SmuflGlyph::kPedalDown),
                          {x_for(span.start), y}, semantic)
               .has_value()) {
        return false;
      }
      builder.output.hit_regions.back().role = HitRole::kMarking;
    }
    builder.add_line(make_id(segment.value, "line"),
                     {x_for(span.start), y + space * 0.7},
                     {x_for(span.end), y + space * 0.7}, space * 0.12);
    if (span.end <= end) {
      if (!builder
               .add_glyph(make_id(segment.value, "up"),
                          smufl_codepoint(SmuflGlyph::kPedalUp),
                          {x_for(span.end), y}, semantic)
               .has_value()) {
        return false;
      }
      builder.output.hit_regions.back().role = HitRole::kMarking;
    }
    builder.output.commands.emplace_back(
        ClipCommand{make_id(segment.value, "clip/end"), system.bounds, false});
    builder.add_hit(segment, semantic, HitRole::kMarking,
                    {x_for(span.start), y - space,
                     x_for(span.end) - x_for(span.start), space * 2.0},
                    kHitPrioritySpanSegment);
  }
  return true;
}

[[nodiscard]] NotationLayoutResult fail(NotationLayoutError error) {
  return NotationLayoutResult{error, std::nullopt};
}

// Shared per-command finiteness rule behind both NotationLayout::
// geometry_is_finite() and NotationPreview::geometry_is_finite(), so a
// preview's standalone command list can be validated the same way without
// folding it into a real NotationLayout.
[[nodiscard]] bool finite_command(const NotationCommand& command) {
  return std::visit(
      [](const auto& concrete) {
        using Command = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<Command, GlyphCommand>) {
          return finite_point(concrete.origin) &&
                 std::isfinite(concrete.staff_space) &&
                 concrete.staff_space > 0.0;
        } else if constexpr (std::is_same_v<Command, LineCommand>) {
          return finite_point(concrete.from) && finite_point(concrete.to) &&
                 std::isfinite(concrete.width) && concrete.width >= 0.0;
        } else if constexpr (std::is_same_v<Command, PathCommand>) {
          return std::isfinite(concrete.stroke_width) &&
                 concrete.stroke_width >= 0.0 &&
                 std::all_of(concrete.elements.begin(), concrete.elements.end(),
                             [](const PathElement& element) {
                               return finite_point(element.control1) &&
                                      finite_point(element.control2) &&
                                      finite_point(element.end);
                             });
        } else {
          return finite_rect(concrete.bounds);
        }
      },
      command);
}

[[nodiscard]] bool geometry_is_bounded(const NotationLayout& layout) {
  if (!bounded_rect(layout.bounds) ||
      !std::ranges::all_of(
          layout.systems,
          [](const SystemLayout& system) {
            return bounded_rect(system.bounds) &&
                   std::ranges::all_of(system.measures,
                                       [](const MeasureLayout& measure) {
                                         return bounded_rect(measure.bounds);
                                       }) &&
                   std::ranges::all_of(
                       system.staves, [](const StaffSystemLayout& staff) {
                         return bounded_rect(staff.bounds) &&
                                std::ranges::all_of(staff.measure_bounds,
                                                    bounded_rect);
                       });
          }) ||
      !std::ranges::all_of(layout.hit_regions, [](const HitRegion& hit) {
        return bounded_rect(hit.bounds);
      })) {
    return false;
  }
  return std::ranges::all_of(
      layout.commands, [](const NotationCommand& command) {
        return std::visit(
            [](const auto& concrete) {
              using Command = std::decay_t<decltype(concrete)>;
              if constexpr (std::is_same_v<Command, GlyphCommand>) {
                return bounded_point(concrete.origin) &&
                       concrete.staff_space <=
                           NotationLayoutOptions::kMaximumCoordinate;
              } else if constexpr (std::is_same_v<Command, LineCommand>) {
                return bounded_point(concrete.from) &&
                       bounded_point(concrete.to) &&
                       concrete.width <=
                           NotationLayoutOptions::kMaximumCoordinate;
              } else if constexpr (std::is_same_v<Command, PathCommand>) {
                return concrete.stroke_width <=
                           NotationLayoutOptions::kMaximumCoordinate &&
                       std::ranges::all_of(
                           concrete.elements, [](const PathElement& element) {
                             return bounded_point(element.control1) &&
                                    bounded_point(element.control2) &&
                                    bounded_point(element.end);
                           });
              } else {
                return bounded_rect(concrete.bounds);
              }
            },
            command);
      });
}

}  // namespace

char32_t smufl_codepoint(SmuflGlyph glyph) noexcept {
  switch (glyph) {
    case SmuflGlyph::kGClef:
      return U'\uE050';
    case SmuflGlyph::kCClef:
      return U'\uE05C';
    case SmuflGlyph::kFClef:
      return U'\uE062';
    case SmuflGlyph::kNoteheadWhole:
      return U'\uE0A2';
    case SmuflGlyph::kNoteheadHalf:
      return U'\uE0A3';
    case SmuflGlyph::kNoteheadBlack:
      return U'\uE0A4';
    case SmuflGlyph::kRestWhole:
      return U'\uE4E3';
    case SmuflGlyph::kRestHalf:
      return U'\uE4E4';
    case SmuflGlyph::kRestQuarter:
      return U'\uE4E5';
    case SmuflGlyph::kRestEighth:
      return U'\uE4E6';
    case SmuflGlyph::kRest16th:
      return U'\uE4E7';
    case SmuflGlyph::kRest32nd:
      return U'\uE4E8';
    case SmuflGlyph::kRest64th:
      return U'\uE4E9';
    case SmuflGlyph::kAugmentationDot:
      return U'\uE1E7';
    case SmuflGlyph::kAccidentalDoubleFlat:
      return U'\uE264';
    case SmuflGlyph::kAccidentalFlat:
      return U'\uE260';
    case SmuflGlyph::kAccidentalNatural:
      return U'\uE261';
    case SmuflGlyph::kAccidentalSharp:
      return U'\uE262';
    case SmuflGlyph::kAccidentalDoubleSharp:
      return U'\uE263';
    case SmuflGlyph::kFlag8thUp:
      return U'\uE240';
    case SmuflGlyph::kFlag16thUp:
      return U'\uE242';
    case SmuflGlyph::kFlag32ndUp:
      return U'\uE244';
    case SmuflGlyph::kFlag64thUp:
      return U'\uE246';
    case SmuflGlyph::kFlag8thDown:
      return U'\uE241';
    case SmuflGlyph::kFlag16thDown:
      return U'\uE243';
    case SmuflGlyph::kFlag32ndDown:
      return U'\uE245';
    case SmuflGlyph::kFlag64thDown:
      return U'\uE247';
    case SmuflGlyph::kDynamicP:
      return U'\uE520';
    case SmuflGlyph::kDynamicM:
      return U'\uE521';
    case SmuflGlyph::kDynamicF:
      return U'\uE522';
    case SmuflGlyph::kArticAccentAbove:
      return U'\uE4A0';
    case SmuflGlyph::kArticMarcatoAbove:
      return U'\uE4AC';
    case SmuflGlyph::kArticStaccatoAbove:
      return U'\uE4A2';
    case SmuflGlyph::kArticStaccatissimoAbove:
      return U'\uE4A6';
    case SmuflGlyph::kArticTenutoAbove:
      return U'\uE4A4';
    case SmuflGlyph::kPedalDown:
      return U'\uE650';
    case SmuflGlyph::kPedalUp:
      return U'\uE655';
    case SmuflGlyph::kTimeDigit0:
      return U'\uE080';
    case SmuflGlyph::kTupletDigit0:
      return U'\uE880';
  }
  return U'\uFFFD';
}

bool NotationRect::contains(NotationPoint point) const noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= x &&
         point.x <= x + width && point.y >= y && point.y <= y + height;
}

std::optional<HitResult> NotationLayout::hit_test(NotationPoint point) const {
  const HitRegion* best = nullptr;
  const auto       area = [](const HitRegion& region) {
    return region.bounds.width * region.bounds.height;
  };
  for (const HitRegion& region : hit_regions) {
    if (!finite_rect(region.bounds) || !region.bounds.contains(point)) {
      continue;
    }
    if (best == nullptr || region.priority > best->priority ||
        (region.priority == best->priority && area(region) < area(*best)) ||
        (region.priority == best->priority && area(region) == area(*best) &&
         region.semantic_id < best->semantic_id) ||
        (region.priority == best->priority && area(region) == area(*best) &&
         region.semantic_id == best->semantic_id && region.id < best->id)) {
      best = &region;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  return HitResult{best->id, best->semantic_id, best->role};
}

bool NotationLayout::geometry_is_finite() const {
  if (!finite_rect(bounds) ||
      !std::all_of(
          systems.begin(), systems.end(),
          [](const SystemLayout& system) {
            return finite_rect(system.bounds) &&
                   std::all_of(system.measures.begin(), system.measures.end(),
                               [](const MeasureLayout& measure) {
                                 return finite_rect(measure.bounds);
                               }) &&
                   std::all_of(system.staves.begin(), system.staves.end(),
                               [](const StaffSystemLayout& staff) {
                                 return finite_rect(staff.bounds) &&
                                        std::all_of(
                                            staff.measure_bounds.begin(),
                                            staff.measure_bounds.end(),
                                            finite_rect);
                               });
          }) ||
      !std::all_of(
          hit_regions.begin(), hit_regions.end(),
          [](const HitRegion& region) { return finite_rect(region.bounds); })) {
    return false;
  }
  return std::all_of(commands.begin(), commands.end(), finite_command);
}

bool NotationPreview::geometry_is_finite() const {
  return std::all_of(commands.begin(), commands.end(), finite_command);
}

bool NotationLayoutOptions::valid() const noexcept {
  const double values[] = {
      system_width,          left_margin,        right_margin, top_margin,
      bottom_margin,         staff_space,        stave_gap,    system_gap,
      minimum_measure_width, whole_note_spacing,
  };
  if (!std::all_of(std::begin(values), std::end(values), [](double value) {
        return std::isfinite(value) &&
               value <= NotationLayoutOptions::kMaximumCoordinate;
      })) {
    return false;
  }
  return system_width > 0.0 && left_margin >= 0.0 && right_margin >= 0.0 &&
         top_margin >= 0.0 && bottom_margin >= 0.0 && staff_space > 0.0 &&
         stave_gap >= 0.0 && system_gap >= 0.0 && minimum_measure_width > 0.0 &&
         whole_note_spacing > 0.0 && left_margin + right_margin < system_width;
}

NotationLayoutResult layout_internal(
    const Project& project, NodeId node_id, const GlyphMetrics& metrics,
    const NotationLayoutOptions& options, const LayoutIndex& layout_index,
    const std::vector<double>&         widths,
    const std::vector<SystemFragment>* previous,
    const std::vector<std::size_t>&    invalid_systems,
    std::vector<SystemFragment>* retained, NotationLayoutWork* work) {
  if (!options.valid()) {
    return fail(NotationLayoutError::kInvalidOptions);
  }
  const Node* node = project.find_node(node_id);
  if (node == nullptr) {
    return fail(NotationLayoutError::kNodeNotFound);
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return fail(NotationLayoutError::kTimelineMissing);
  }
  LayoutBuilder builder{*timeline, metrics, options, NotationLayout{},
                        NotationLayoutError::kNone};
  builder.output.node_id     = node_id;
  const MeasureMap& measures = timeline->measures();
  const double      content_width =
      options.system_width - options.left_margin - options.right_margin;
  const auto ranges = system_ranges(widths, content_width);

  std::size_t stave_count = 0;
  for (const Track& track : project.active_tracks()) {
    stave_count += track.layout().stave_count();
  }
  const double staff_height = options.staff_space * 4.0;
  // Each stave owns vertical engraving lanes above/below its five lines.
  // Reserving the lanes in the system bounds makes span clips truthful and
  // keeps dynamics/pedal geometry from colliding with the following stave.
  // Reserve a stable per-system marking budget, divided among its staves.
  // Overlap-based lane reuse means later disjoint collections do not alter
  // this extent or push unrelated retained systems. A one-staff system has
  // room for well over one hundred simultaneous lanes; dense multi-staff
  // systems retain a sixteen-space minimum per stave.
  const double staff_slot_height =
      options.staff_space *
      std::max(16.0, 256.0 / static_cast<double>(
                                 std::max(stave_count, std::size_t{1})));
  const double system_top_padding = options.staff_space * 6.0;
  const double system_height =
      stave_count == 0
          ? 0.0
          : system_top_padding +
                static_cast<double>(stave_count) * staff_slot_height +
                static_cast<double>(stave_count - 1) * options.stave_gap;

  double system_y      = options.top_margin;
  double maximum_width = options.system_width;
  for (const auto [first, end] : ranges) {
    double used_width = 0.0;
    for (std::size_t index = first; index < end; ++index) {
      used_width += widths[index];
    }
    const double actual_width =
        std::max(options.system_width,
                 options.left_margin + used_width + options.right_margin);
    maximum_width = std::max(maximum_width, actual_width);
    const auto old =
        previous == nullptr
            ? std::vector<SystemFragment>::const_iterator{}
            : std::ranges::find_if(*previous, [first](const auto& fragment) {
                return fragment.system.first_measure == first;
              });
    const bool invalid =
        std::ranges::find(invalid_systems, first) != invalid_systems.end();
    const NotationRect expected_bounds{0.0, system_y, actual_width,
                                       system_height};
    if (previous != nullptr && !invalid && old != previous->end() &&
        old->system.bounds == expected_bounds &&
        old->system.measures.size() == end - first) {
      append_fragment(builder.output, *old);
      if (retained != nullptr) {
        retained->push_back(*old);
      }
      if (work != nullptr) {
        work->reused_systems.push_back(first);
        for (std::size_t ordinal = first; ordinal < end; ++ordinal) {
          work->reused_measures.push_back(ordinal);
        }
      }
      system_y += system_height + options.system_gap;
      continue;
    }

    const std::size_t command_begin    = builder.output.commands.size();
    const std::size_t hit_begin        = builder.output.hit_regions.size();
    const std::size_t diagnostic_begin = builder.output.diagnostics.size();
    SystemLayout      system;
    system.first_measure = first;
    system.id            = make_id(node_id, "system/" + std::to_string(first));
    system.bounds = NotationRect{0.0, system_y, actual_width, system_height};
    builder.output.hit_regions.push_back(
        HitRegion{make_id(system.id.value, "hit"), system.id, HitRole::kSystem,
                  system.bounds, kHitPrioritySystem});
    builder.output.commands.emplace_back(ClipCommand{
        make_id(system.id.value, "clip/begin"), system.bounds, true});

    double measure_x = options.left_margin;
    for (std::size_t index = first; index < end; ++index) {
      const NotationId measure_id =
          make_id(node_id, "measure/" + std::to_string(index));
      const NotationRect bounds{measure_x, system_y, widths[index],
                                system_height};
      system.measures.push_back(MeasureLayout{index, measure_id, bounds});
      builder.output.hit_regions.push_back(HitRegion{
          make_id(measure_id.value, "system/" + std::to_string(first) + "/hit"),
          measure_id, HitRole::kMeasure, bounds, kHitPriorityMeasure});
      measure_x += widths[index];
    }

    std::size_t stave_ordinal = 0;
    for (const Track& track : project.active_tracks()) {
      const TrackLane* lane = node->lane(track.id());
      if (lane == nullptr) {
        return fail(NotationLayoutError::kLaneMissing);
      }
      for (const StaveDefinition& stave_definition : track.layout().staves()) {
        const StaveVoices* voices = lane->stave(stave_definition.id);
        if (voices == nullptr) {
          static const StaveVoices kEmptyVoices;
          voices = &kEmptyVoices;
        }
        StaffSystemLayout staff;
        staff.track_id = track.id();
        staff.stave_id = stave_definition.id;
        staff.id =
            make_id(stave_definition.id, "system/" + std::to_string(first));
        const double staff_y = system_y + system_top_padding +
                               static_cast<double>(stave_ordinal) *
                                   (staff_slot_height + options.stave_gap);
        staff.bounds =
            NotationRect{options.left_margin, staff_y,
                         measure_x - options.left_margin, staff_height};
        for (const MeasureLayout& measure : system.measures) {
          const NotationRect measure_staff_bounds{
              measure.bounds.x, staff_y, measure.bounds.width, staff_height};
          staff.measure_bounds.push_back(measure_staff_bounds);
          const NotationId staff_measure_id =
              staff_measure_semantic_id(staff.id, measure.ordinal);
          builder.output.hit_regions.push_back(
              HitRegion{make_id(staff_measure_id.value, "hit"),
                        staff_measure_id, HitRole::kStaffMeasure,
                        measure_staff_bounds, kHitPriorityStaffMeasure});
        }
        builder.output.hit_regions.push_back(
            HitRegion{make_id(staff.id.value, "hit"),
                      NotationId{stave_definition.id.to_string()},
                      HitRole::kStaff, staff.bounds, kHitPriorityStaff});
        for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
             ++voice_index) {
          const Voice      voice    = *Voice::create(voice_index);
          const NotationId voice_id = make_id(
              stave_definition.id, "voice/" + std::to_string(voice_index));
          const IndexedVoice& indexed_voice =
              indexed_staff(layout_index, stave_definition.id)
                  ->voices[voice_index - Voice::kMin];
          std::size_t event_count = 0;
          for (std::size_t ordinal = first; ordinal < end; ++ordinal) {
            event_count += indexed_voice.measures[ordinal].size();
          }
          staff.voices.push_back(VoiceLayout{voice, voice_id, event_count});
          builder.output.hit_regions.push_back(HitRegion{
              make_id(voice_id.value,
                      "system/" + std::to_string(first) + "/hit"),
              voice_id, HitRole::kVoice, staff.bounds, kHitPriorityVoice});
        }
        for (int line = 0; line < 5; ++line) {
          const double y =
              staff_y + static_cast<double>(line) * options.staff_space;
          builder.add_line(
              make_id(staff.id.value, "line/" + std::to_string(line)),
              NotationPoint{options.left_margin, y},
              NotationPoint{measure_x, y}, options.staff_space * 0.1);
        }
        for (const MeasureLayout& measure : system.measures) {
          builder.add_line(
              make_id(
                  staff.id.value,
                  "measure/" + std::to_string(measure.ordinal) + "/barline"),
              NotationPoint{measure.bounds.x, staff_y},
              NotationPoint{measure.bounds.x, staff_y + staff_height},
              options.staff_space * 0.15);
          const ClefLane* clef_lane = timeline->clef_lane(stave_definition.id);
          const Rational  measure_start =
              measures.measure_start(measure.ordinal);
          const Clef clef         = clef_lane == nullptr
                                        ? stave_definition.default_clef
                                        : clef_lane->clef_at(measure_start);
          const bool system_start = measure.ordinal == first;
          const bool key_change =
              measure.ordinal == 0 ||
              measures.measure(measure.ordinal - 1).key_signature !=
                  measures.measure(measure.ordinal).key_signature;
          const bool time_change =
              measure.ordinal == 0 ||
              measures.measure(measure.ordinal - 1).time_signature !=
                  measures.measure(measure.ordinal).time_signature;
          const bool clef_change =
              clef_lane != nullptr &&
              std::ranges::any_of(clef_lane->changes(),
                                  [&](const ClefChange& change) {
                                    return change.position == measure_start;
                                  });
          if (!add_signature_glyphs(
                  builder, measures.measure(measure.ordinal), clef, staff.id,
                  NotationPoint{measure.bounds.x, staff_y}, measure.ordinal,
                  system_start || clef_change, system_start || key_change,
                  system_start || time_change,
                  key_change && measure.ordinal > 0
                      ? std::optional<KeySignature>{measures
                                                        .measure(
                                                            measure.ordinal - 1)
                                                        .key_signature}
                      : std::nullopt)) {
            return fail(builder.error);
          }
          if (clef_lane != nullptr) {
            const Rational measure_end =
                measure_start + measures.measure_length(measure.ordinal);
            for (const ClefChange& change : clef_lane->changes()) {
              if (change.position <= measure_start ||
                  change.position >= measure_end) {
                continue;
              }
              const auto        components = change.position.to_components();
              const std::string role =
                  "clef-change/" + std::to_string(components.numerator) + "-" +
                  std::to_string(components.denominator);
              const NotationId change_id = make_id(staff.id.value, role);
              const double     measure_position =
                  position_x(measures, widths, measure.ordinal, change.position,
                             measure.bounds.x, options.staff_space);
              const double x = measure_position + options.staff_space;
              if (!builder
                       .add_glyph(change_id, clef_glyph(change.clef),
                                  {x, staff_y},
                                  NotationId{stave_definition.id.to_string()})
                       .has_value()) {
                return fail(builder.error);
              }
            }
          }
        }
        const MeasureLayout& last = system.measures.back();
        builder.add_line(
            make_id(staff.id.value,
                    "measure/" + std::to_string(last.ordinal) + "/end-barline"),
            NotationPoint{last.bounds.x + last.bounds.width, staff_y},
            NotationPoint{last.bounds.x + last.bounds.width,
                          staff_y + staff_height},
            options.staff_space * 0.15);
        if (!add_rhythm(builder, system, staff, *voices,
                        *indexed_staff(layout_index, stave_definition.id),
                        widths, system.measures)) {
          return fail(builder.error);
        }
        const IndexedStaff* staff_index =
            indexed_staff(layout_index, stave_definition.id);
        std::vector<PedalSpan> system_pedals;
        for (const NotationEntityId& id :
             system_reference_ids(staff_index->pedals, system.measures)) {
          system_pedals.push_back(staff_index->pedals.entries.at(id).record);
        }
        if (!add_pedal_spans(builder, system, staff, system_pedals)) {
          return fail(builder.error);
        }
        system.staves.push_back(std::move(staff));
        ++stave_ordinal;
      }
    }
    builder.output.commands.emplace_back(ClipCommand{
        make_id(system.id.value, "clip/end"), system.bounds, false});
    builder.output.systems.push_back(std::move(system));
    if (work != nullptr) {
      work->rebuilt_systems.push_back(first);
      for (std::size_t ordinal = first; ordinal < end; ++ordinal) {
        work->visited_measures.push_back(ordinal);
        work->rebuilt_measures.push_back(ordinal);
      }
    }
    if (retained != nullptr) {
      retained->push_back(
          SystemFragment{builder.output.systems.back(),
                         {builder.output.commands.begin() +
                              static_cast<std::ptrdiff_t>(command_begin),
                          builder.output.commands.end()},
                         {builder.output.hit_regions.begin() +
                              static_cast<std::ptrdiff_t>(hit_begin),
                          builder.output.hit_regions.end()},
                         {builder.output.diagnostics.begin() +
                              static_cast<std::ptrdiff_t>(diagnostic_begin),
                          builder.output.diagnostics.end()}});
    }
    system_y += system_height + options.system_gap;
  }

  const double total_height =
      ranges.empty() ? options.top_margin + options.bottom_margin
                     : system_y - options.system_gap + options.bottom_margin;
  builder.output.bounds = NotationRect{0.0, 0.0, maximum_width, total_height};
  if (!builder.output.geometry_is_finite() ||
      !geometry_is_bounded(builder.output)) {
    return fail(NotationLayoutError::kInvalidGeometry);
  }
  return NotationLayoutResult{NotationLayoutError::kNone,
                              std::move(builder.output)};
}

NotationLayoutResult layout_notation(const Project& project, NodeId node_id,
                                     const GlyphMetrics&          metrics,
                                     const NotationLayoutOptions& options) {
  const Node* node = project.find_node(node_id);
  if (node == nullptr) {
    return fail(NotationLayoutError::kNodeNotFound);
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return fail(NotationLayoutError::kTimelineMissing);
  }
  const LayoutIndex index = build_index(project, *node, timeline->measures(),
                                        metrics, options, nullptr);
  const std::vector<double> widths =
      measure_widths(timeline->measures(), index, metrics, options);
  return layout_internal(project, node_id, metrics, options, index, widths,
                         nullptr, {}, nullptr, nullptr);
}

struct NotationLayoutCache::Impl {
  std::vector<SystemFragment> fragments;
  std::optional<NodeTimeline> timeline;
  std::vector<std::string>    track_structure;
  NotationLayoutOptions       options;
  NodeId                      node_id;
  LayoutIndex                 index;
  std::vector<double>         cached_widths;
  bool                        initialized = false;
};

namespace {

[[nodiscard]] std::vector<std::string> track_structure(const Project& project,
                                                       const Node&    node) {
  std::vector<std::string> result;
  for (const Track& track : project.active_tracks()) {
    const TrackLane* const lane = node.lane(track.id());
    result.push_back(track.id().to_string());
    for (const StaveDefinition& stave : track.layout().staves()) {
      result.push_back(
          stave.id.to_string() + "/" +
          std::to_string(static_cast<int>(stave.default_clef)) + "/" +
          (lane != nullptr && lane->has_stave(stave.id) ? "1" : "0"));
    }
  }
  return result;
}

[[nodiscard]] bool invalidation_is_well_formed(
    const NotationInvalidation& invalidation, std::size_t measure_count) {
  switch (invalidation.kind) {
    case NotationInvalidationKind::kLocalContent:
    case NotationInvalidationKind::kCrossMeasureSpan:
      return invalidation.first_measure <= invalidation.last_measure &&
             invalidation.last_measure < measure_count;
    case NotationInvalidationKind::kContext:
    case NotationInvalidationKind::kMeasureStructure:
      return invalidation.first_measure < measure_count &&
             invalidation.last_measure == invalidation.first_measure;
    case NotationInvalidationKind::kTrackStaffArchive:
    case NotationInvalidationKind::kLayoutOptionsOrMetrics:
    case NotationInvalidationKind::kFullReset:
      return invalidation.first_measure == 0 && invalidation.last_measure == 0;
  }
}

void add_invalid_system(std::vector<std::size_t>& systems,
                        std::size_t               first_measure) {
  if (std::ranges::find(systems, first_measure) == systems.end()) {
    systems.push_back(first_measure);
  }
}

}  // namespace

NotationLayoutCache::NotationLayoutCache() : impl_(std::make_unique<Impl>()) {}

NotationLayoutCache::NotationLayoutCache(NotationLayoutCache&&) noexcept =
    default;

NotationLayoutCache& NotationLayoutCache::operator=(
    NotationLayoutCache&&) noexcept = default;

NotationLayoutCache::~NotationLayoutCache() = default;

IncrementalNotationLayoutResult NotationLayoutCache::update(
    const Project& project, NodeId node_id, const GlyphMetrics& metrics,
    const NotationLayoutOptions&             options,
    const std::vector<NotationInvalidation>& invalidations) {
  if (!options.valid()) {
    return {NotationLayoutError::kInvalidOptions, std::nullopt, {}};
  }
  const Node* const node = project.find_node(node_id);
  if (node == nullptr) {
    return {NotationLayoutError::kNodeNotFound, std::nullopt, {}};
  }
  const NodeTimeline* const timeline = node->timeline();
  if (timeline == nullptr) {
    return {NotationLayoutError::kTimelineMissing, std::nullopt, {}};
  }
  const std::size_t measure_count = timeline->measures().measure_count();
  if (!std::ranges::all_of(invalidations, [&](const auto& invalidation) {
        return invalidation_is_well_formed(invalidation, measure_count);
      })) {
    return {NotationLayoutError::kInvalidInvalidation, std::nullopt, {}};
  }

  NotationLayoutWork             work;
  const std::vector<std::string> structure = track_structure(project, *node);
  const bool                     timeline_change_declared = std::ranges::any_of(
      invalidations, [](const NotationInvalidation& invalidation) {
        return invalidation.kind == NotationInvalidationKind::kContext ||
               invalidation.kind ==
                   NotationInvalidationKind::kMeasureStructure ||
               invalidation.kind == NotationInvalidationKind::kFullReset;
      });
  bool full_reset = !impl_->initialized || impl_->node_id != node_id ||
                    impl_->options != options ||
                    impl_->track_structure != structure;
  if (impl_->initialized && impl_->timeline != *timeline &&
      !timeline_change_declared) {
    full_reset = true;
  }
  const bool structure_change = std::ranges::any_of(
      invalidations, [](const NotationInvalidation& invalidation) {
        return invalidation.kind == NotationInvalidationKind::kMeasureStructure;
      });
  if (std::ranges::any_of(
          invalidations, [](const NotationInvalidation& invalidation) {
            return invalidation.kind ==
                       NotationInvalidationKind::kTrackStaffArchive ||
                   invalidation.kind ==
                       NotationInvalidationKind::kLayoutOptionsOrMetrics ||
                   invalidation.kind == NotationInvalidationKind::kFullReset;
          })) {
    full_reset = true;
  }
  if (full_reset || structure_change) {
    impl_->index = build_index(project, *node, timeline->measures(), metrics,
                               options, &work);
    impl_->cached_widths =
        measure_widths(timeline->measures(), impl_->index, metrics, options);
  } else {
    const std::size_t count = timeline->measures().measure_count();
    for (const NotationInvalidation& invalidation : invalidations) {
      if (invalidation.kind == NotationInvalidationKind::kLocalContent ||
          invalidation.kind == NotationInvalidationKind::kCrossMeasureSpan) {
        refresh_index_range(
            project, *node, timeline->measures(), invalidation.first_measure,
            invalidation.last_measure, metrics, options, impl_->index, work);
      } else if (invalidation.kind == NotationInvalidationKind::kContext) {
        // Context changes shift measure boundaries; rebuild event assignments
        // and reference bucket membership for the suffix.
        if (count > 0 && invalidation.first_measure < count) {
          refresh_index_range(project, *node, timeline->measures(),
                              invalidation.first_measure, count - 1, metrics,
                              options, impl_->index, work);
        }
      }
    }
    // Recompute widths for affected measures.
    for (const NotationInvalidation& invalidation : invalidations) {
      if (invalidation.kind == NotationInvalidationKind::kLocalContent ||
          invalidation.kind == NotationInvalidationKind::kCrossMeasureSpan) {
        const std::size_t wfirst = invalidation.first_measure;
        const std::size_t wlast =
            std::min(invalidation.last_measure + 1, count - 1);
        for (std::size_t measure = wfirst; measure <= wlast; ++measure) {
          impl_->cached_widths[measure] = compute_measure_width(
              measure, timeline->measures(), impl_->index, metrics, options);
        }
      } else if (invalidation.kind == NotationInvalidationKind::kContext) {
        // Recompute widths for the suffix.
        for (std::size_t measure = invalidation.first_measure; measure < count;
             ++measure) {
          impl_->cached_widths[measure] = compute_measure_width(
              measure, timeline->measures(), impl_->index, metrics, options);
        }
      }
    }
  }

  // Context changes and other timeline mutations can affect measure widths
  // without requiring a full index rebuild.  When the cached timeline
  // differs from the current one, recompute all widths.
  if (!(full_reset || structure_change) && impl_->initialized &&
      impl_->timeline != *timeline) {
    impl_->cached_widths =
        measure_widths(timeline->measures(), impl_->index, metrics, options);
  }

  const auto ranges = system_ranges(
      impl_->cached_widths,
      options.system_width - options.left_margin - options.right_margin);
  std::vector<std::size_t> invalid_systems;
  const auto               invalidate_all = [&] {
    for (const auto& [first, end] : ranges) {
      (void)end;
      add_invalid_system(invalid_systems, first);
    }
  };
  const auto invalidate_intersection = [&](std::size_t first_measure,
                                           std::size_t last_measure) {
    for (const auto& [first, end] : ranges) {
      if (first <= last_measure && end > first_measure) {
        add_invalid_system(invalid_systems, first);
      }
    }
  };
  const auto invalidate_suffix = [&](std::size_t first_measure) {
    for (const auto& [first, end] : ranges) {
      if (end > first_measure) {
        add_invalid_system(invalid_systems, first);
      }
    }
  };

  for (const NotationInvalidation& invalidation : invalidations) {
    switch (invalidation.kind) {
      case NotationInvalidationKind::kLocalContent:
      case NotationInvalidationKind::kCrossMeasureSpan:
        invalidate_intersection(invalidation.first_measure,
                                invalidation.last_measure);
        break;
      case NotationInvalidationKind::kContext:
      case NotationInvalidationKind::kMeasureStructure:
        invalidate_suffix(invalidation.first_measure);
        break;
      case NotationInvalidationKind::kTrackStaffArchive:
      case NotationInvalidationKind::kLayoutOptionsOrMetrics:
      case NotationInvalidationKind::kFullReset:
        break;
    }
  }
  if (full_reset) {
    invalid_systems.clear();
    invalidate_all();
  }

  std::vector<SystemFragment> next_fragments;
  const auto                  result = layout_internal(
      project, node_id, metrics, options, impl_->index, impl_->cached_widths,
      impl_->initialized && !full_reset ? &impl_->fragments : nullptr,
      invalid_systems, &next_fragments, &work);
  if (!result) {
    return {result.error, std::nullopt, std::move(work)};
  }
  impl_->fragments       = std::move(next_fragments);
  impl_->timeline        = *timeline;
  impl_->track_structure = structure;
  impl_->options         = options;
  impl_->node_id         = node_id;
  impl_->initialized     = true;
  return {NotationLayoutError::kNone, result.layout, std::move(work)};
}

void NotationLayoutCache::reset() noexcept {
  impl_->fragments.clear();
  impl_->timeline.reset();
  impl_->track_structure.clear();
  impl_->cached_widths.clear();
  impl_->initialized = false;
}

std::vector<Articulation> NotePaletteState::armed_articulations() const {
  std::vector<Articulation> armed;
  for (const Articulation articulation : kAllArticulations) {
    if (has_articulation(articulation))
      armed.push_back(articulation);
  }
  return armed;
}

NotePaletteEntrySpec NotePaletteState::next_entry_spec() const {
  return NotePaletteEntrySpec{
      .duration      = resolved_duration(),
      .entry_kind    = entry_kind(),
      .voice         = voice(),
      .articulations = armed_articulations(),
      .dynamic       = dynamic(),
      .hairpin       = hairpin_direction(),
      .tie_to_next   = tie_to_next_armed(),
      .slur          = slur_armed(),
      .pedal         = pedal_armed(),
      .beam_override = beam_override_kind(),
  };
}

namespace {

// A system plus the staff `point` attributes to within it: shared by
// preview_note_entry and resolve_selection_at, both of which need exactly
// the same point -> staff attribution.
struct ResolvedStaffSite {
  const SystemLayout*      system = nullptr;
  const StaffSystemLayout* staff  = nullptr;
};

[[nodiscard]] std::optional<ResolvedStaffSite> resolve_staff_at(
    const NotationLayout& layout, NotationPoint point) {
  const SystemLayout* system = nullptr;
  for (const SystemLayout& candidate : layout.systems) {
    if (candidate.bounds.contains(point)) {
      system = &candidate;
      break;
    }
  }
  if (system == nullptr) {
    return std::nullopt;
  }

  // The nearest staff by vertical center, not strict containment: a click
  // in the ledger-line/marking lane above or below a staff's own five lines
  // must still resolve to that staff. But SystemLayout::bounds reserves a
  // large per-system marking budget (system_top_padding plus a
  // staff_slot_height per stave, up to 256 staff-spaces for a single-staff
  // system) so system containment alone is not a bounded proximity check --
  // a click far below every staff, still inside that oversized system
  // extent, must not silently attribute to the nearest one. kLedgerLaneSpaces
  // matches system_top_padding's own six-staff-space marking budget: the
  // window a click may fall in above/below a staff's own five lines and
  // still resolve to it.
  constexpr double         kLedgerLaneSpaces = 6.0;
  const StaffSystemLayout* staff             = nullptr;
  double staff_distance = std::numeric_limits<double>::infinity();
  for (const StaffSystemLayout& candidate : system->staves) {
    const double candidate_space = candidate.bounds.height / 4.0;
    const double allowed =
        candidate.bounds.height * 0.5 + kLedgerLaneSpaces * candidate_space;
    const double center   = candidate.bounds.y + candidate.bounds.height * 0.5;
    const double distance = std::abs(point.y - center);
    if (distance <= allowed && distance < staff_distance) {
      staff_distance = distance;
      staff          = &candidate;
    }
  }
  if (staff == nullptr) {
    return std::nullopt;
  }
  return ResolvedStaffSite{system, staff};
}

[[nodiscard]] const MeasureLayout* resolve_measure_at(
    const SystemLayout& system, NotationPoint point) {
  for (const MeasureLayout& candidate : system.measures) {
    if (point.x >= candidate.bounds.x &&
        point.x <= candidate.bounds.x + candidate.bounds.width) {
      return &candidate;
    }
  }
  return nullptr;
}

// The staff/measure/nearest-legal-onset `point` resolves to for `voice`:
// the resolution preview_note_entry previews and resolve_selection_at's
// insertion-caret arm snaps to, factored out so the two can never disagree
// about it.
struct ResolvedInsertionSite {
  const StaffSystemLayout* staff    = nullptr;
  const MeasureLayout*     measure  = nullptr;
  const NodeTimeline*      timeline = nullptr;
  Rational                 resolved_onset;
  // The lane/voice content the onset above was resolved against, so a
  // caller that must additionally check the onset against the domain's own
  // caret-legality rule (validate_insertion_caret_set,
  // graphscore/domain/selection.cpp) can do so without re-deriving them
  // from `project`. preview_note_entry, the other caller, never reads
  // these two fields, so adding them changes nothing about its behavior.
  const TrackLane*    lane          = nullptr;
  const VoiceContent* voice_content = nullptr;
};

[[nodiscard]] std::optional<ResolvedInsertionSite> resolve_insertion_site(
    const Project& project, const NotationLayout& layout, Voice voice,
    NotationPoint point) {
  const std::optional<ResolvedStaffSite> site = resolve_staff_at(layout, point);
  if (!site.has_value()) {
    return std::nullopt;
  }
  const MeasureLayout* measure = resolve_measure_at(*site->system, point);
  if (measure == nullptr) {
    return std::nullopt;
  }

  const Node* node = project.find_node(layout.node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return std::nullopt;
  }
  const MeasureMap& measures = timeline->measures();
  if (measure->ordinal >= measures.measure_count()) {
    return std::nullopt;
  }
  const TrackLane* lane = node->lane(site->staff->track_id);
  if (lane == nullptr) {
    return std::nullopt;
  }
  const StaveVoices* voices = lane->stave(site->staff->stave_id);
  if (voices == nullptr) {
    return std::nullopt;
  }
  const VoiceContent& content = voices->voice(voice);

  const Rational measure_start  = measures.measure_start(measure->ordinal);
  const Rational measure_length = measures.measure_length(measure->ordinal);
  const double   staff_space    = site->staff->bounds.height / 4.0;
  const double   reference_time =
      time_at_x(measures, measure->ordinal, point.x, measure->bounds.x,
                measure->bounds.width, staff_space);

  // The durations to scan for onsets: `voice`'s own events, unless it is
  // entirely empty, in which case the voice-stream workflow resolves
  // against the durations of the same measure-aligned rest fill a click
  // would materialize (decompose_measure_aligned_rests, voice_content.hpp).
  // A voice that merely has no event boundary within the resolved measure
  // (but is not itself empty) is not covered by this substitution and
  // still yields std::nullopt below via an unmatched scan.
  std::vector<Rational> event_durations;
  if (content.events().empty()) {
    // Reads only the hypothetical fill's shape, so the duration-only core
    // (decompose_measure_aligned_rest_durations, voice_content.hpp) is used
    // directly rather than decompose_measure_aligned_rests: every pointer
    // move through an empty voice would otherwise mint and immediately
    // discard a fresh Rest id per term.
    const std::optional<std::vector<Duration>> hypothetical_fill =
        decompose_measure_aligned_rest_durations(*timeline);
    if (!hypothetical_fill.has_value()) {
      return std::nullopt;
    }
    event_durations.reserve(hypothetical_fill->size());
    for (const Duration& duration : *hypothetical_fill) {
      event_durations.push_back(duration.resolved());
    }
  } else {
    event_durations.reserve(content.events().size());
    for (const VoiceEvent& event : content.events()) {
      event_durations.push_back(event_duration(event).resolved());
    }
  }

  // Snaps to the start of an existing rhythmic event (including a
  // normalized rest, real or hypothetical) in `voice`, scoped to the
  // resolved measure -- never a metric grid derived from an armed duration.
  // Onsets are visited in order, and only a strictly smaller distance
  // replaces the current best, so on an exact tie the earlier onset wins
  // deterministically.
  bool     found_onset   = false;
  double   best_distance = std::numeric_limits<double>::infinity();
  Rational resolved_onset;
  Rational onset;
  for (const Rational& event_dur : event_durations) {
    if (onset >= measure_start && onset < measure_start + measure_length) {
      const double distance = std::abs(onset.to_double() - reference_time);
      if (distance < best_distance) {
        best_distance  = distance;
        resolved_onset = onset;
        found_onset    = true;
      }
    }
    onset = onset + event_dur;
  }
  if (!found_onset) {
    return std::nullopt;
  }

  return ResolvedInsertionSite{site->staff,    measure, timeline,
                               resolved_onset, lane,    &content};
}

}  // namespace

std::optional<NotationPreview> preview_note_entry(
    const Project& project, const NotationLayout& layout,
    const NotePaletteState& palette, NotationPoint point) {
  if (!finite_point(point)) {
    return std::nullopt;
  }
  const std::optional<ResolvedInsertionSite> site =
      resolve_insertion_site(project, layout, palette.voice(), point);
  if (!site.has_value()) {
    return std::nullopt;
  }
  const StaffSystemLayout* staff          = site->staff;
  const MeasureLayout*     measure        = site->measure;
  const NodeTimeline*      timeline       = site->timeline;
  const Rational           resolved_onset = site->resolved_onset;
  const double             staff_space    = staff->bounds.height / 4.0;

  const ClefLane* clef_lane = timeline->clef_lane(staff->stave_id);
  const Clef      clef =
      clef_lane == nullptr ? Clef::kTreble : clef_lane->clef_at(resolved_onset);

  const double glyph_x =
      position_x(timeline->measures(), measure->ordinal, measure->bounds.width,
                 resolved_onset, measure->bounds.x, staff_space);

  NotationPreview preview;
  preview.track_id        = staff->track_id;
  preview.stave_id        = staff->stave_id;
  preview.voice           = palette.voice();
  preview.entry_kind      = palette.entry_kind();
  preview.candidate_onset = resolved_onset;

  double glyph_y = 0.0;
  if (palette.entry_kind() == NotePaletteEntryKind::kNote) {
    const std::optional<SpelledPitch> candidate_pitch =
        spelled_pitch_at(point.y, clef, staff->bounds.y, staff_space);
    if (!candidate_pitch.has_value()) {
      return std::nullopt;
    }
    preview.candidate_pitch = candidate_pitch;
    glyph_y = pitch_y(*candidate_pitch, clef, staff->bounds.y, staff_space);
  } else {
    glyph_y = event_y(palette.voice(), staff->bounds.y, staff_space);
  }
  if (!bounded_point({glyph_x, glyph_y})) {
    return std::nullopt;
  }

  const NoteValue base_value = palette.resolved_duration().base();
  const bool      is_note = palette.entry_kind() == NotePaletteEntryKind::kNote;
  const char32_t  code_point = smufl_codepoint(
      is_note ? notehead_glyph(base_value) : rest_glyph(base_value));
  preview.commands.emplace_back(
      GlyphCommand{make_id("preview", is_note ? "notehead" : "rest"),
                   code_point,
                   {glyph_x, glyph_y},
                   staff_space});
  for (std::uint8_t dot = 0; dot < palette.dots(); ++dot) {
    preview.commands.emplace_back(
        GlyphCommand{make_id("preview", "dot/" + std::to_string(dot)),
                     smufl_codepoint(SmuflGlyph::kAugmentationDot),
                     {glyph_x + staff_space * (1.2 + dot * 0.65),
                      glyph_y - staff_space * 0.25},
                     staff_space});
  }
  return preview;
}

namespace {

// What kind of domain entity a hit's semantic id names within one voice's
// content, plus the actual matched id -- see find_entity_in_voice.
enum class ResolvedEntityKind : std::uint8_t {
  kNone,
  kNote,
  kChord,
  kRest,
  kChordNote,
  kGraceNote,
};

struct ResolvedEntity {
  ResolvedEntityKind kind = ResolvedEntityKind::kNone;
  NotationEntityId   id;
  // The owning top-level VoiceEvent (the Note/Chord/Rest itself for
  // kNote/kChord/kRest, the parent Chord for kChordNote), or nullptr for
  // kGraceNote (GraceNote is not a VoiceEvent) or kNone. Only the
  // kMarking-hit callers below (articulation, tuplet) need this; the
  // pre-existing kNotehead/kEvent path never reads it. Points into the
  // VoiceContent `find_entity_in_voice` scanned, which outlives the single
  // resolve_selection_at call this is used within.
  const VoiceEvent* event = nullptr;
};

// Scoped, bounded lookup of the entity `target` (a HitRegion::semantic_id's
// string value) names within one voice's own content: a linear scan of
// that one voice's own events/chord notes/grace notes, never the whole
// project. There is no way to parse a NotationEntityId back out of that
// string -- StrongId/Uuid (graphscore/core) expose no from_string()/parse()
// -- so this instead walks the actual ids the voice already owns and
// compares each one's own to_string() against `target`, returning that
// found id directly rather than a reconstructed one.
[[nodiscard]] ResolvedEntity find_entity_in_voice(const VoiceContent& voice,
                                                  const std::string&  target) {
  for (const VoiceEvent& event : voice.events()) {
    if (event_id(event).to_string() == target) {
      if (std::holds_alternative<Note>(event)) {
        return ResolvedEntity{ResolvedEntityKind::kNote, event_id(event),
                              &event};
      }
      if (std::holds_alternative<Chord>(event)) {
        return ResolvedEntity{ResolvedEntityKind::kChord, event_id(event),
                              &event};
      }
      return ResolvedEntity{ResolvedEntityKind::kRest, event_id(event), &event};
    }
    if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const ChordNote& chord_note : chord->notes) {
        if (chord_note.id.to_string() == target) {
          return ResolvedEntity{ResolvedEntityKind::kChordNote, chord_note.id,
                                &event};
        }
      }
    }
  }
  for (const GraceGroup& group : voice.grace_groups()) {
    for (const GraceNote& grace : group.notes) {
      if (grace.id.to_string() == target) {
        return ResolvedEntity{ResolvedEntityKind::kGraceNote, grace.id};
      }
    }
  }
  return ResolvedEntity{};
}

// The one voice (of its owning staff's small, fixed set) that actually owns
// a kNotehead/kEvent/kMarking hit's semantic entity, plus that entity's
// resolved kind/id and the VoiceContent it was found in (kMarking's
// articulation/tuplet resolution needs the owning voice's full event list,
// not just the one matched entity).
struct ResolvedVoiceEntity {
  const StaffSystemLayout* staff = nullptr;
  Voice                    voice;
  ResolvedEntity           entity;
  const VoiceContent*      content = nullptr;
};

// Attributes `hit` to the staff that actually owns its semantic entity, by
// scanning the layout's own staves rather than re-deriving a staff from the
// click point: `hit_test` already returned the correct HitResult, including
// for a ledger-line notehead engraved outside resolve_staff_at's own
// proximity window (that window is a blank-click heuristic, not a bound on
// how far a real notehead may be engraved from its staff's center), so
// re-deriving the staff from the point instead of trusting `hit` can
// discard an otherwise-correct hit. Entity IDs are unique, so scanning
// every staff this layout has (bounded: systems x staves x StaveVoices'
// fixed four voices, never a project-wide scan) for the one voice whose
// content owns hit.semantic_id.value is authoritative rather than a guess.
[[nodiscard]] std::optional<ResolvedVoiceEntity> resolve_hit_entity(
    const Project& project, const NotationLayout& layout,
    const HitResult& hit) {
  const Node* node = project.find_node(layout.node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  for (const SystemLayout& system : layout.systems) {
    for (const StaffSystemLayout& staff : system.staves) {
      const TrackLane* lane = node->lane(staff.track_id);
      if (lane == nullptr) {
        continue;
      }
      const StaveVoices* voices = lane->stave(staff.stave_id);
      if (voices == nullptr) {
        continue;
      }
      for (const VoiceLayout& voice_layout : staff.voices) {
        const VoiceContent&  content = voices->voice(voice_layout.voice);
        const ResolvedEntity found =
            find_entity_in_voice(content, hit.semantic_id.value);
        if (found.kind != ResolvedEntityKind::kNone) {
          return ResolvedVoiceEntity{&staff, voice_layout.voice, found,
                                     &content};
        }
      }
    }
  }
  return std::nullopt;
}

// True if `id` (a HitResult::id/HitRegion::id) was built by
// make_id(<owning entity>, std::string{suffix}) and then wrapped through
// add_glyph/add_hit's own trailing "/hit" -- i.e. `id` names exactly the
// region convention `suffix` names, not some other region that happens to
// share a semantic_id with it. The match is anchored at a "/" path
// boundary immediately before `suffix`, not a bare substring match: a bare
// "ends with <suffix>/hit" test would let e.g. kHitSuffixGraceNotehead
// ("grace-notehead") also satisfy kHitSuffixNotehead ("notehead"), since
// "grace-notehead" itself ends with "notehead".
[[nodiscard]] bool hit_id_ends_with(const NotationId& id,
                                    std::string_view  suffix) {
  const std::string expected = "/" + std::string(suffix) + "/hit";
  return id.value.size() >= expected.size() &&
         id.value.compare(id.value.size() - expected.size(), expected.size(),
                          expected) == 0;
}

// Parses a hit id built as make_id(<anchor>, "<prefix>/" +
// std::to_string(n)) and then wrapped through add_glyph's own trailing
// "/hit" -- the shape kHitRoleArticulation ("entity/articulation/N/hit")
// and kHitRoleTupletDigit ("id/tuplet/digit/N/hit") share, a fixed role
// prefix followed by one numeric path segment. Returns that segment's
// value, or std::nullopt on any shape mismatch -- missing "/hit" trailer,
// no numeric segment immediately before it, a non-numeric or otherwise
// unparsable segment (e.g. leading '+'/'-', or a value too large for
// std::size_t), or a prefix that does not end exactly at a "/" path
// boundary. Never a clamped guess: a stale or hand-built layout with a
// malformed id must fail resolution outright, matching
// resolve_selection_at's existing hit_id_ends_with-guarded checks.
[[nodiscard]] std::optional<std::size_t> hit_id_numeric_suffix(
    const NotationId& id, std::string_view prefix) {
  constexpr std::string_view kTrailer = "/hit";
  if (id.value.size() < kTrailer.size() ||
      id.value.compare(id.value.size() - kTrailer.size(), kTrailer.size(),
                       kTrailer) != 0) {
    return std::nullopt;
  }
  const std::string_view body(id.value.data(),
                              id.value.size() - kTrailer.size());
  const std::size_t      last_slash = body.rfind('/');
  if (last_slash == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view digits = body.substr(last_slash + 1);
  if (digits.empty() || !std::ranges::all_of(digits, [](char c) {
        return c >= '0' && c <= '9';
      })) {
    return std::nullopt;
  }
  const std::string_view head     = body.substr(0, last_slash);
  const std::string      expected = "/" + std::string(prefix);
  if (head.size() < expected.size() ||
      head.compare(head.size() - expected.size(), expected.size(), expected) !=
          0) {
    return std::nullopt;
  }
  std::size_t value = 0;
  const auto [ptr, ec] =
      std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (ec != std::errc() || ptr != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return value;
}

// True if `id` was built by add_span_segment(builder, ..., role ==
// kHitRoleTie, ...) and then add_hit's own trailing "/hit" -- i.e. `id`
// matches ".../tie/segment/system-<N>/hit" at path boundaries, for some
// non-negative integer N (the fragment's own first_measure). This resolver
// has no use for N itself, only for the "is this a tie segment" fact, so
// unlike hit_id_numeric_suffix this returns bool rather than the parsed
// value.
[[nodiscard]] bool hit_id_is_tie_segment(const NotationId& id) {
  constexpr std::string_view kTrailer = "/hit";
  if (id.value.size() < kTrailer.size() ||
      id.value.compare(id.value.size() - kTrailer.size(), kTrailer.size(),
                       kTrailer) != 0) {
    return false;
  }
  const std::string_view body(id.value.data(),
                              id.value.size() - kTrailer.size());
  const std::size_t      last_slash = body.rfind('/');
  if (last_slash == std::string_view::npos) {
    return false;
  }
  const std::string_view     last_segment  = body.substr(last_slash + 1);
  constexpr std::string_view kSystemPrefix = "system-";
  if (last_segment.size() <= kSystemPrefix.size() ||
      last_segment.compare(0, kSystemPrefix.size(), kSystemPrefix) != 0) {
    return false;
  }
  const std::string_view digits = last_segment.substr(kSystemPrefix.size());
  if (!std::ranges::all_of(digits,
                           [](char c) { return c >= '0' && c <= '9'; })) {
    return false;
  }
  const std::string_view head     = body.substr(0, last_slash);
  const std::string      expected = "/" + std::string(kHitRoleTie) + "/segment";
  return head.size() >= expected.size() &&
         head.compare(head.size() - expected.size(), expected.size(),
                      expected) == 0;
}

// One of the four record-backed kMarking kinds (dynamic, hairpin, slur,
// pedal span), resolved by looking `target` (a HitRegion::semantic_id's
// string value, which for these four kinds *is* the record's own id) up
// against the actual records the layout's node owns -- never by inspecting
// the hit id's own string, which is how the id-less kinds below are told
// apart instead. Dynamic/hairpin/slur/pedal span ids occupy the same
// project-wide NotationEntityId space as every other entity, but this
// lookup only ever searches the four collections each kind actually lives
// in, so a match is unambiguous.
struct ResolvedMarkingRecord {
  const StaffSystemLayout* staff = nullptr;
  std::optional<Voice>     voice;
  MarkingKind              kind = MarkingKind::kDynamic;
  NotationEntityId         anchor;
};

[[nodiscard]] std::optional<ResolvedMarkingRecord> resolve_marking_record(
    const Project& project, const NotationLayout& layout,
    const std::string& target) {
  const Node* node = project.find_node(layout.node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  for (const SystemLayout& system : layout.systems) {
    for (const StaffSystemLayout& staff : system.staves) {
      const TrackLane* lane = node->lane(staff.track_id);
      if (lane == nullptr) {
        continue;
      }
      if (const std::vector<PedalSpan>* spans =
              lane->pedal_spans(staff.stave_id)) {
        for (const PedalSpan& span : *spans) {
          if (span.id.to_string() == target) {
            return ResolvedMarkingRecord{&staff, std::nullopt,
                                         MarkingKind::kPedalSpan, span.id};
          }
        }
      }
      const StaveVoices* voices = lane->stave(staff.stave_id);
      if (voices == nullptr) {
        continue;
      }
      for (const VoiceLayout& voice_layout : staff.voices) {
        const VoiceContent& content = voices->voice(voice_layout.voice);
        for (const DynamicMarking& dynamic : content.dynamics()) {
          if (dynamic.id.to_string() == target) {
            return ResolvedMarkingRecord{&staff, voice_layout.voice,
                                         MarkingKind::kDynamic, dynamic.id};
          }
        }
        for (const Hairpin& hairpin : content.hairpins()) {
          if (hairpin.id.to_string() == target) {
            return ResolvedMarkingRecord{&staff, voice_layout.voice,
                                         MarkingKind::kHairpin, hairpin.id};
          }
        }
        for (const Slur& slur : content.slurs()) {
          if (slur.id.to_string() == target) {
            return ResolvedMarkingRecord{&staff, voice_layout.voice,
                                         MarkingKind::kSlur, slur.id};
          }
        }
      }
    }
  }
  return std::nullopt;
}

// The cross-system tuplet hazard: the engraver's own per-system fragment
// scan (see the tuplet-digit emission above) prepends only the single
// immediately-preceding measure's *last* event as lookback context, so a
// tuplet run that began earlier than that can have its bracket/digits
// anchored to a mid-run event rather than the run's true first event --
// exactly what validate_selection's kTuplet check rejects
// (kMarkingNotPresent, "event is not the first event of its tuplet run").
// This walks `voice`'s own full, unfragmented event list backward from
// `anchor` while the preceding event carries an equal TupletRatio, landing
// on the true first event regardless of which system the click happened
// in. Returns std::nullopt if `anchor` does not name a voice event, or
// names one with no tuplet ratio at all (a stale layout).
[[nodiscard]] std::optional<NotationEntityId> normalize_tuplet_anchor(
    const VoiceContent& voice, NotationEntityId anchor) {
  const std::vector<VoiceEvent>& events = voice.events();
  std::optional<std::size_t>     index;
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (event_id(events[i]) == anchor) {
      index = i;
      break;
    }
  }
  if (!index.has_value()) {
    return std::nullopt;
  }
  const std::optional<TupletRatio> ratio =
      event_duration(events[*index]).tuplet();
  if (!ratio.has_value()) {
    return std::nullopt;
  }
  while (*index > 0) {
    const std::optional<TupletRatio> previous =
        event_duration(events[*index - 1]).tuplet();
    if (!previous.has_value() || *previous != *ratio) {
      break;
    }
    --*index;
  }
  return event_id(events[*index]);
}

// Resolves a kMarking hit to its single MarkingSet selection, covering all
// seven MarkingKinds: docs/plan/05-notation-editor.md's "Resolve marking
// hit regions to dynamic, hairpin, slur, pedal span, articulation, tie, and
// tuplet selections." See resolve_marking_record's own comment for the
// four record-backed kinds, and hit_id_numeric_suffix/
// hit_id_is_tie_segment's for the three id-less ones. Returns std::nullopt
// whenever the named marking cannot actually be found or no longer carries
// the shape its kind requires -- a stale layout -- rather than a Selection
// validate_selection would reject.
[[nodiscard]] std::optional<Selection> resolve_marking_selection(
    const Project& project, const NotationLayout& layout,
    const HitResult& hit) {
  if (const std::optional<ResolvedMarkingRecord> record =
          resolve_marking_record(project, layout, hit.semantic_id.value);
      record.has_value()) {
    std::optional<MarkingSet> set = MarkingSet::create({MarkingItem{
        layout.node_id, record->staff->track_id, record->staff->stave_id,
        record->voice, record->kind, record->anchor, std::nullopt}});
    if (!set.has_value()) {
      return std::nullopt;
    }
    return Selection{*std::move(set)};
  }

  const std::optional<ResolvedVoiceEntity> resolved =
      resolve_hit_entity(project, layout, hit);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  const ResolvedEntity& entity = resolved->entity;
  const NodeId          node   = layout.node_id;
  const TrackId         track  = resolved->staff->track_id;
  const StaveId         stave  = resolved->staff->stave_id;
  const Voice           voice  = resolved->voice;

  if (const std::optional<std::size_t> index =
          hit_id_numeric_suffix(hit.id, kHitRoleArticulation);
      index.has_value()) {
    if ((entity.kind != ResolvedEntityKind::kNote &&
         entity.kind != ResolvedEntityKind::kChord) ||
        entity.event == nullptr) {
      return std::nullopt;
    }
    const std::vector<Articulation>* articulations =
        event_articulations(*entity.event);
    if (articulations == nullptr || *index >= articulations->size()) {
      return std::nullopt;
    }
    std::optional<MarkingSet> set = MarkingSet::create(
        {MarkingItem{node, track, stave, voice, MarkingKind::kArticulation,
                     entity.id, (*articulations)[*index]}});
    if (!set.has_value()) {
      return std::nullopt;
    }
    return Selection{*std::move(set)};
  }

  if (hit_id_is_tie_segment(hit.id)) {
    if (entity.kind != ResolvedEntityKind::kNote &&
        entity.kind != ResolvedEntityKind::kChordNote) {
      return std::nullopt;
    }
    if (entity.event == nullptr) {
      return std::nullopt;
    }
    const std::optional<bool> tied = std::visit(
        [&](const auto& concrete) -> std::optional<bool> {
          using Event = std::decay_t<decltype(concrete)>;
          if constexpr (std::is_same_v<Event, Note>) {
            return concrete.id == entity.id
                       ? std::optional<bool>{concrete.tied_to_next}
                       : std::nullopt;
          } else if constexpr (std::is_same_v<Event, Chord>) {
            const auto found =
                std::ranges::find(concrete.notes, entity.id, &ChordNote::id);
            return found == concrete.notes.end()
                       ? std::nullopt
                       : std::optional<bool>{found->tied_to_next};
          } else {
            return std::nullopt;
          }
        },
        *entity.event);
    if (!tied.has_value() || !*tied) {
      return std::nullopt;
    }
    std::optional<MarkingSet> set = MarkingSet::create(
        {MarkingItem{node, track, stave, voice, MarkingKind::kTie, entity.id,
                     std::nullopt}});
    if (!set.has_value()) {
      return std::nullopt;
    }
    return Selection{*std::move(set)};
  }

  if (hit_id_numeric_suffix(hit.id, kHitRoleTupletDigit).has_value()) {
    if ((entity.kind != ResolvedEntityKind::kNote &&
         entity.kind != ResolvedEntityKind::kChord &&
         entity.kind != ResolvedEntityKind::kRest) ||
        resolved->content == nullptr) {
      return std::nullopt;
    }
    const std::optional<NotationEntityId> normalized =
        normalize_tuplet_anchor(*resolved->content, entity.id);
    if (!normalized.has_value()) {
      return std::nullopt;
    }
    std::optional<MarkingSet> set = MarkingSet::create(
        {MarkingItem{node, track, stave, voice, MarkingKind::kTuplet,
                     *normalized, std::nullopt}});
    if (!set.has_value()) {
      return std::nullopt;
    }
    return Selection{*std::move(set)};
  }

  return std::nullopt;
}

// The project-wide score order resolve_range_selection's staff-range rule
// uses: Project::active_tracks() order, then each track's own
// StaffLayout::staves() order -- the identical order layout_notation itself
// assigns to every system's own StaffSystemLayout list (see its own
// "std::size_t stave_ordinal" loop above), so every system carries this
// exact same ordered staff set project-wide.
[[nodiscard]] std::vector<std::pair<TrackId, StaveId>> score_ordered_staves(
    const Project& project) {
  std::vector<std::pair<TrackId, StaveId>> order;
  for (const Track& track : project.active_tracks()) {
    for (const StaveDefinition& stave_definition : track.layout().staves()) {
      order.emplace_back(track.id(), stave_definition.id);
    }
  }
  return order;
}

// The grid resolve_range_selection quantizes a raw pixel-derived musical
// time to -- see that function's own contract comment
// (graphscore/notation/graphscore_notation.hpp) for the rationale and this
// grid's limits.
constexpr std::int64_t kRangeSelectionGridDenominator = 192;

[[nodiscard]] Rational quantize_range_time(double time) noexcept {
  const double scaled =
      time * static_cast<double>(kRangeSelectionGridDenominator);
  const auto numerator = static_cast<std::int64_t>(std::llround(scaled));
  return Rational(numerator) / Rational(kRangeSelectionGridDenominator);
}

// Whether `content` has any event whose own [onset, onset + duration)
// extent overlaps the half-open `span` -- resolve_range_selection's
// content-driven substitute for voice geometry, which VoiceLayout does not
// carry (see its own comment in the public header).
[[nodiscard]] bool voice_overlaps_span(const VoiceContent& content,
                                       const MusicalSpan&  span) {
  Rational onset;
  for (const VoiceEvent& event : content.events()) {
    const Rational event_end = onset + event_duration(event).resolved();
    if (onset < span.end && event_end > span.start) {
      return true;
    }
    onset = event_end;
  }
  return false;
}

}  // namespace

std::optional<Selection> resolve_range_selection(const Project&        project,
                                                 const NotationLayout& layout,
                                                 NotationPoint         anchor,
                                                 NotationPoint         focus) {
  if (!finite_point(anchor) || !finite_point(focus)) {
    return std::nullopt;
  }

  const std::optional<ResolvedStaffSite> anchor_site =
      resolve_staff_at(layout, anchor);
  const std::optional<ResolvedStaffSite> focus_site =
      resolve_staff_at(layout, focus);
  if (!anchor_site.has_value() || !focus_site.has_value()) {
    return std::nullopt;
  }

  const MeasureLayout* anchor_measure =
      resolve_measure_at(*anchor_site->system, anchor);
  const MeasureLayout* focus_measure =
      resolve_measure_at(*focus_site->system, focus);
  if (anchor_measure == nullptr || focus_measure == nullptr) {
    return std::nullopt;
  }

  const Node* node = project.find_node(layout.node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return std::nullopt;
  }
  const MeasureMap& measures = timeline->measures();
  if (anchor_measure->ordinal >= measures.measure_count() ||
      focus_measure->ordinal >= measures.measure_count()) {
    return std::nullopt;
  }

  const double anchor_staff_space = anchor_site->staff->bounds.height / 4.0;
  const double focus_staff_space  = focus_site->staff->bounds.height / 4.0;
  const double anchor_time        = time_at_x(
      measures, anchor_measure->ordinal, anchor.x, anchor_measure->bounds.x,
      anchor_measure->bounds.width, anchor_staff_space);
  const double focus_time = time_at_x(
      measures, focus_measure->ordinal, focus.x, focus_measure->bounds.x,
      focus_measure->bounds.width, focus_staff_space);

  const Rational start = quantize_range_time(std::min(anchor_time, focus_time));
  const Rational end   = quantize_range_time(std::max(anchor_time, focus_time));
  if (!(start < end)) {
    return std::nullopt;
  }
  const MusicalSpan span{start, end};

  const std::vector<std::pair<TrackId, StaveId>> score_order =
      score_ordered_staves(project);
  const auto anchor_position = std::ranges::find(
      score_order,
      std::pair{anchor_site->staff->track_id, anchor_site->staff->stave_id});
  const auto focus_position = std::ranges::find(
      score_order,
      std::pair{focus_site->staff->track_id, focus_site->staff->stave_id});
  if (anchor_position == score_order.end() ||
      focus_position == score_order.end()) {
    return std::nullopt;
  }
  const auto [lower, upper] = std::minmax(anchor_position, focus_position);

  std::vector<ArbitraryRangeItem> items;
  for (auto it = lower; it <= upper; ++it) {
    const auto [track_id, stave_id] = *it;
    const TrackLane* lane           = node->lane(track_id);
    // Unlike resolve_insertion_site's identical two checks, a staff in the
    // resolved range whose own TrackLane/StaveVoices cannot be found is
    // skipped rather than failing the whole query: a lane==nullptr staff is
    // unreachable for any layout satisfying this function's own contract
    // (layout_notation hard-fails with NotationLayoutError::kLaneMissing
    // before producing such a layout), and voices==nullptr is semantically
    // empty (layout_notation treats a missing StaveVoices as
    // kEmptyVoices), so skipping this one staff is equivalent to scanning
    // its four voices and finding no overlapping content in any of them.
    if (lane == nullptr) {
      continue;
    }
    const StaveVoices* voices = lane->stave(stave_id);
    if (voices == nullptr) {
      continue;
    }
    for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
         ++voice_index) {
      // Voice::create fails only when index < kMin || index > kMax
      // (graphscore/core/voice.hpp), and this loop's own bounds keep
      // voice_index within [kMin, kMax] throughout, so this can never fail.
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
      const Voice         voice   = *Voice::create(voice_index);
      const VoiceContent& content = voices->voice(voice);
      if (!voice_overlaps_span(content, span)) {
        continue;
      }
      items.push_back(
          ArbitraryRangeItem{layout.node_id, track_id, stave_id, voice, span});
    }
  }

  std::optional<ArbitraryRangeSet> set =
      ArbitraryRangeSet::create(std::move(items));
  if (!set.has_value()) {
    return std::nullopt;
  }
  return Selection{*std::move(set)};
}

std::optional<Selection> resolve_selection_at(const Project&          project,
                                              const NotationLayout&   layout,
                                              const NotePaletteState& palette,
                                              NotationPoint           point) {
  if (!finite_point(point)) {
    return std::nullopt;
  }

  const std::optional<HitResult> hit = layout.hit_test(point);
  if (hit.has_value()) {
    if (hit->role == HitRole::kMarking) {
      return resolve_marking_selection(project, layout, *hit);
    }
    if (hit->role == HitRole::kNotehead || hit->role == HitRole::kEvent) {
      const std::optional<ResolvedVoiceEntity> resolved =
          resolve_hit_entity(project, layout, *hit);
      if (!resolved.has_value()) {
        return std::nullopt;
      }
      const ResolvedEntity& entity = resolved->entity;

      // Defends against a future emitter silently drifting from the
      // resolution logic above: each of these checks fails safe (returns
      // std::nullopt) rather than trusting a hit whose id's own naming
      // convention disagrees with what it resolved to, e.g. a kNotehead-role
      // region whose id was not actually built from one of the two
      // notehead-emitting suffixes, or a "stem" region that somehow
      // resolved to something other than the Note/Chord a stem is drawn
      // for. This is deliberately a defensive `if`, not `assert`: an
      // NDEBUG-only caller would otherwise make hit_id_ends_with itself
      // dead code (and so, an unused-function warning) in a release build.
      if (hit->role == HitRole::kNotehead &&
          !hit_id_ends_with(hit->id, kHitSuffixNotehead) &&
          !hit_id_ends_with(hit->id, kHitSuffixGraceNotehead)) {
        return std::nullopt;
      }
      if (hit_id_ends_with(hit->id, kHitSuffixStem) &&
          entity.kind != ResolvedEntityKind::kNote &&
          entity.kind != ResolvedEntityKind::kChord) {
        return std::nullopt;
      }
      if (hit_id_ends_with(hit->id, kHitSuffixRest) &&
          entity.kind != ResolvedEntityKind::kRest) {
        return std::nullopt;
      }

      const NodeId  node  = layout.node_id;
      const TrackId track = resolved->staff->track_id;
      const StaveId stave = resolved->staff->stave_id;
      const Voice   voice = resolved->voice;

      switch (entity.kind) {
        case ResolvedEntityKind::kNote:
        case ResolvedEntityKind::kChordNote:
        case ResolvedEntityKind::kGraceNote: {
          // A single top-level Note has no notehead identity distinct from
          // its own id, so its stem/dot/accidental (kEvent, not kNotehead)
          // selects the same one notehead a direct kNotehead hit on it
          // would. A ChordNote's own stem/dot/accidental (the chord's, or
          // that one notehead's) likewise selects just that notehead --
          // Chord-level markings belong to the ChordSet arm below instead.
          std::optional<NoteheadSet> set = NoteheadSet::create(
              {NoteheadItem{node, track, stave, voice, entity.id}});
          if (!set.has_value()) {
            return std::nullopt;
          }
          return Selection{*std::move(set)};
        }
        case ResolvedEntityKind::kChord: {
          std::optional<ChordSet> set = ChordSet::create(
              {ChordItem{node, track, stave, voice, entity.id}});
          if (!set.has_value()) {
            return std::nullopt;
          }
          return Selection{*std::move(set)};
        }
        case ResolvedEntityKind::kRest: {
          std::optional<RestSet> set =
              RestSet::create({RestItem{node, track, stave, voice, entity.id}});
          if (!set.has_value()) {
            return std::nullopt;
          }
          return Selection{*std::move(set)};
        }
        case ResolvedEntityKind::kNone:
          return std::nullopt;
      }
    }
    // Every ResolvedEntityKind enumerator the switch above covers already
    // returns, so only a hit whose role is none of kNotehead/kEvent/
    // kMarking (kSystem, kMeasure, kStaff, kVoice, kStaffMeasure today)
    // reaches here and falls through to insertion-caret resolution below,
    // using the point the hit occurred at.
  }

  const std::optional<ResolvedInsertionSite> site =
      resolve_insertion_site(project, layout, palette.voice(), point);
  if (!site.has_value()) {
    return std::nullopt;
  }
  // The caret's legality is the domain's own rule
  // (validate_insertion_caret_set, graphscore/domain/selection.cpp): position
  // 0, TrackLane::total_length(), or an existing event boundary in the
  // *armed* voice. resolve_insertion_site's onset is not automatically any
  // of these -- when the armed voice is empty, the onset it snaps to comes
  // from a hypothetical measure-aligned rest fill (see that function's own
  // comment), which is a real onset for a preview but not necessarily one
  // the domain accepts as a caret before that fill is actually materialized.
  // Rejecting an illegal onset here, rather than trusting it, is what keeps
  // every Selection this function returns satisfying
  // validate_selection(...).empty().
  const Rational lane_end = site->lane->total_length();
  if (site->resolved_onset != Rational(0) && site->resolved_onset != lane_end &&
      !site->voice_content->find_event_index_at(site->resolved_onset)
           .has_value()) {
    return std::nullopt;
  }
  std::optional<InsertionCaretSet> set =
      InsertionCaretSet::create({InsertionCaretItem{
          layout.node_id, site->staff->track_id, site->staff->stave_id,
          palette.voice(), site->resolved_onset}});
  if (!set.has_value()) {
    return std::nullopt;
  }
  return Selection{*std::move(set)};
}

namespace {

// The (staff, measure ordinal) pair a HitRole::kStaffMeasure HitResult's own
// semantic_id names, recovered by rebuilding the same id
// staff_measure_semantic_id built at emission for every (staff, measure)
// pair this layout actually carries and comparing each for exact string
// equality against the hit's own semantic_id -- never by parsing the hit id
// itself, for the same reason find_entity_in_voice above cannot: there is
// no way to parse a NodeId/TrackId/StaveId/ordinal back out of a
// NotationId's own string. The scan is O(systems x staves x measures per
// system), the same order of magnitude resolve_marking_record above
// already scans; both are a per-pointer-event query this notation target
// runs on the writer's own click handling, entirely outside the ADR 0003
// §3.1 runtime closure and off any path graphscore_runtime_impl's process
// call reaches, so neither is subject to the realtime rules in AGENTS.md.
// The inner staff_measure_semantic_id call still allocates two fresh
// std::strings per candidate before the comparison; the starts_with check
// below skips straight past every staff other than the hit's own without
// paying that cost, since staff_measure_semantic_id(staff.id, ordinal)'s
// value is always staff.id.value with a "/staff-measure/<ordinal>" suffix
// appended (make_id's own "root/role" concatenation), so a semantic_id
// naming this staff's own measure always carries staff.id.value as a
// literal prefix.
struct ResolvedStaffMeasure {
  const StaffSystemLayout* staff   = nullptr;
  std::size_t              ordinal = 0;
};

[[nodiscard]] std::optional<ResolvedStaffMeasure> resolve_staff_measure_hit(
    const NotationLayout& layout, const NotationId& semantic_id) {
  for (const SystemLayout& system : layout.systems) {
    for (const StaffSystemLayout& staff : system.staves) {
      if (!semantic_id.value.starts_with(staff.id.value)) {
        continue;
      }
      for (const MeasureLayout& measure : system.measures) {
        if (staff_measure_semantic_id(staff.id, measure.ordinal).value ==
            semantic_id.value) {
          return ResolvedStaffMeasure{&staff, measure.ordinal};
        }
      }
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<Selection> resolve_measure_selection_at(
    const Project& project, const NotationLayout& layout, NotationPoint point) {
  if (!finite_point(point)) {
    return std::nullopt;
  }
  const std::optional<HitResult> hit = layout.hit_test(point);
  if (!hit.has_value() || hit->role != HitRole::kStaffMeasure) {
    return std::nullopt;
  }
  const std::optional<ResolvedStaffMeasure> resolved =
      resolve_staff_measure_hit(layout, hit->semantic_id);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  std::optional<FullMeasureSet> set = FullMeasureSet::create(
      {FullMeasureItem{layout.node_id, resolved->staff->track_id,
                       resolved->staff->stave_id, resolved->ordinal}});
  if (!set.has_value()) {
    return std::nullopt;
  }
  Selection selection{*std::move(set)};
  if (!validate_selection(project, selection).empty()) {
    return std::nullopt;
  }
  return selection;
}

namespace {

// The one case a note-entry click resolves to. Branch selection lives
// solely in resolve_note_entry below, and both make_note_entry_command and
// audition_for_note_entry consume its result, so the command a click builds
// and the audition it requests can never disagree about which case the
// click fell into.
enum class NoteEntryBranch : std::uint8_t {
  kCreateStreamRest,   // entirely empty armed voice, kRest armed
  kCreateStreamNote,   // entirely empty armed voice, kNote armed
  kRestDurationOnly,   // existing Rest, kRest armed
  kEventToRest,        // existing Note/Chord, kRest armed
  kRestToNote,         // existing Rest, kNote armed
  kNoteDurationOnly,   // existing Note, same pitch clicked
  kNoteToChord,        // existing Note, different pitch clicked
  kChordDurationOnly,  // existing Chord, pitch already present
  kChordExtension,     // existing Chord, new pitch clicked
};

struct NoteEntryResolution {
  NoteEntryBranch branch = NoteEntryBranch::kCreateStreamRest;

  // The event currently at the clicked position, owned by the project's own
  // VoiceContent. Null on the two kCreateStream* branches, where the armed
  // voice is entirely empty and there is no event to replace.
  const VoiceEvent* existing = nullptr;

  // The pitches this click makes newly audible, with the NEWLY INSERTED
  // pitch first and every retained pre-existing chord pitch after it.
  // Empty on every branch that introduces no new sounding pitch (both
  // duration-only branches and every kRest branch), which is exactly when
  // audition_for_note_entry returns no request.
  std::vector<SpelledPitch> sounding_pitches;

  // The pitch the click inserts. Precondition: this is one of the four
  // pitch-inserting branches, equivalently !sounding_pitches.empty(). Both
  // entry points read the inserted pitch from here rather than from their
  // own std::optional<SpelledPitch> parameter, so the command and the
  // audition are built from literally the same value and neither has to
  // re-establish that the caller supplied one.
  [[nodiscard]] const SpelledPitch& inserted_pitch() const {
    assert(!sounding_pitches.empty());
    return sounding_pitches.front();
  }
};

// Resolves a note-entry click into its single branch. A pure query: it
// never mutates `project`. Returns std::nullopt for exactly the inputs
// make_note_entry_command rejects with nullptr.
[[nodiscard]] std::optional<NoteEntryResolution> resolve_note_entry(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    const std::optional<SpelledPitch>& candidate_pitch) {
  const Node* node = project.find_node(node_id);
  if (node == nullptr)
    return std::nullopt;
  const TrackLane* lane = node->lane(track_id);
  if (lane == nullptr)
    return std::nullopt;
  const StaveVoices* stave = lane->stave(stave_id);
  if (stave == nullptr)
    return std::nullopt;
  const VoiceContent& content = stave->voice(armed.voice);

  // Explicit voice-stream workflow: the armed voice has never held
  // anything, so there is no existing event boundary to click on. Match
  // `position` against the onsets of the same hypothetical measure-aligned
  // rest fill preview_note_entry previews; on a match the caller builds one
  // CommandTransaction that creates the stream and then replaces the rest
  // at `position`. Only the fill's onset shape is read here, never the Rest
  // ids, so this calls the duration-only core
  // (decompose_measure_aligned_rest_durations) exactly as
  // preview_note_entry does rather than
  // decompose_measure_aligned_rests, whose freshly minted ids were
  // discarded unused on this path; both derive from the same tiling and so
  // can never disagree on shape.
  if (content.events().empty()) {
    const NodeTimeline* timeline = node->timeline();
    if (timeline == nullptr)
      return std::nullopt;
    const std::optional<std::vector<Duration>> hypothetical_fill =
        decompose_measure_aligned_rest_durations(*timeline);
    if (!hypothetical_fill.has_value())
      return std::nullopt;

    bool     position_is_an_onset = false;
    Rational onset;
    for (const Duration& rest_duration : *hypothetical_fill) {
      if (onset == position) {
        position_is_an_onset = true;
        break;
      }
      onset = onset + rest_duration.resolved();
    }
    if (!position_is_an_onset)
      return std::nullopt;

    if (armed.entry_kind == NotePaletteEntryKind::kRest)
      return NoteEntryResolution{
          NoteEntryBranch::kCreateStreamRest, nullptr, {}};
    if (!candidate_pitch.has_value())
      return std::nullopt;
    return NoteEntryResolution{
        NoteEntryBranch::kCreateStreamNote, nullptr, {*candidate_pitch}};
  }

  const auto idx = content.find_event_index_at(position);
  if (!idx.has_value())
    return std::nullopt;
  const VoiceEvent& existing = content.events()[*idx];

  if (armed.entry_kind == NotePaletteEntryKind::kRest) {
    // A rest sounds nothing, so no kRest branch ever auditions. Discard any
    // candidate_pitch: rests have no pitch.
    if (std::holds_alternative<Rest>(existing))
      return NoteEntryResolution{
          NoteEntryBranch::kRestDurationOnly, &existing, {}};
    return NoteEntryResolution{NoteEntryBranch::kEventToRest, &existing, {}};
  }

  // --- kNote entry ---
  if (!candidate_pitch.has_value())
    return std::nullopt;

  const SpelledPitch& new_pitch = *candidate_pitch;

  if (std::holds_alternative<Rest>(existing))
    return NoteEntryResolution{
        NoteEntryBranch::kRestToNote, &existing, {new_pitch}};

  // Existing Note: pitch match → duration-only; mismatch → promote to Chord.
  if (const auto* old_note = std::get_if<Note>(&existing)) {
    if (old_note->pitch == new_pitch)
      return NoteEntryResolution{
          NoteEntryBranch::kNoteDurationOnly, &existing, {}};
    return NoteEntryResolution{
        NoteEntryBranch::kNoteToChord, &existing, {new_pitch, old_note->pitch}};
  }

  // Existing Chord.
  if (const auto* old_chord = std::get_if<Chord>(&existing)) {
    // Detect duplicate pitch.
    const bool pitch_already_present = std::ranges::any_of(
        old_chord->notes,
        [&](const ChordNote& cn) { return cn.pitch == new_pitch; });

    if (pitch_already_present)
      return NoteEntryResolution{
          NoteEntryBranch::kChordDurationOnly, &existing, {}};

    std::vector<SpelledPitch> sounding_pitches;
    sounding_pitches.reserve(old_chord->notes.size() + 1);
    sounding_pitches.push_back(new_pitch);
    for (const ChordNote& chord_note : old_chord->notes)
      sounding_pitches.push_back(chord_note.pitch);
    return NoteEntryResolution{NoteEntryBranch::kChordExtension, &existing,
                               std::move(sounding_pitches)};
  }

  return std::nullopt;
}

}  // namespace

std::unique_ptr<Command> make_note_entry_command(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    std::optional<SpelledPitch> candidate_pitch) {
  const std::optional<NoteEntryResolution> resolution = resolve_note_entry(
      project, node_id, track_id, stave_id, position, armed, candidate_pitch);
  if (!resolution.has_value())
    return nullptr;

  const auto set_event = [&](VoiceEvent event) {
    return std::make_unique<SetEventCommand>(
        node_id, track_id, stave_id, armed.voice, position, std::move(event));
  };

  switch (resolution->branch) {
    case NoteEntryBranch::kCreateStreamRest:
    case NoteEntryBranch::kCreateStreamNote: {
      // One CommandTransaction that creates the stream and then replaces the
      // rest at `position` -- a single undoable action that either succeeds
      // completely or leaves the project untouched.
      VoiceEvent new_event;
      if (resolution->branch == NoteEntryBranch::kCreateStreamRest) {
        new_event = make_rest(armed.duration);
      } else {
        new_event = make_note(resolution->inserted_pitch(), armed.duration);
      }

      auto transaction = std::make_unique<CommandTransaction>();
      if (!transaction
               ->add_command(std::make_unique<CreateVoiceStreamCommand>(
                   node_id, track_id, stave_id, armed.voice))
               .ok())
        return nullptr;
      if (!transaction
               ->add_command(std::make_unique<SetEventCommand>(
                   node_id, track_id, stave_id, armed.voice, position,
                   std::move(new_event)))
               .ok())
        return nullptr;
      return transaction;
    }

    case NoteEntryBranch::kRestDurationOnly: {
      // Preserve identity on duration-only.
      Rest new_rest     = std::get<Rest>(*resolution->existing);
      new_rest.duration = armed.duration;
      return set_event(new_rest);
    }

    case NoteEntryBranch::kEventToRest:
      // For kind conversion (Note/Chord→Rest) the old identity is consumed
      // by replace_event per its documented ID-reuse rules.
      return set_event(make_rest(armed.duration));

    case NoteEntryBranch::kRestToNote:
      return set_event(make_note(resolution->inserted_pitch(), armed.duration));

    case NoteEntryBranch::kNoteDurationOnly: {
      Note new_note     = std::get<Note>(*resolution->existing);
      new_note.duration = armed.duration;
      return set_event(new_note);
    }

    case NoteEntryBranch::kNoteToChord: {
      // Promote Note to a 2-note Chord.  The original Note's id becomes the
      // first ChordNote; the new pitch gets a fresh id. Preserve articulations
      // and stem override from the original Note.
      const Note&            old_note = std::get<Note>(*resolution->existing);
      std::vector<ChordNote> chord_notes;
      chord_notes.push_back(
          {old_note.id, old_note.pitch, old_note.tied_to_next});
      chord_notes.push_back(
          {NotationEntityId::generate(), resolution->inserted_pitch(), false});
      return set_event(make_chord(armed.duration, std::move(chord_notes),
                                  old_note.articulations, old_note.stem));
    }

    case NoteEntryBranch::kChordDurationOnly: {
      // Duration-only: preserve every identity.
      Chord new_chord    = std::get<Chord>(*resolution->existing);
      new_chord.duration = armed.duration;
      return set_event(new_chord);
    }

    case NoteEntryBranch::kChordExtension: {
      // Add a new notehead to the existing chord. Preserve identity,
      // articulations and stem override.
      const Chord&           old_chord = std::get<Chord>(*resolution->existing);
      std::vector<ChordNote> new_notes = old_chord.notes;
      new_notes.push_back(
          {NotationEntityId::generate(), resolution->inserted_pitch(), false});
      Chord new_chord = make_chord(armed.duration, std::move(new_notes));
      new_chord.id    = old_chord.id;    // preserve top-level identity
      new_chord.stem  = old_chord.stem;  // preserve stem override
      new_chord.articulations =
          old_chord.articulations;  // preserve articulations
      return set_event(new_chord);
    }
  }

  return nullptr;
}

std::optional<NoteAuditionRequest> audition_for_note_entry(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    std::optional<SpelledPitch> candidate_pitch) {
  const std::optional<NoteEntryResolution> resolution = resolve_note_entry(
      project, node_id, track_id, stave_id, position, armed, candidate_pitch);
  // An empty sounding set is exactly the "nothing newly sounds" case: both
  // duration-only branches and every kRest branch.
  if (!resolution.has_value() || resolution->sounding_pitches.empty())
    return std::nullopt;

  // The newly inserted pitch failing to convert leaves nothing meaningful
  // to audition, so the whole request is dropped rather than reduced to the
  // retained chord pitches.
  if (!resolution->inserted_pitch().to_midi_pitch().has_value())
    return std::nullopt;

  NoteAuditionRequest request;
  request.track_id = track_id;
  request.velocity = velocity_for_dynamic(project.default_dynamic());
  request.pitches.reserve(resolution->sounding_pitches.size());
  for (const SpelledPitch& pitch : resolution->sounding_pitches) {
    const std::optional<MidiPitch> midi = pitch.to_midi_pitch();
    // A pre-existing chord pitch that cannot sound is silently skipped.
    if (midi.has_value())
      request.pitches.push_back(*midi);
  }

  // NoteAuditionRequest's contract: ascending MidiPitch order, deduplicated
  // so enharmonic spellings collapsing to one MidiPitch sound once.
  std::ranges::sort(request.pitches);
  const auto duplicates = std::ranges::unique(request.pitches);
  request.pitches.erase(duplicates.begin(), duplicates.end());
  return request;
}

// ---- SelectionDragState: dedicated-selection-tool pointer-drag lifecycle ----

bool SelectionDragState::begin(ActiveTool tool, NotationPoint anchor) noexcept {
  if (tool != ActiveTool::kSelection) {
    return false;
  }
  // A non-finite anchor produces a nullopt from update() rather than
  // silently entering a drag that can never resolve.
  if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y)) {
    return false;
  }
  cancel();  // Discard any in-progress drag without touching
             // committed_selection_.
  active_tool_ = tool;
  anchor_      = anchor;
  dragging_    = true;
  return true;
}

std::optional<Selection> SelectionDragState::update(
    const Project& project, const NotationLayout& layout, NotationPoint focus) {
  if (!dragging_) {
    return std::nullopt;
  }
  live_extent_ = resolve_range_selection(project, layout, anchor_, focus);
  return live_extent_;
}

std::optional<Selection> SelectionDragState::commit() noexcept {
  if (!dragging_) {
    return std::nullopt;
  }
  committed_selection_ = std::move(live_extent_);
  live_extent_.reset();
  dragging_ = false;
  return committed_selection_;
}

void SelectionDragState::cancel() noexcept {
  dragging_ = false;
  anchor_   = {};
  live_extent_.reset();
  // committed_selection_ is untouched: its previous value persists until the
  // next commit().
}

// ---- build_range_highlight_rects -------------------------------------------

std::vector<NotationRect> build_range_highlight_rects(
    const Selection& selection, const Project& project,
    const NotationLayout& layout) {
  const auto* range_set = std::get_if<ArbitraryRangeSet>(&selection);
  if (range_set == nullptr || range_set->items().empty()) {
    return {};
  }

  const Node* node = project.find_node(layout.node_id);
  if (node == nullptr) {
    return {};
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return {};
  }
  const MeasureMap& measures = timeline->measures();

  // Accumulate span intervals per (system, staff, measure).  Multiple items
  // (e.g. two voices on the same staff, both selected) project through the
  // same measure.  Intervals that are identical, overlapping, or touching
  // are coalesced to avoid duplicate translucent geometry; disjoint intervals
  // produce separate rectangles so gaps are not filled.
  using Key = std::tuple<std::size_t, std::size_t, std::size_t>;
  std::map<Key, std::vector<std::pair<double, double>>> intervals;

  for (std::size_t sys_idx = 0; sys_idx < layout.systems.size(); ++sys_idx) {
    const SystemLayout& system = layout.systems[sys_idx];
    for (std::size_t staff_idx = 0; staff_idx < system.staves.size();
         ++staff_idx) {
      const StaffSystemLayout& staff = system.staves[staff_idx];
      for (const ArbitraryRangeItem& item : range_set->items()) {
        if (item.node != layout.node_id) {
          continue;
        }
        if (item.track != staff.track_id || item.stave != staff.stave_id) {
          continue;
        }

        const MusicalSpan& span        = item.span;
        const double       staff_space = staff.bounds.height / 4.0;
        for (const MeasureLayout& measure : system.measures) {
          if (measure.ordinal >= measures.measure_count()) {
            continue;
          }
          const Rational measure_start =
              measures.measure_start(measure.ordinal);
          const Rational measure_end =
              measure_start + measures.measure_length(measure.ordinal);
          if (!(measure_start < span.end) || !(span.start < measure_end)) {
            continue;
          }

          const Rational clamped_start = std::max(span.start, measure_start);
          const Rational clamped_end   = std::min(span.end, measure_end);
          const double   start_x =
              position_x(measures, measure.ordinal, measure.bounds.width,
                         clamped_start, measure.bounds.x, staff_space);
          const double end_x =
              position_x(measures, measure.ordinal, measure.bounds.width,
                         clamped_end, measure.bounds.x, staff_space);

          const Key key{sys_idx, staff_idx, measure.ordinal};
          intervals[key].emplace_back(start_x, end_x);
        }
      }
    }
  }

  // Coalesce overlapping or touching intervals per key into disjoint rects.
  std::vector<NotationRect> rects;
  for (auto& [key, raw] : intervals) {
    // Sort by start_x, then end_x for determinism.
    std::ranges::sort(raw);

    // Coalesce: merge intervals that overlap or touch.
    std::vector<std::pair<double, double>> coalesced;
    for (const auto& [start_x, end_x] : raw) {
      if (coalesced.empty()) {
        coalesced.emplace_back(start_x, end_x);
      } else {
        auto& last = coalesced.back();
        if (start_x <= last.second) {
          // Overlapping or touching: extend the last interval.
          last.second = std::max(last.second, end_x);
        } else {
          // Disjoint: start a new interval.
          coalesced.emplace_back(start_x, end_x);
        }
      }
    }

    const std::size_t        sys_idx   = std::get<0>(key);
    const std::size_t        staff_idx = std::get<1>(key);
    const StaffSystemLayout& staff = layout.systems[sys_idx].staves[staff_idx];
    for (const auto& [start_x, end_x] : coalesced) {
      rects.push_back(NotationRect{start_x, staff.bounds.y, end_x - start_x,
                                   staff.bounds.height});
    }
  }
  return rects;
}

}  // namespace graphscore
