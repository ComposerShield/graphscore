// SPDX-License-Identifier: Apache-2.0
//
// Internal helpers shared by PasteFragmentCommand, CutFragmentCommand, and
// DuplicateNodesCommand. Not installed; included only by the command .cpp
// files that need them (src/domain/*_fragment_command.cpp and
// src/domain/duplicate_nodes_command.cpp).

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/notation_fragment.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/paste_fragment_command.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/track.hpp>
#include <graphscore/domain/voice_content.hpp>
#include "command_snapshot_compare.hpp"
#include "marking_command_helpers.hpp"

namespace graphscore {
namespace internal {

// ---- destination ordinal mapping (PasteFragmentCommand) ----

// The destination TrackId/StaveId a fragment's ordinals resolve to for one
// paste, computed once up front so a failure never leaves a partially
// applied mapping.  Stave ordinals are compacted by distinct referenced
// ordinals per track: the smallest referenced source stave ordinal maps to
// the anchor stave; further distinct ordinals map to subsequent destination
// staves within that track; voices sharing an ordinal share a stave.
// Overflow counts referenced distinct staves only — a grand-staff fragment
// naming only the bass stave (ordinal 1) consumes exactly one destination
// stave.
struct PasteMapping {
  std::vector<TrackId>              tracks;
  std::vector<std::vector<StaveId>> staves;
  // Per track_ordinal: source stave_ordinal -> compacted index into
  // staves[track_ordinal].
  std::vector<std::unordered_map<std::size_t, std::size_t>> ordinal_to_compact;

  [[nodiscard]] TrackId track_id(std::size_t track_ordinal) const {
    return tracks[track_ordinal];
  }

  [[nodiscard]] std::optional<StaveId> stave_id(
      std::size_t track_ordinal, std::size_t stave_ordinal) const {
    if (track_ordinal >= ordinal_to_compact.size())
      return std::nullopt;
    const auto& ord_map = ordinal_to_compact[track_ordinal];
    const auto  it      = ord_map.find(stave_ordinal);
    if (it == ord_map.end())
      return std::nullopt;
    const std::size_t compact_idx = it->second;
    if (compact_idx >= staves[track_ordinal].size())
      return std::nullopt;
    return staves[track_ordinal][compact_idx];
  }
};

// Resolves `fragment`'s (track_ordinal, stave_ordinal) coordinate space
// onto real destination TrackId/StaveId values per the "anchor + project
// order" rule documented on PasteFragmentCommand. Returns std::nullopt if
// the anchor itself does not name a real active track/stave, or if mapping
// any ordinal actually referenced by a fragment part would require more
// active tracks or more staves in a destination track than exist.
[[nodiscard]] inline std::optional<PasteMapping> resolve_paste_mapping(
    const Project& project, const NotationFragment& fragment,
    const PasteAnchor& anchor) {
  const Track* anchor_track = project.find_active_track(anchor.track);
  if (anchor_track == nullptr)
    return std::nullopt;

  const std::vector<Track>&  active = project.active_tracks();
  std::optional<std::size_t> anchor_track_pos;
  for (std::size_t i = 0; i < active.size(); ++i) {
    if (active[i].id() == anchor.track) {
      anchor_track_pos = i;
      break;
    }
  }
  if (!anchor_track_pos.has_value())
    return std::nullopt;

  std::optional<std::size_t>          anchor_stave_ordinal;
  const std::vector<StaveDefinition>& anchor_staves =
      anchor_track->layout().staves();
  for (std::size_t i = 0; i < anchor_staves.size(); ++i) {
    if (anchor_staves[i].id == anchor.stave) {
      anchor_stave_ordinal = i;
      break;
    }
  }
  if (!anchor_stave_ordinal.has_value())
    return std::nullopt;

  // Collect the set of distinct stave ordinals actually referenced per
  // track_ordinal, then compact them so only those staves are consumed in
  // the destination.  The fragment shape's declared stave_count is not
  // used for overflow — only distinct referenced ordinals matter.
  // The union of fragment voice parts, stave-scoped pedal spans, AND clef
  // changes forms the mapping: a pedal span or a clef change can each name
  // a stave no voice part references, and that stave must still be
  // mapped/created as a destination stave so it resolves rather than
  // silently failing the whole paste.
  std::vector<std::set<std::size_t>> used(fragment.tracks().size());
  for (const FragmentVoicePart& part : fragment.parts()) {
    if (part.track_ordinal < used.size())
      used[part.track_ordinal].insert(part.stave_ordinal);
  }
  for (const FragmentPedalSpan& span : fragment.pedal_spans()) {
    if (span.track_ordinal < used.size())
      used[span.track_ordinal].insert(span.stave_ordinal);
  }
  for (const FragmentClefChange& change : fragment.clef_changes()) {
    if (change.track_ordinal < used.size())
      used[change.track_ordinal].insert(change.stave_ordinal);
  }

  PasteMapping mapping;
  mapping.tracks.reserve(fragment.tracks().size());
  mapping.staves.reserve(fragment.tracks().size());
  mapping.ordinal_to_compact.resize(fragment.tracks().size());

  for (std::size_t t = 0; t < fragment.tracks().size(); ++t) {
    const std::size_t dest_pos = *anchor_track_pos + t;
    if (dest_pos >= active.size())
      return std::nullopt;
    const Track& dest_track = active[dest_pos];
    mapping.tracks.push_back(dest_track.id());

    const std::size_t base = (t == 0) ? *anchor_stave_ordinal : 0;
    const std::vector<StaveDefinition>& dest_staves =
        dest_track.layout().staves();

    // Compact: sort the distinct used ordinals and map the i-th smallest
    // to destination stave index base + i.
    std::vector<std::size_t> sorted(used[t].begin(), used[t].end());
    std::sort(sorted.begin(), sorted.end());

    std::vector<StaveId>                         stave_ids;
    std::unordered_map<std::size_t, std::size_t> ordinal_map;
    stave_ids.reserve(sorted.size());
    for (std::size_t i = 0; i < sorted.size(); ++i) {
      const std::size_t dest_stave_pos = base + i;
      if (dest_stave_pos >= dest_staves.size())
        return std::nullopt;
      stave_ids.push_back(dest_staves[dest_stave_pos].id);
      ordinal_map[sorted[i]] = i;
    }
    mapping.staves.push_back(std::move(stave_ids));
    mapping.ordinal_to_compact[t] = std::move(ordinal_map);
  }

  return mapping;
}

// ---- destination voice reconnection (Paste and Cut) ----

// One retained region of a destination voice: events exactly tiling
// [region_start, region_end), and the ids of every top-level event kept
// with unchanged identity (used to decide which markings survive).
struct ClippedRegion {
  std::vector<VoiceEvent>              events;
  std::unordered_set<NotationEntityId> surviving_ids;
};

// ---- helper: truncate a sounding event at the right edge ----
//
// Given a Note `event` whose onset is `event_pos` and whose total span is
// `event_pos + event.duration.resolved()`, returns the portion exactly
// tiling [event_pos, trunc_point) where trunc_point < event_end.  The
// truncated span is decomposed into Duration pieces; the first piece
// preserves the original event's id, pitch, articulations, and stem;
// subsequent pieces are tied continuations (same pitch, no articulations);
// the last piece has tied_to_next = false (the outgoing tie is severed).
// If the truncated span decomposes into exactly one piece, tied_to_next is
// false on that single piece.
// Returns std::nullopt if the truncated span cannot be decomposed exactly.
[[nodiscard]] inline std::optional<std::vector<VoiceEvent>> truncate_note_left(
    const Note& event, Rational trunc_point, Rational event_pos) {
  const Rational truncated_dur                  = trunc_point - event_pos;
  const std::optional<std::vector<Rest>> pieces = decompose_rest(truncated_dur);
  if (!pieces.has_value() || pieces->empty())
    return std::nullopt;

  std::vector<VoiceEvent> out;
  out.reserve(pieces->size());

  for (std::size_t i = 0; i < pieces->size(); ++i) {
    const bool is_last = (i + 1 == pieces->size());
    if (i == 0) {
      out.push_back(Note{event.id, event.pitch, (*pieces)[i].duration, !is_last,
                         event.articulations, event.stem, std::nullopt});
    } else {
      out.push_back(make_note(event.pitch, (*pieces)[i].duration, !is_last));
    }
  }
  return out;
}

// Same as truncate_note_left but for a Chord: every ChordNote participates
// in every piece; each ChordNote's tied_to_next is set identically per
// piece (true for non-last, false for last).  The original chord id is
// preserved on the first piece.
[[nodiscard]] inline std::optional<std::vector<VoiceEvent>> truncate_chord_left(
    const Chord& chord, Rational trunc_point, Rational event_pos) {
  const Rational truncated_dur                  = trunc_point - event_pos;
  const std::optional<std::vector<Rest>> pieces = decompose_rest(truncated_dur);
  if (!pieces.has_value() || pieces->empty())
    return std::nullopt;

  std::vector<VoiceEvent> out;
  out.reserve(pieces->size());

  for (std::size_t i = 0; i < pieces->size(); ++i) {
    const bool is_last = (i + 1 == pieces->size());
    if (i == 0) {
      // First piece: original id plus noteheads, each with tied_to_next =
      // !is_last.  Articulations and stem preserved; each notehead's tie
      // state adjusted individually (Chord has no top-level tied_to_next).
      Chord first    = chord;
      first.duration = (*pieces)[i].duration;
      for (ChordNote& notehead : first.notes)
        notehead.tied_to_next = !is_last;
      out.push_back(std::move(first));
    } else {
      // Continuation chord: all noteheads, no articulations.
      std::vector<ChordNote> notes;
      notes.reserve(chord.notes.size());
      for (const ChordNote& notehead : chord.notes) {
        notes.push_back(
            ChordNote{NotationEntityId{}, notehead.pitch, !is_last});
      }
      out.push_back(make_chord((*pieces)[i].duration, std::move(notes)));
    }
  }
  return out;
}

// ---- clip_left_region (Finding 2, 3): before range_start ----
//
// Walks `source_events` from position 0 and returns the portion exactly
// tiling [0, boundary_pos).  Events wholly before the boundary are kept
// verbatim (same id).  An event whose attack precedes the boundary but
// extends past it is truncated at the boundary with its original attack
// preserved and its outgoing tie severed (see truncate_note_left /
// truncate_chord_left).  A rest straddling the boundary is clipped and
// re-decomposed.  A tuplet event straddling the boundary fails with
// std::nullopt.  Gaps are filled with normalized rests.
[[nodiscard]] inline std::optional<ClippedRegion> clip_left_region(
    const std::vector<VoiceEvent>& source_events, Rational boundary_pos) {
  ClippedRegion result;
  Rational      pos(0);

  for (const VoiceEvent& event : source_events) {
    if (!(pos < boundary_pos))
      break;

    const Duration& duration  = event_duration(event);
    const Rational  event_dur = duration.resolved();
    const Rational  event_end = pos + event_dur;
    if (!(event_end > Rational(0))) {
      pos = event_end;
      continue;
    }

    // Event wholly before or at boundary: keep verbatim.
    if (!(event_end > boundary_pos)) {
      result.surviving_ids.insert(event_id(event));
      result.events.push_back(event);
      pos = event_end;
      continue;
    }

    // Event straddles the boundary.
    const bool is_tuplet = duration.tuplet().has_value();
    if (is_tuplet)
      return std::nullopt;

    if (const auto* note = std::get_if<Note>(&event)) {
      auto truncated = truncate_note_left(*note, boundary_pos, pos);
      if (!truncated.has_value())
        return std::nullopt;
      result.surviving_ids.insert(note->id);
      for (auto& piece : *truncated)
        result.events.push_back(std::move(piece));
    } else if (const auto* chord = std::get_if<Chord>(&event)) {
      auto truncated = truncate_chord_left(*chord, boundary_pos, pos);
      if (!truncated.has_value())
        return std::nullopt;
      result.surviving_ids.insert(chord->id);
      for (auto& piece : *truncated)
        result.events.push_back(std::move(piece));
    } else {
      // Rest: decompose the truncated span.
      const Rational clip_len = boundary_pos - pos;
      if (clip_len > Rational(0)) {
        const auto rests = decompose_rest(clip_len);
        if (!rests.has_value())
          return std::nullopt;
        for (const Rest& rest : *rests)
          result.events.push_back(rest);
      }
    }
    pos = event_end;
  }

  // Fill any remaining gap with rests.
  if (pos < boundary_pos) {
    const auto rests = decompose_rest(boundary_pos - pos);
    if (!rests.has_value())
      return std::nullopt;
    for (const Rest& rest : *rests)
      result.events.push_back(rest);
  }

  return result;
}

// ---- clip_right_region (Finding 2): after range_end ----
//
// Walks `source_events` from position 0 and returns the portion exactly
// tiling [boundary_pos, total_end).  An event whose attack falls before
// boundary_pos but extends past it has its in-region portion converted to
// normalized rests (its attack was removed).  An event wholly after the
// boundary is kept verbatim.  A tuplet event straddling the boundary
// fails with std::nullopt.  Gaps are filled with normalized rests.
[[nodiscard]] inline std::optional<ClippedRegion> clip_right_region(
    const std::vector<VoiceEvent>& source_events, Rational boundary_pos,
    Rational total_end) {
  ClippedRegion result;
  Rational      pos(0);

  for (const VoiceEvent& event : source_events) {
    const Duration& duration  = event_duration(event);
    const Rational  event_dur = duration.resolved();
    const Rational  event_end = pos + event_dur;

    // Event wholly before boundary: skip.
    if (!(event_end > boundary_pos)) {
      pos = event_end;
      continue;
    }

    // Event wholly after or at boundary: keep verbatim.
    if (!(pos < boundary_pos)) {
      if (pos < total_end) {
        result.surviving_ids.insert(event_id(event));
        result.events.push_back(event);
      }
      pos = event_end;
      continue;
    }

    // Event straddles the boundary (onset < boundary_pos < event_end).
    const bool is_tuplet = duration.tuplet().has_value();
    if (is_tuplet)
      return std::nullopt;

    // The portion in [boundary_pos, min(event_end, total_end)) becomes
    // rests (attack was removed).
    const Rational clip_start = boundary_pos;
    const Rational clip_end   = event_end < total_end ? event_end : total_end;
    if (clip_end > clip_start) {
      const auto rests = decompose_rest(clip_end - clip_start);
      if (!rests.has_value())
        return std::nullopt;
      for (const Rest& rest : *rests)
        result.events.push_back(rest);
    }
    pos = event_end;
    if (!(pos < total_end))
      break;
  }

  // Fill any remaining gap with rests.
  const Rational fill_start = pos < boundary_pos ? boundary_pos : pos;
  if (fill_start < total_end) {
    const auto rests = decompose_rest(total_end - fill_start);
    if (!rests.has_value())
      return std::nullopt;
    for (const Rest& rest : *rests)
      result.events.push_back(rest);
  }

  return result;
}

// A copy of `event` with every id (its own, and every embedded
// ChordNote::id) freshly regenerated, otherwise structurally identical.
[[nodiscard]] inline VoiceEvent regenerate_event(const VoiceEvent& event) {
  if (const auto* note = std::get_if<Note>(&event)) {
    return make_note(note->pitch, note->duration, note->tied_to_next,
                     note->articulations, note->stem);
  }
  if (const auto* chord = std::get_if<Chord>(&event)) {
    std::vector<ChordNote> notes;
    notes.reserve(chord->notes.size());
    for (const ChordNote& notehead : chord->notes) {
      notes.push_back(
          ChordNote{NotationEntityId{}, notehead.pitch, notehead.tied_to_next});
    }
    return make_chord(chord->duration, std::move(notes), chord->articulations,
                      chord->stem);
  }
  return make_rest(std::get<Rest>(event).duration);
}

struct RegeneratedEvents {
  std::vector<VoiceEvent>                                events;
  std::unordered_map<NotationEntityId, NotationEntityId> id_map;
};

[[nodiscard]] inline RegeneratedEvents regenerate_events(
    const std::vector<VoiceEvent>& source_events) {
  RegeneratedEvents                                out;
  std::unordered_map<TupletGroupId, TupletGroupId> tuplet_ids;
  out.events.reserve(source_events.size());
  for (const VoiceEvent& event : source_events) {
    VoiceEvent copy = regenerate_event(event);
    if (const auto& source_group = event_tuplet_group(event);
        source_group.has_value()) {
      const auto [it, inserted] =
          tuplet_ids.emplace(*source_group, TupletGroupId::generate());
      (void)inserted;
      std::visit([&](auto& copied) { copied.tuplet_group = it->second; }, copy);
    }
    out.id_map.emplace(event_id(event), event_id(copy));
    out.events.push_back(std::move(copy));
  }
  return out;
}

// Appends every marking from `source` whose referenced event id(s) are all
// present in `surviving`, unchanged (same marking id, same referenced
// ids). A beam override or a hairpin/slur with any referenced id missing
// is dropped entirely; a hairpin or slur needs both endpoints to survive.
[[nodiscard]] inline Result add_surviving_markings(
    VoiceContent& content, const VoiceContent& source,
    const std::unordered_set<NotationEntityId>& surviving,
    const bool                                  include_beam_overrides = true) {
  const auto survives = [&](NotationEntityId id) {
    return surviving.contains(id);
  };

  for (const DynamicMarking& marking : source.dynamics()) {
    if (!survives(marking.at_event))
      continue;
    const Result result = content.add_dynamic(marking);
    if (!result.ok())
      return result;
  }
  for (const Hairpin& hairpin : source.hairpins()) {
    if (!survives(hairpin.start_event) || !survives(hairpin.end_event))
      continue;
    const Result result = content.add_hairpin(hairpin);
    if (!result.ok())
      return result;
  }
  for (const Slur& slur : source.slurs()) {
    if (!survives(slur.start_event) || !survives(slur.end_event))
      continue;
    const Result result = content.add_slur(slur);
    if (!result.ok())
      return result;
  }
  if (include_beam_overrides) {
    for (const BeamOverride& override_ : source.beam_overrides()) {
      bool all_survive = true;
      for (const NotationEntityId id : override_.events) {
        if (!survives(id)) {
          all_survive = false;
          break;
        }
      }
      if (!all_survive)
        continue;
      const Result result = content.add_beam_override(override_);
      if (!result.ok())
        return result;
    }
  }
  for (const GraceGroup& group : source.grace_groups()) {
    if (!survives(group.principal_event))
      continue;
    const Result result = content.add_grace_group(group);
    if (!result.ok())
      return result;
  }
  return Result();
}

// Appends every marking from `fragment_part`, freshly re-pointed through
// `id_map` (the fragment-local id -> freshly regenerated destination id
// map regenerate_events built) and given a freshly regenerated marking id
// of its own. Fails kInvalidArgument if a referenced id is somehow absent
// from `id_map` -- unreachable for a fragment produced by extract_fragment
// (every marking there references only ids inside its own content), kept
// as a defensive guard against a hand-built NotationFragment.
[[nodiscard]] inline Result add_regenerated_markings(
    VoiceContent& content, const VoiceContent& fragment_part,
    const std::unordered_map<NotationEntityId, NotationEntityId>& id_map) {
  const auto mapped =
      [&](NotationEntityId id) -> std::optional<NotationEntityId> {
    const auto it = id_map.find(id);
    if (it == id_map.end())
      return std::nullopt;
    return it->second;
  };

  for (const DynamicMarking& marking : fragment_part.dynamics()) {
    const std::optional<NotationEntityId> at_event = mapped(marking.at_event);
    if (!at_event.has_value())
      return Result(ResultCode::kInvalidArgument);
    const Result result =
        content.add_dynamic(make_dynamic_marking(*at_event, marking.value));
    if (!result.ok())
      return result;
  }
  for (const Hairpin& hairpin : fragment_part.hairpins()) {
    const std::optional<NotationEntityId> start = mapped(hairpin.start_event);
    const std::optional<NotationEntityId> end   = mapped(hairpin.end_event);
    if (!start.has_value() || !end.has_value())
      return Result(ResultCode::kInvalidArgument);
    const Result result =
        content.add_hairpin(make_hairpin(*start, *end, hairpin.direction));
    if (!result.ok())
      return result;
  }
  for (const Slur& slur : fragment_part.slurs()) {
    const std::optional<NotationEntityId> start = mapped(slur.start_event);
    const std::optional<NotationEntityId> end   = mapped(slur.end_event);
    if (!start.has_value() || !end.has_value())
      return Result(ResultCode::kInvalidArgument);
    const Result result = content.add_slur(make_slur(*start, *end));
    if (!result.ok())
      return result;
  }
  for (const BeamOverride& override_ : fragment_part.beam_overrides()) {
    std::vector<NotationEntityId> events;
    events.reserve(override_.events.size());
    for (const NotationEntityId id : override_.events) {
      const std::optional<NotationEntityId> new_id = mapped(id);
      if (!new_id.has_value())
        return Result(ResultCode::kInvalidArgument);
      events.push_back(*new_id);
    }
    const Result result = content.add_beam_override(
        make_beam_override(override_.kind, std::move(events)));
    if (!result.ok())
      return result;
  }
  for (const GraceGroup& group : fragment_part.grace_groups()) {
    const std::optional<NotationEntityId> principal =
        mapped(group.principal_event);
    if (!principal.has_value())
      return Result(ResultCode::kInvalidArgument);
    std::vector<GraceNote> notes;
    notes.reserve(group.notes.size());
    for (const GraceNote& note : group.notes) {
      notes.push_back(GraceNote{NotationEntityId{}, note.pitch, note.duration,
                                note.type, note.slashed});
    }
    const Result result =
        content.add_grace_group(make_grace_group(*principal, std::move(notes)));
    if (!result.ok())
      return result;
  }
  return Result();
}

struct VoiceRebuildResult {
  Result                      status;
  std::optional<VoiceContent> content;
};

// Rebuilds one destination voice wholesale as three concatenated regions:
// [0, range_start) kept from `destination` (left region, attacks preserved,
// straddling events truncated, outgoing ties severed), then either
// `fragment_part`'s events (freshly regenerated ids, for a paste) or plain
// normalized rests covering [range_start, range_end) (when `fragment_part`
// is nullptr, for a cut), then [range_end, node_end) kept from
// `destination` (right region, attacks inside the replaced range removed).
//
// Markings anchored to a removed destination event are dropped; a fragment
// part's markings are added with freshly regenerated ids. Fails, returning
// no content, if either boundary region fails to clip (a straddling tuplet,
// or an unrepresentable rest span) or if any VoiceContent mutator rejects
// the assembled candidate.
//
// The rebuilt voice is normalized to node_end only for the middle region
// (paste/cut coverage), not for the outer retained regions which are
// already exact tiles.
[[nodiscard]] inline VoiceRebuildResult rebuild_voice_range(
    const VoiceContent& destination, Rational range_start, Rational range_end,
    Rational node_end, const VoiceContent* fragment_part) {
  // Left retained region: preserve attacks, truncate straddlers.
  std::optional<ClippedRegion> before =
      clip_left_region(destination.events(), range_start);
  if (!before.has_value())
    return VoiceRebuildResult{Result(ResultCode::kInvalidArgument),
                              std::nullopt};

  // Right retained region: attacks inside replaced range → rests.
  const std::optional<ClippedRegion> after =
      clip_right_region(destination.events(), range_end, node_end);
  if (!after.has_value())
    return VoiceRebuildResult{Result(ResultCode::kInvalidArgument),
                              std::nullopt};

  std::unordered_set<NotationEntityId> surviving = before->surviving_ids;
  surviving.insert(after->surviving_ids.begin(), after->surviving_ids.end());

  // Finding 3: sever the outgoing tie on the last event of the left
  // retained region if the immediately following event in the original
  // destination was removed (its id is not in `surviving`).  Without this,
  // a note wholly before the paste/cut range with tied_to_next = true
  // pointing into the replaced range would dangle its tie.
  if (!before->events.empty()) {
    const VoiceEvent&      raw_last = before->events.back();
    const NotationEntityId last_id  = event_id(raw_last);
    for (std::size_t i = 0; i < destination.events().size(); ++i) {
      if (event_id(destination.events()[i]) == last_id) {
        if (i + 1 < destination.events().size()) {
          const NotationEntityId next_id =
              event_id(destination.events()[i + 1]);
          if (!surviving.contains(next_id)) {
            if (const auto* note = std::get_if<Note>(&raw_last)) {
              Note severed          = *note;
              severed.tied_to_next  = false;
              before->events.back() = severed;
            } else if (const auto* chord = std::get_if<Chord>(&raw_last)) {
              Chord severed = *chord;
              for (ChordNote& notehead : severed.notes)
                notehead.tied_to_next = false;
              before->events.back() = severed;
            }
          }
        }
        break;
      }
    }
  }

  VoiceContent content;
  for (const VoiceEvent& event : before->events) {
    const Result result = content.append(event);
    if (!result.ok())
      return VoiceRebuildResult{result, std::nullopt};
  }

  std::unordered_map<NotationEntityId, NotationEntityId> id_map;
  if (fragment_part != nullptr) {
    RegeneratedEvents regenerated = regenerate_events(fragment_part->events());
    for (VoiceEvent& event : regenerated.events) {
      const Result result = content.append(std::move(event));
      if (!result.ok())
        return VoiceRebuildResult{result, std::nullopt};
    }
    id_map = std::move(regenerated.id_map);
  } else {
    if (range_end > range_start) {
      const std::optional<std::vector<Rest>> rests =
          decompose_rest(range_end - range_start);
      if (!rests.has_value())
        return VoiceRebuildResult{Result(ResultCode::kInvalidArgument),
                                  std::nullopt};
      for (const Rest& rest : *rests) {
        const Result result = content.append(rest);
        if (!result.ok())
          return VoiceRebuildResult{result, std::nullopt};
      }
    }
  }

  for (const VoiceEvent& event : after->events) {
    const Result result = content.append(event);
    if (!result.ok())
      return VoiceRebuildResult{result, std::nullopt};
  }

  const Result surviving_result =
      add_surviving_markings(content, destination, surviving);
  if (!surviving_result.ok())
    return VoiceRebuildResult{surviving_result, std::nullopt};

  if (fragment_part != nullptr) {
    const Result fragment_result =
        add_regenerated_markings(content, *fragment_part, id_map);
    if (!fragment_result.ok())
      return VoiceRebuildResult{fragment_result, std::nullopt};
  }

  const Result normalize_result = content.normalize(node_end);
  if (!normalize_result.ok())
    return VoiceRebuildResult{normalize_result, std::nullopt};

  return VoiceRebuildResult{Result(), std::move(content)};
}

// ---- pedal span reconnection (Paste and Cut) ----

// Removes/truncates every existing pedal span on `stave_id` intersecting
// [range_start, range_end): a span entirely inside is removed outright; a
// span straddling either boundary is replaced by freshly identified
// span(s) covering only its portion(s) outside the range. A span entirely
// outside the range is left completely untouched, including its id.
[[nodiscard]] inline Result clip_pedal_spans_in_range(TrackLane& lane,
                                                      StaveId    stave_id,
                                                      Rational   range_start,
                                                      Rational   range_end) {
  const std::vector<PedalSpan>* existing_ptr = lane.pedal_spans(stave_id);
  if (existing_ptr == nullptr)
    return Result();
  const std::vector<PedalSpan> existing = *existing_ptr;

  for (const PedalSpan& span : existing) {
    if (!(span.end > range_start) || !(span.start < range_end))
      continue;

    const Result remove_result = lane.remove_pedal_span(stave_id, span.id);
    if (!remove_result.ok())
      return remove_result;

    if (span.start < range_start) {
      const Result add_result = lane.add_pedal_span(
          stave_id, make_pedal_span(span.start, range_start));
      if (!add_result.ok())
        return add_result;
    }
    if (span.end > range_end) {
      const Result add_result =
          lane.add_pedal_span(stave_id, make_pedal_span(range_end, span.end));
      if (!add_result.ok())
        return add_result;
    }
  }
  return Result();
}

// Adds `fragment`'s pedal spans for (track_ordinal, stave_ordinal) to
// `lane`, offset by `range_start` (the paste anchor's position) with
// freshly regenerated ids.
[[nodiscard]] inline Result add_offset_fragment_pedal_spans(
    TrackLane& lane, StaveId stave_id, Rational range_start,
    const NotationFragment& fragment, std::size_t track_ordinal,
    std::size_t stave_ordinal) {
  for (const FragmentPedalSpan& span : fragment.pedal_spans()) {
    if (span.track_ordinal != track_ordinal ||
        span.stave_ordinal != stave_ordinal)
      continue;
    const Result add_result = lane.add_pedal_span(
        stave_id,
        make_pedal_span(range_start + span.start, range_start + span.end));
    if (!add_result.ok())
      return add_result;
  }
  return Result();
}

// ---- Cut's selection-derived range and voice/stave targets ----

struct AffectedVoice {
  TrackId track;
  StaveId stave;
  Voice   voice;
};

struct CutTarget {
  NodeId                     node;
  Rational                   start;
  Rational                   end;
  std::vector<AffectedVoice> voices;
};

struct LaneRestoreBundle {
  std::vector<TrackLane*> live_lanes;
  std::vector<TrackLane>  candidates;
};

// Derives the node, the exact musical-time range, and the (track, stave,
// voice) triples a cut of `selection` touches: for a FullMeasureSet, every
// voice of each selected stave; for an ArbitraryRangeSet, exactly the
// named (stave, voice) pairs. Only called after extract_fragment has
// already succeeded for the same selection, so every lookup here is
// expected to succeed; std::nullopt is a defensive fallback, not a normal
// outcome. The same is true of the arms this function does not handle:
// extract_fragment's whitelist has already rejected every Selection arm
// other than these two, so returning std::nullopt for one of them is an
// unreachable second refusal, not a silent fallthrough.
[[nodiscard]] inline std::optional<CutTarget> resolve_cut_target(
    const Project& project, const Selection& selection) {
  if (const auto* full_measure = std::get_if<FullMeasureSet>(&selection)) {
    const std::vector<FullMeasureItem>& items = full_measure->items();
    if (items.empty())
      return std::nullopt;
    const NodeId node_id = items[0].node;
    const Node*  node    = project.find_node(node_id);
    if (node == nullptr)
      return std::nullopt;
    const NodeTimeline* timeline = node->timeline();
    if (timeline == nullptr)
      return std::nullopt;
    const std::size_t measure_index = items[0].measure_index;
    if (measure_index >= timeline->measures().measure_count())
      return std::nullopt;

    CutTarget target;
    target.node  = node_id;
    target.start = timeline->measures().measure_start(measure_index);
    target.end =
        target.start + timeline->measures().measure_length(measure_index);
    for (const FullMeasureItem& item : items) {
      for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
        const std::optional<Voice> voice = Voice::create(v);
        if (voice.has_value())
          target.voices.push_back(
              AffectedVoice{item.track, item.stave, *voice});
      }
    }
    return target;
  }

  if (const auto* range = std::get_if<ArbitraryRangeSet>(&selection)) {
    const std::vector<ArbitraryRangeItem>& items = range->items();
    if (items.empty())
      return std::nullopt;

    CutTarget target;
    target.node  = items[0].node;
    target.start = items[0].span.start;
    target.end   = items[0].span.end;
    for (const ArbitraryRangeItem& item : items)
      target.voices.push_back(
          AffectedVoice{item.track, item.stave, item.voice});
    return target;
  }

  return std::nullopt;
}

// ---- lane snapshot candidate validation (clipboard-specific) ----

// Validates a TrackLane candidate that was captured as a raw snapshot,
// meaning some voices may be in their default-constructed (empty) state
// rather than rhythmically complete.  Empty voices (total_length() == 0)
// represent "no music written in this voice" and are accepted as valid.
// Non-empty voices are checked for completeness, structural validity, and
// referential integrity.
//
// Also runs validate_lane_references (pedal spans, cross-voice references)
// against the full lane.  This is the permissive analogue of
// validate_lane_candidate in marking_command_helpers.hpp, which rejects
// empty voices.
[[nodiscard]] inline Result validate_clipboard_lane_candidate(
    const TrackLane& lane, Rational node_end) {
  try {
    const std::vector<NotationDiagnostic> lane_diags =
        validate_lane_references(lane, node_end);
    if (!lane_diags.empty())
      return Result(ResultCode::kInvalidArgument);

    for (const StaveId stave_id : lane.stave_ids()) {
      const StaveVoices* stave = lane.stave(stave_id);
      if (stave == nullptr)
        continue;
      for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
        const std::optional<Voice> voice = Voice::create(v);
        if (!voice.has_value())
          continue;
        const VoiceContent& vc = stave->voice(*voice);
        if (vc.total_length() == Rational(0))
          continue;
        if (!vc.check_complete(node_end).ok())
          return Result(ResultCode::kInvalidArgument);
        if (!vc.validate().ok())
          return Result(ResultCode::kInvalidArgument);
        const std::vector<NotationDiagnostic> vd =
            validate_voice_references(vc);
        if (!vd.empty())
          return Result(ResultCode::kInvalidArgument);
      }
    }

    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

// Compile-time guard: the live-lane publication phase in both
// PasteFragmentCommand, CutFragmentCommand, and multi_lane_restore_snapshot
// depends on TrackLane move assignment being noexcept so that after the
// first live mutation the remaining mutations cannot fail.  If a future
// TrackLane member or allocator change breaks this, the build fails here
// rather than silently introducing a partial-commit window.
static_assert(std::is_nothrow_move_assignable_v<TrackLane>,
              "TrackLane move assignment must be noexcept for atomic "
              "clipboard command publication");

// ---- multi-lane snapshot restore (Paste and Cut undo/redo) ----

// Restores every (TrackId, TrackLane) pair in `pre_snapshot` onto
// `node_id`'s live lanes, verifying first that each live lane still
// matches its paired entry in `expected_current` (a stale-context guard,
// the multi-lane analogue of marking_command_helpers.hpp's
// lane_restore_snapshot). Both vectors are expected to name the same
// TrackIds in the same order -- the order a command itself built them in
// -- so comparison and restoration are done by matching index, not by
// re-searching. Every candidate lane is validated before any live lane is
// touched: on any stale-context or validation failure, no live lane is
// mutated.
//
// Uses validate_clipboard_lane_candidate (permissive of raw-empty voices)
// so that a pre-snapshot captured exactly as the original lane state
// (without pre-normalization) restores correctly.
//
// Split into prepare_lane_restore (validate + build every candidate,
// touching no live lane, may allocate) and commit_lane_restore (publish
// every prepared candidate, noexcept) so a caller that must interleave
// another reversible edit — e.g. PasteFragmentCommand's clef-lane
// creation, itself fallible — between validation and publication can do so
// without risking a partial commit. multi_lane_restore_snapshot itself
// remains the simple all-in-one entry point Cut and Paste's own undo/redo
// use directly.
[[nodiscard]] inline Result prepare_lane_restore(
    const std::vector<std::pair<TrackId, TrackLane>>& pre_snapshot,
    const std::vector<std::pair<TrackId, TrackLane>>& expected_current,
    NodeId node_id, Project& project, std::optional<LaneRestoreBundle>& out) {
  if (pre_snapshot.size() != expected_current.size())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(node_id);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const Rational node_end = timeline->node_end();

  try {
    std::vector<TrackLane*> live_lanes;
    live_lanes.reserve(pre_snapshot.size());
    for (const auto& entry : pre_snapshot) {
      TrackLane* lane = node->lane(entry.first);
      if (lane == nullptr)
        return Result(ResultCode::kInvalidArgument);
      live_lanes.push_back(lane);
    }

    for (std::size_t i = 0; i < pre_snapshot.size(); ++i) {
      if (expected_current[i].first != pre_snapshot[i].first)
        return Result(ResultCode::kInternalError);
      if (!snapshot_matches(*live_lanes[i], expected_current[i].second))
        return Result(ResultCode::kInvalidArgument);
    }

    std::vector<TrackLane> candidates;
    candidates.reserve(pre_snapshot.size());
    for (const auto& entry : pre_snapshot) {
      TrackLane    candidate = entry.second;
      const Result result =
          validate_clipboard_lane_candidate(candidate, node_end);
      if (!result.ok())
        return result;
      candidates.push_back(std::move(candidate));
    }

    out.emplace(
        LaneRestoreBundle{std::move(live_lanes), std::move(candidates)});
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    return Result(ResultCode::kInternalError);
  }
  return Result();
}

// Publishes a bundle prepare_lane_restore already validated and built.
// Precondition: `bundle` came from a successful prepare_lane_restore call
// on the same node whose live lanes have not changed since. Cannot fail.
inline void commit_lane_restore(LaneRestoreBundle& bundle) noexcept {
  static_assert(std::is_nothrow_move_assignable_v<TrackLane>);
  for (std::size_t i = 0; i < bundle.live_lanes.size(); ++i)
    *bundle.live_lanes[i] = std::move(bundle.candidates[i]);
}

[[nodiscard]] inline Result multi_lane_restore_snapshot(
    const std::vector<std::pair<TrackId, TrackLane>>& pre_snapshot,
    const std::vector<std::pair<TrackId, TrackLane>>& expected_current,
    NodeId node_id, Project& project) {
  std::optional<LaneRestoreBundle> bundle;
  const Result                     prepare_result = prepare_lane_restore(
      pre_snapshot, expected_current, node_id, project, bundle);
  if (!bundle.has_value())
    return prepare_result;
  commit_lane_restore(*bundle);
  return Result();
}

}  // namespace internal
}  // namespace graphscore
