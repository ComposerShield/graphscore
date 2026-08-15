// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>
#include <graphscore/domain/voice_content.hpp>
#include <graphscore/notation/notation_selection.hpp>

#include "hit_resolution_internal.hpp"
#include "notation_ids.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

namespace {

// What kind of domain entity a hit's semantic id names within one voice's
// content, plus the actual matched id -- see find_entity_in_voice.

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

}  // namespace

// The one voice (of its owning staff's small, fixed set) that actually owns
// a kNotehead/kEvent/kMarking hit's semantic entity, plus that entity's
// resolved kind/id and the VoiceContent it was found in (kMarking's
// articulation/tuplet resolution needs the owning voice's full event list,
// not just the one matched entity).

// Owner-constrained column resolution: given a HitRegion's
// owner_system_id/owner_staff_id, locates the actual SystemLayout and
// StaffSystemLayout in the layout, resolves the semantic entity only within
// the named staff's own voices, and verifies the entity's onset falls
// within the system's actual measure range.  Rejects absent/nonexistent/
// inconsistent owner metadata so a stale or synthetic region that copies
// the winner's owner IDs but names a chord from a different staff/system
// cannot pass the column-disambiguation owner checks below.  This is used
// for both the winner and alternatives in armed-column disambiguation; the
// generic resolve_hit_entity (below) is preserved for non-column/direct
// hits that have no owner metadata to validate.
[[nodiscard]] std::optional<ResolvedVoiceEntity> resolve_entity_constrained(
    const Project& project, const NotationLayout& layout,
    const HitRegion& region) {
  if (!region.owner_system_id.has_value() ||
      !region.owner_staff_id.has_value()) {
    return std::nullopt;
  }
  const SystemLayout* owner_system = nullptr;
  for (const SystemLayout& sys : layout.systems) {
    if (sys.id == *region.owner_system_id) {
      owner_system = &sys;
      break;
    }
  }
  if (owner_system == nullptr) {
    return std::nullopt;
  }
  const StaffSystemLayout* owner_staff = nullptr;
  for (const StaffSystemLayout& st : owner_system->staves) {
    if (st.id == *region.owner_staff_id) {
      owner_staff = &st;
      break;
    }
  }
  if (owner_staff == nullptr) {
    return std::nullopt;
  }
  const Node* node = project.find_node(layout.node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  const TrackLane* lane = node->lane(owner_staff->track_id);
  if (lane == nullptr) {
    return std::nullopt;
  }
  const StaveVoices* voices = lane->stave(owner_staff->stave_id);
  if (voices == nullptr) {
    return std::nullopt;
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return std::nullopt;
  }
  const MeasureMap& measures     = timeline->measures();
  const std::size_t system_start = owner_system->first_measure;
  const std::size_t system_end   = system_start + owner_system->measures.size();

  const std::string& target = region.semantic_id.value;
  for (const VoiceLayout& voice_layout : owner_staff->voices) {
    const VoiceContent&  content = voices->voice(voice_layout.voice);
    const ResolvedEntity found   = find_entity_in_voice(content, target);
    if (found.kind == ResolvedEntityKind::kNone) {
      continue;
    }
    if (found.event == nullptr) {
      continue;
    }
    Rational onset;
    bool     onset_found = false;
    for (const VoiceEvent& ev : content.events()) {
      if (&ev == found.event) {
        onset_found = true;
        break;
      }
      onset = onset + event_duration(ev).resolved();
    }
    if (!onset_found) {
      continue;
    }
    const auto measure_opt = measures.measure_index_at(onset);
    if (!measure_opt.has_value() || *measure_opt < system_start ||
        *measure_opt >= system_end) {
      continue;
    }
    return ResolvedVoiceEntity{owner_system, owner_staff, voice_layout.voice,
                               found, &content};
  }
  return std::nullopt;
}

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
          return ResolvedVoiceEntity{&system, &staff, voice_layout.voice, found,
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

namespace {

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
// matches ".../tie/segment/system-<N>/hit" or its subdivided form
// ".../tie/segment/system-<N>/sub/<M>/hit" at path boundaries, for some
// non-negative integers N (the fragment's own first_measure) and M (the
// subdivision index). This resolver has no use for either integer, only
// for the "is this a tie segment" fact, so unlike hit_id_numeric_suffix
// this returns bool rather than the parsed value.
[[nodiscard]] bool hit_id_is_tie_segment(const NotationId& id) {
  constexpr std::string_view kTrailer = "/hit";
  if (id.value.size() < kTrailer.size() ||
      id.value.compare(id.value.size() - kTrailer.size(), kTrailer.size(),
                       kTrailer) != 0) {
    return false;
  }
  std::string_view body(id.value.data(), id.value.size() - kTrailer.size());

  // Strip optional trailing "/sub/<M>" suffix (subdivided tie curve).
  const std::size_t slash = body.rfind('/');
  if (slash != std::string_view::npos) {
    const std::string_view last = body.substr(slash + 1);
    if (!last.empty() && std::ranges::all_of(last, [](char c) {
          return c >= '0' && c <= '9';
        })) {
      if (slash >= 4 && body.substr(slash - 4, 4) == "/sub") {
        body = body.substr(0, slash - 4);
      }
    }
  }

  const std::size_t last_slash = body.rfind('/');
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
// `anchor` while the preceding event carries the same TupletGroupId, landing
// on the true first event regardless of which system the click happened in
// without merging an adjacent equal-ratio group. Returns std::nullopt if
// `anchor` does not name a voice event, or names one with no group identity
// (a stale layout).
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
  const std::optional<TupletGroupId> group = event_tuplet_group(events[*index]);
  if (!group.has_value()) {
    return std::nullopt;
  }
  while (*index > 0) {
    if (event_tuplet_group(events[*index - 1]) != group) {
      break;
    }
    --*index;
  }
  return event_id(events[*index]);
}

}  // namespace

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

}  // namespace graphscore
