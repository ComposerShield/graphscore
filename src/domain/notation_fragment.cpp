// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/notation_fragment.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/clef_lane.hpp>
#include <graphscore/domain/measure_map.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

namespace {

// ---- NotationFragment::create helpers ----

void collect_event_ids(const VoiceEvent&              event,
                       std::vector<NotationEntityId>& ids) {
  ids.push_back(event_id(event));
  if (const auto* chord = std::get_if<Chord>(&event)) {
    for (const ChordNote& notehead : chord->notes)
      ids.push_back(notehead.id);
  }
}

bool has_duplicate_id(const std::vector<FragmentVoicePart>& parts) {
  std::unordered_set<NotationEntityId> seen;
  auto mark_seen = [&](NotationEntityId id) { return !seen.insert(id).second; };

  for (const FragmentVoicePart& part : parts) {
    for (const VoiceEvent& event : part.content.events()) {
      std::vector<NotationEntityId> ids;
      collect_event_ids(event, ids);
      for (const NotationEntityId id : ids) {
        if (mark_seen(id))
          return true;
      }
    }
    for (const DynamicMarking& marking : part.content.dynamics()) {
      if (mark_seen(marking.id))
        return true;
    }
    for (const Hairpin& hairpin : part.content.hairpins()) {
      if (mark_seen(hairpin.id))
        return true;
    }
    for (const Slur& slur : part.content.slurs()) {
      if (mark_seen(slur.id))
        return true;
    }
    for (const BeamOverride& override_ : part.content.beam_overrides()) {
      if (mark_seen(override_.id))
        return true;
    }
    for (const GraceGroup& group : part.content.grace_groups()) {
      if (mark_seen(group.id))
        return true;
      for (const GraceNote& note : group.notes) {
        if (mark_seen(note.id))
          return true;
      }
    }
  }
  return false;
}

bool has_duplicate_stave_context(
    const std::vector<FragmentStaveContext>& contexts) {
  for (std::size_t i = 0; i < contexts.size(); ++i) {
    for (std::size_t j = i + 1; j < contexts.size(); ++j) {
      if (contexts[i].track_ordinal == contexts[j].track_ordinal &&
          contexts[i].stave_ordinal == contexts[j].stave_ordinal)
        return true;
    }
  }
  return false;
}

bool has_duplicate_clef_change(const std::vector<FragmentClefChange>& changes) {
  for (std::size_t i = 0; i < changes.size(); ++i) {
    for (std::size_t j = i + 1; j < changes.size(); ++j) {
      if (changes[i].track_ordinal == changes[j].track_ordinal &&
          changes[i].stave_ordinal == changes[j].stave_ordinal &&
          changes[i].position == changes[j].position)
        return true;
    }
  }
  return false;
}

bool has_duplicate_measure_context(
    const std::vector<FragmentMeasureContext>& contexts) {
  for (std::size_t i = 0; i < contexts.size(); ++i) {
    for (std::size_t j = i + 1; j < contexts.size(); ++j) {
      if (contexts[i].position == contexts[j].position)
        return true;
    }
  }
  return false;
}

bool has_duplicate_pedal_span(const std::vector<FragmentPedalSpan>& spans) {
  for (std::size_t i = 0; i < spans.size(); ++i) {
    for (std::size_t j = i + 1; j < spans.size(); ++j) {
      if (spans[i].track_ordinal == spans[j].track_ordinal &&
          spans[i].stave_ordinal == spans[j].stave_ordinal &&
          spans[i].start == spans[j].start)
        return true;
    }
  }
  return false;
}

// ---- extraction: clipping ----

Note copy_note_verbatim(const Note& note) {
  return make_note(note.pitch, note.duration, note.tied_to_next,
                   note.articulations, note.stem);
}

Chord copy_chord_verbatim(const Chord& chord) {
  std::vector<ChordNote> notes;
  notes.reserve(chord.notes.size());
  for (const ChordNote& notehead : chord.notes)
    notes.push_back(
        ChordNote{NotationEntityId{}, notehead.pitch, notehead.tied_to_next});
  return make_chord(chord.duration, std::move(notes), chord.articulations,
                    chord.stem);
}

VoiceEvent copy_event_verbatim(const VoiceEvent& event) {
  if (const auto* note = std::get_if<Note>(&event))
    return copy_note_verbatim(*note);
  if (const auto* chord = std::get_if<Chord>(&event))
    return copy_chord_verbatim(*chord);
  return make_rest(std::get<Rest>(event).duration);
}

Note first_note_piece(const Note& note, Duration duration, bool tied_to_next) {
  return make_note(note.pitch, duration, tied_to_next, note.articulations,
                   note.stem);
}

Note continuation_note_piece(const Note& note, Duration duration,
                             bool tied_to_next) {
  return make_note(note.pitch, duration, tied_to_next);
}

Chord first_chord_piece(const Chord& chord, Duration duration,
                        bool tied_to_next) {
  std::vector<ChordNote> notes;
  notes.reserve(chord.notes.size());
  for (const ChordNote& notehead : chord.notes)
    notes.push_back(
        ChordNote{NotationEntityId{}, notehead.pitch, tied_to_next});
  return make_chord(duration, std::move(notes), chord.articulations,
                    chord.stem);
}

Chord continuation_chord_piece(const Chord& chord, Duration duration,
                               bool tied_to_next) {
  std::vector<ChordNote> notes;
  notes.reserve(chord.notes.size());
  for (const ChordNote& notehead : chord.notes)
    notes.push_back(
        ChordNote{NotationEntityId{}, notehead.pitch, tied_to_next});
  return make_chord(duration, std::move(notes));
}

void clear_trailing_tie(std::vector<VoiceEvent>& events) {
  if (events.empty())
    return;
  VoiceEvent& last = events.back();
  if (auto* note = std::get_if<Note>(&last)) {
    note->tied_to_next = false;
    return;
  }
  if (auto* chord = std::get_if<Chord>(&last)) {
    for (ChordNote& notehead : chord->notes)
      notehead.tied_to_next = false;
  }
}

// Every source top-level event id that survives into the fragment, mapped
// to its first surviving piece's fragment-local id -- the substrate for
// re-pointing markings (R6-R9). `surviving_source_order` additionally
// records each survivor's exact source position, in ascending source
// order, for R6's slur/hairpin endpoint search.
struct ClipState {
  std::vector<VoiceEvent>                                events;
  std::unordered_map<NotationEntityId, NotationEntityId> id_map;
  std::vector<std::pair<Rational, NotationEntityId>>     surviving_source_order;
};

// Walks `source_events` and produces the clipped, relocated fragment
// events for [origin, origin + span_length). Returns std::nullopt if a
// clipping rule cannot be satisfied (a tuplet event straddles a boundary,
// or decompose_rest cannot represent a clipped span exactly).
std::optional<ClipState> clip_events(
    const std::vector<VoiceEvent>& source_events, Rational origin,
    Rational span_length) {
  const Rational                                   end = origin + span_length;
  ClipState                                        state;
  Rational                                         pos(0);
  std::unordered_map<TupletGroupId, TupletGroupId> tuplet_ids;

  // Tuplet groups are indivisible clipboard entities, not independent
  // ratio-bearing events. Reject a range containing only part of a group.
  std::unordered_map<TupletGroupId, std::pair<Rational, Rational>> bounds;
  Rational scan_position(0);
  for (const VoiceEvent& event : source_events) {
    const Rational event_end = scan_position + event_duration(event).resolved();
    if (const auto& group = event_tuplet_group(event); group.has_value()) {
      auto [it, inserted] =
          bounds.emplace(*group, std::pair{scan_position, event_end});
      if (!inserted)
        it->second.second = event_end;
    }
    scan_position = event_end;
  }
  for (const auto& [group, group_bounds] : bounds) {
    (void)group;
    const bool intersects =
        group_bounds.first < end && origin < group_bounds.second;
    const bool contained =
        !(group_bounds.first < origin) && !(group_bounds.second > end);
    if (intersects && !contained)
      return std::nullopt;
  }

  auto append_rests = [&](Rational length) {
    if (!(length > Rational(0)))
      return true;
    const std::optional<std::vector<Rest>> rests = decompose_rest(length);
    if (!rests.has_value())
      return false;
    for (const Rest& rest : *rests)
      state.events.push_back(rest);
    return true;
  };

  auto append_verbatim = [&](const VoiceEvent& source_event) {
    VoiceEvent copy = copy_event_verbatim(source_event);
    if (const auto& source_group = event_tuplet_group(source_event);
        source_group.has_value()) {
      const auto [it, inserted] =
          tuplet_ids.emplace(*source_group, TupletGroupId::generate());
      (void)inserted;
      std::visit([&](auto& copied) { copied.tuplet_group = it->second; }, copy);
    }
    const NotationEntityId new_id  = event_id(copy);
    const bool             is_rest = std::holds_alternative<Rest>(source_event);
    state.events.push_back(std::move(copy));
    if (!is_rest) {
      state.id_map.emplace(event_id(source_event), new_id);
      state.surviving_source_order.emplace_back(pos, new_id);
    }
  };

  for (const VoiceEvent& event : source_events) {
    if (!(pos < end))
      break;

    const Duration& duration  = event_duration(event);
    const Rational  event_dur = duration.resolved();
    const Rational  event_end = pos + event_dur;
    if (!(event_end > origin)) {
      pos = event_end;
      continue;
    }

    const bool is_tuplet = duration.tuplet().has_value();
    const bool contained = !(pos < origin) && !(event_end > end);

    if (is_tuplet) {
      if (!contained)
        return std::nullopt;
      append_verbatim(event);
      pos = event_end;
      continue;
    }

    if (std::holds_alternative<Rest>(event)) {
      const Rational clip_start = pos < origin ? origin : pos;
      const Rational clip_end   = event_end < end ? event_end : end;
      if (!append_rests(clip_end - clip_start))
        return std::nullopt;
      pos = event_end;
      continue;
    }

    if (pos < origin) {
      // R1: head-straddling sounding event -> plain rests only.
      const Rational rest_end = event_end < end ? event_end : end;
      if (!append_rests(rest_end - origin))
        return std::nullopt;
      pos = event_end;
      continue;
    }

    if (event_end > end) {
      // R2: tail-straddling sounding event -> truncated tie chain.
      const std::optional<std::vector<Rest>> pieces = decompose_rest(end - pos);
      if (!pieces.has_value())
        return std::nullopt;

      NotationEntityId first_new_id;
      for (std::size_t i = 0; i < pieces->size(); ++i) {
        const bool tied_to_next = (i + 1 != pieces->size());
        VoiceEvent piece;
        if (i == 0) {
          if (const auto* note = std::get_if<Note>(&event))
            piece =
                first_note_piece(*note, (*pieces)[i].duration, tied_to_next);
          else
            piece = first_chord_piece(std::get<Chord>(event),
                                      (*pieces)[i].duration, tied_to_next);
          first_new_id = event_id(piece);
        } else {
          if (const auto* note = std::get_if<Note>(&event))
            piece = continuation_note_piece(*note, (*pieces)[i].duration,
                                            tied_to_next);
          else
            piece = continuation_chord_piece(
                std::get<Chord>(event), (*pieces)[i].duration, tied_to_next);
        }
        state.events.push_back(std::move(piece));
      }
      state.id_map.emplace(event_id(event), first_new_id);
      state.surviving_source_order.emplace_back(pos, first_new_id);
      pos = event_end;
      continue;
    }

    append_verbatim(event);
    pos = event_end;
  }

  clear_trailing_tie(state.events);
  return state;
}

// Finds R6's re-pointed slur/hairpin endpoints, or std::nullopt if the
// span must be dropped.
std::optional<std::pair<NotationEntityId, NotationEntityId>>
clip_span_endpoints(
    NotationEntityId start_event, NotationEntityId end_event,
    const VoiceContent&                                       source,
    const std::vector<std::pair<Rational, NotationEntityId>>& surviving) {
  const std::optional<Rational> start_pos =
      source.position_of_event(start_event);
  const std::optional<Rational> end_pos = source.position_of_event(end_event);
  if (!start_pos.has_value() || !end_pos.has_value())
    return std::nullopt;

  std::optional<std::size_t> start_index;
  for (std::size_t i = 0; i < surviving.size(); ++i) {
    if (!(surviving[i].first < *start_pos)) {
      start_index = i;
      break;
    }
  }
  if (!start_index.has_value())
    return std::nullopt;

  std::optional<std::size_t> end_index;
  for (std::size_t i = 0; i < surviving.size(); ++i) {
    if (!(surviving[i].first > *end_pos))
      end_index = i;
    else
      break;
  }
  if (!end_index.has_value())
    return std::nullopt;

  if (!(*start_index < *end_index))
    return std::nullopt;

  return std::make_pair(surviving[*start_index].second,
                        surviving[*end_index].second);
}

std::optional<Hairpin> clip_hairpin(
    const Hairpin& hairpin, const VoiceContent& source,
    const std::vector<std::pair<Rational, NotationEntityId>>& surviving) {
  const auto endpoints = clip_span_endpoints(
      hairpin.start_event, hairpin.end_event, source, surviving);
  if (!endpoints.has_value())
    return std::nullopt;
  return make_hairpin(endpoints->first, endpoints->second, hairpin.direction);
}

std::optional<Slur> clip_slur(
    const Slur& slur, const VoiceContent& source,
    const std::vector<std::pair<Rational, NotationEntityId>>& surviving) {
  const auto endpoints =
      clip_span_endpoints(slur.start_event, slur.end_event, source, surviving);
  if (!endpoints.has_value())
    return std::nullopt;
  return make_slur(endpoints->first, endpoints->second);
}

std::optional<BeamOverride> clip_beam_override(
    const BeamOverride&                                           override_,
    const std::unordered_map<NotationEntityId, NotationEntityId>& id_map) {
  std::vector<NotationEntityId> mapped;
  mapped.reserve(override_.events.size());
  for (const NotationEntityId id : override_.events) {
    const auto it = id_map.find(id);
    if (it != id_map.end())
      mapped.push_back(it->second);
  }
  if (mapped.size() < 2)
    return std::nullopt;
  return make_beam_override(override_.kind, std::move(mapped));
}

// The outcome of clipping one voice: either a Result carrying the real
// failure code (kInvalidArgument for a clipping-rule or referential
// failure, kOutOfMemory if a mutator hit an allocation limit) with no
// content, or a successful Result with the finished, fully-tiled
// VoiceContent.
struct VoiceClipResult {
  Result                      status;
  std::optional<VoiceContent> content;
};

// Builds the clipped voice's markings (R6-R9) on top of an already-appended
// event sequence, normalizes the result to exactly tile [0, span_length)
// with automatic rests (a source voice shorter than the copied span --
// including one never populated at all -- yields a correctly rest-filled
// part rather than a rejected one), then returns the finished VoiceContent.
// Fails, propagating the real ResultCode, if any add_* mutator rejects the
// constructed candidate or normalization itself fails.
VoiceClipResult build_clipped_content(const VoiceContent& source,
                                      ClipState&& state, Rational span_length) {
  VoiceContent content;
  for (VoiceEvent& event : state.events) {
    const Result result = content.append(std::move(event));
    if (!result.ok())
      return VoiceClipResult{result, std::nullopt};
  }

  for (const Hairpin& hairpin : source.hairpins()) {
    const std::optional<Hairpin> clipped =
        clip_hairpin(hairpin, source, state.surviving_source_order);
    if (clipped.has_value()) {
      const Result result = content.add_hairpin(*clipped);
      if (!result.ok())
        return VoiceClipResult{result, std::nullopt};
    }
  }
  for (const Slur& slur : source.slurs()) {
    const std::optional<Slur> clipped =
        clip_slur(slur, source, state.surviving_source_order);
    if (clipped.has_value()) {
      const Result result = content.add_slur(*clipped);
      if (!result.ok())
        return VoiceClipResult{result, std::nullopt};
    }
  }
  for (const BeamOverride& override_ : source.beam_overrides()) {
    const std::optional<BeamOverride> clipped =
        clip_beam_override(override_, state.id_map);
    if (clipped.has_value()) {
      const Result result = content.add_beam_override(*clipped);
      if (!result.ok())
        return VoiceClipResult{result, std::nullopt};
    }
  }
  for (const DynamicMarking& marking : source.dynamics()) {
    const auto it = state.id_map.find(marking.at_event);
    if (it == state.id_map.end())
      continue;
    const Result result =
        content.add_dynamic(make_dynamic_marking(it->second, marking.value));
    if (!result.ok())
      return VoiceClipResult{result, std::nullopt};
  }
  for (const GraceGroup& group : source.grace_groups()) {
    const auto it = state.id_map.find(group.principal_event);
    if (it == state.id_map.end())
      continue;
    std::vector<GraceNote> notes;
    notes.reserve(group.notes.size());
    for (const GraceNote& note : group.notes) {
      notes.push_back(GraceNote{NotationEntityId{}, note.pitch, note.duration,
                                note.type, note.slashed});
    }
    const Result result =
        content.add_grace_group(make_grace_group(it->second, std::move(notes)));
    if (!result.ok())
      return VoiceClipResult{result, std::nullopt};
  }

  const Result normalize_result = content.normalize(span_length);
  if (!normalize_result.ok())
    return VoiceClipResult{normalize_result, std::nullopt};

  return VoiceClipResult{Result(), std::move(content)};
}

VoiceClipResult clip_voice(const VoiceContent& source, Rational origin,
                           Rational span_length) {
  std::optional<ClipState> state =
      clip_events(source.events(), origin, span_length);
  if (!state.has_value())
    return VoiceClipResult{Result(ResultCode::kInvalidArgument), std::nullopt};
  return build_clipped_content(source, std::move(*state), span_length);
}

// ---- extraction: selection walk ----

FragmentExtraction fail(ResultCode code) {
  return FragmentExtraction{Result(code), std::nullopt};
}

struct StaveRef {
  TrackId     track_id;
  StaveId     stave_id;
  std::size_t track_ordinal = 0;
  std::size_t stave_ordinal = 0;
};

struct FragmentTopology {
  std::vector<TrackId>                     track_ids;
  std::unordered_map<TrackId, std::size_t> track_ordinal;
  std::vector<FragmentTrackShape>          track_shapes;
};

std::optional<FragmentTopology> build_topology(
    const Project& project, const std::vector<TrackId>& referenced) {
  std::vector<TrackId> distinct;
  for (const TrackId& id : referenced) {
    bool present = false;
    for (const TrackId& existing : distinct) {
      if (existing == id) {
        present = true;
        break;
      }
    }
    if (!present)
      distinct.push_back(id);
  }

  for (const TrackId& id : distinct) {
    if (project.find_active_track(id) == nullptr)
      return std::nullopt;
  }

  std::sort(distinct.begin(), distinct.end(),
            [&](const TrackId& a, const TrackId& b) {
              return project.find_active_track(a)->index() <
                     project.find_active_track(b)->index();
            });

  FragmentTopology topology;
  topology.track_ids = distinct;
  topology.track_shapes.reserve(distinct.size());
  for (std::size_t i = 0; i < distinct.size(); ++i) {
    const Track* track = project.find_active_track(distinct[i]);
    topology.track_ordinal.emplace(distinct[i], i);
    topology.track_shapes.push_back(
        FragmentTrackShape{track->layout().stave_count()});
  }
  return topology;
}

std::optional<std::size_t> stave_ordinal_of(const Project& project,
                                            TrackId        track_id,
                                            StaveId        stave_id) {
  const Track* track = project.find_active_track(track_id);
  if (track == nullptr)
    return std::nullopt;
  const std::vector<StaveDefinition>& staves = track->layout().staves();
  for (std::size_t i = 0; i < staves.size(); ++i) {
    if (staves[i].id == stave_id)
      return i;
  }
  return std::nullopt;
}

// R10/R11/R12's per-stave collections, appended in `staves` order (sorted
// deterministically by NotationFragment::create afterward).
Result append_stave_scoped_fragments(
    const Project& project, const Node& node, const NodeTimeline& timeline,
    const std::vector<StaveRef>& staves, Rational origin, Rational end,
    std::vector<FragmentPedalSpan>&    pedal_spans,
    std::vector<FragmentClefChange>&   clef_changes,
    std::vector<FragmentStaveContext>& stave_contexts) {
  for (const StaveRef& ref : staves) {
    const TrackLane* lane = node.lane(ref.track_id);
    if (lane == nullptr)
      return Result(ResultCode::kInvalidArgument);

    if (const std::vector<PedalSpan>* spans = lane->pedal_spans(ref.stave_id)) {
      for (const PedalSpan& span : *spans) {
        const Rational start = span.start < origin ? origin : span.start;
        const Rational stop  = span.end < end ? span.end : end;
        if (start < stop) {
          pedal_spans.push_back(
              FragmentPedalSpan{ref.track_ordinal, ref.stave_ordinal,
                                start - origin, stop - origin});
        }
      }
    }

    Clef            clef_at_origin = Clef::kTreble;
    const ClefLane* clef_lane      = timeline.clef_lane(ref.stave_id);
    if (clef_lane != nullptr) {
      for (const ClefChange& change : clef_lane->changes()) {
        if (change.position > origin && change.position < end) {
          clef_changes.push_back(
              FragmentClefChange{ref.track_ordinal, ref.stave_ordinal,
                                 change.position - origin, change.clef});
        }
      }
      clef_at_origin = clef_lane->clef_at(origin);
    } else {
      const Track* track = project.find_active_track(ref.track_id);
      if (track == nullptr)
        return Result(ResultCode::kInvalidArgument);
      bool found = false;
      for (const StaveDefinition& def : track->layout().staves()) {
        if (def.id == ref.stave_id) {
          clef_at_origin = def.default_clef;
          found          = true;
          break;
        }
      }
      if (!found)
        return Result(ResultCode::kInvalidArgument);
    }

    stave_contexts.push_back(FragmentStaveContext{
        ref.track_ordinal, ref.stave_ordinal, clef_at_origin});
  }
  return Result();
}

std::vector<FragmentMeasureContext> build_measure_contexts(
    const NodeTimeline& timeline, Rational origin, Rational end) {
  std::vector<FragmentMeasureContext> contexts;
  const MeasureMap&                   measures = timeline.measures();
  const std::optional<std::size_t>    first = measures.measure_index_at(origin);
  if (!first.has_value()) {
    // The origin lies in the pickdown region (at or past
    // MeasureMap::total_length()): there is no containing measure, but
    // R12 guarantees a context at relative position 0 regardless. The
    // pickdown is always shorter than one measure under the boundary's
    // governing meter (NodeTimeline::set_pickdown), so the last main-region
    // measure's signatures are the ones actually in effect there. No
    // measure start can fall strictly inside [origin, end) in this case,
    // since every measure start is < total_length() <= origin.
    const std::size_t last_index   = measures.measure_count() - 1;
    const Measure&    last_measure = measures.measure(last_index);
    contexts.push_back(FragmentMeasureContext{
        Rational(0), last_measure.time_signature, last_measure.key_signature});
    return contexts;
  }

  const Measure& origin_measure = measures.measure(*first);
  contexts.push_back(FragmentMeasureContext{Rational(0),
                                            origin_measure.time_signature,
                                            origin_measure.key_signature});

  for (std::size_t index = *first + 1; index < measures.measure_count();
       ++index) {
    const Rational start = measures.measure_start(index);
    if (!(start < end))
      break;
    const Measure& measure = measures.measure(index);
    contexts.push_back(FragmentMeasureContext{
        start - origin, measure.time_signature, measure.key_signature});
  }
  return contexts;
}

FragmentExtraction assemble(
    Rational span_length, std::vector<FragmentTrackShape> track_shapes,
    std::vector<FragmentVoicePart>      parts,
    std::vector<FragmentPedalSpan>      pedal_spans,
    std::vector<FragmentClefChange>     clef_changes,
    std::vector<FragmentStaveContext>   stave_contexts,
    std::vector<FragmentMeasureContext> measure_contexts) {
  std::optional<NotationFragment> fragment = NotationFragment::create(
      span_length, std::move(track_shapes), std::move(parts),
      std::move(pedal_spans), std::move(clef_changes),
      std::move(stave_contexts), std::move(measure_contexts));
  if (!fragment.has_value())
    return fail(ResultCode::kInvalidArgument);
  return FragmentExtraction{Result(), std::move(fragment)};
}

FragmentExtraction extract_full_measure(const Project&        project,
                                        const FullMeasureSet& set) {
  const std::vector<FullMeasureItem>& items         = set.items();
  const NodeId                        node_id       = items[0].node;
  const std::size_t                   measure_index = items[0].measure_index;
  const std::size_t                   measure_count = items[0].measure_count;
  for (const FullMeasureItem& item : items) {
    if (!(item.node == node_id) || item.measure_index != measure_index ||
        item.measure_count != measure_count)
      return fail(ResultCode::kInvalidArgument);
  }

  const Node* node = project.find_node(node_id);
  if (node == nullptr)
    return fail(ResultCode::kInvalidArgument);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return fail(ResultCode::kInvalidArgument);
  const std::size_t timeline_count = timeline->measures().measure_count();
  if (measure_index >= timeline_count ||
      measure_count > timeline_count - measure_index)
    return fail(ResultCode::kInvalidArgument);

  const MeasureMap& measures  = timeline->measures();
  const Rational    origin    = measures.measure_start(measure_index);
  const std::size_t end_index = measure_index + measure_count;
  const Rational    end       = end_index == timeline_count
                                    ? measures.total_length()
                                    : measures.measure_start(end_index);
  const Rational    length    = end - origin;

  std::vector<TrackId> referenced_tracks;
  referenced_tracks.reserve(items.size());
  for (const FullMeasureItem& item : items)
    referenced_tracks.push_back(item.track);
  const std::optional<FragmentTopology> topology =
      build_topology(project, referenced_tracks);
  if (!topology.has_value())
    return fail(ResultCode::kInvalidArgument);

  std::vector<FragmentVoicePart> parts;
  std::vector<StaveRef>          distinct_staves;
  for (const FullMeasureItem& item : items) {
    const auto ordinal_it = topology->track_ordinal.find(item.track);
    if (ordinal_it == topology->track_ordinal.end())
      return fail(ResultCode::kInvalidArgument);
    const std::optional<std::size_t> stave_ordinal =
        stave_ordinal_of(project, item.track, item.stave);
    if (!stave_ordinal.has_value())
      return fail(ResultCode::kInvalidArgument);

    distinct_staves.push_back(
        StaveRef{item.track, item.stave, ordinal_it->second, *stave_ordinal});

    // validate_selection (already required to pass, above) proves node,
    // track, stave and lane all exist via validate_track_scope_chain, so
    // these dereferences are safe without an explicit null check.
    const TrackLane*   lane = node->lane(item.track);
    const StaveVoices* sv   = lane->stave(item.stave);
    for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
      const Voice         voice   = *Voice::create(v);
      const VoiceContent& source  = sv->voice(voice);
      VoiceClipResult     clipped = clip_voice(source, origin, length);
      if (!clipped.status.ok())
        return fail(clipped.status.code());
      parts.push_back(FragmentVoicePart{ordinal_it->second, *stave_ordinal,
                                        voice, std::move(*clipped.content)});
    }
  }

  std::vector<FragmentPedalSpan>    pedal_spans;
  std::vector<FragmentClefChange>   clef_changes;
  std::vector<FragmentStaveContext> stave_contexts;
  const Result stave_result = append_stave_scoped_fragments(
      project, *node, *timeline, distinct_staves, origin, end, pedal_spans,
      clef_changes, stave_contexts);
  if (!stave_result.ok())
    return fail(ResultCode::kInvalidArgument);

  std::vector<FragmentMeasureContext> measure_contexts =
      build_measure_contexts(*timeline, origin, end);

  return assemble(length, topology->track_shapes, std::move(parts),
                  std::move(pedal_spans), std::move(clef_changes),
                  std::move(stave_contexts), std::move(measure_contexts));
}

FragmentExtraction extract_arbitrary_range(const Project&           project,
                                           const ArbitraryRangeSet& set) {
  const std::vector<ArbitraryRangeItem>& items   = set.items();
  const NodeId                           node_id = items[0].node;
  const MusicalSpan                      span    = items[0].span;
  for (const ArbitraryRangeItem& item : items) {
    if (!(item.node == node_id) || !(item.span == span))
      return fail(ResultCode::kInvalidArgument);
  }
  if (!(span.start < span.end) || span.start < Rational(0))
    return fail(ResultCode::kInvalidArgument);

  const Node* node = project.find_node(node_id);
  if (node == nullptr)
    return fail(ResultCode::kInvalidArgument);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return fail(ResultCode::kInvalidArgument);
  if (span.end > timeline->node_end())
    return fail(ResultCode::kInvalidArgument);

  const Rational origin = span.start;
  const Rational end    = span.end;
  const Rational length = end - origin;

  std::vector<TrackId> referenced_tracks;
  referenced_tracks.reserve(items.size());
  for (const ArbitraryRangeItem& item : items)
    referenced_tracks.push_back(item.track);
  const std::optional<FragmentTopology> topology =
      build_topology(project, referenced_tracks);
  if (!topology.has_value())
    return fail(ResultCode::kInvalidArgument);

  std::vector<FragmentVoicePart> parts;
  std::vector<StaveRef>          distinct_staves;
  for (const ArbitraryRangeItem& item : items) {
    const auto ordinal_it = topology->track_ordinal.find(item.track);
    if (ordinal_it == topology->track_ordinal.end())
      return fail(ResultCode::kInvalidArgument);
    const std::optional<std::size_t> stave_ordinal =
        stave_ordinal_of(project, item.track, item.stave);
    if (!stave_ordinal.has_value())
      return fail(ResultCode::kInvalidArgument);

    bool present = false;
    for (const StaveRef& ref : distinct_staves) {
      if (ref.track_id == item.track && ref.stave_id == item.stave) {
        present = true;
        break;
      }
    }
    if (!present) {
      distinct_staves.push_back(
          StaveRef{item.track, item.stave, ordinal_it->second, *stave_ordinal});
    }

    // validate_selection (already required to pass, above) proves node,
    // track, stave and lane all exist via validate_track_scope_chain, so
    // these dereferences are safe without an explicit null check.
    const TrackLane*    lane    = node->lane(item.track);
    const StaveVoices*  sv      = lane->stave(item.stave);
    const VoiceContent& source  = sv->voice(item.voice);
    VoiceClipResult     clipped = clip_voice(source, origin, length);
    if (!clipped.status.ok())
      return fail(clipped.status.code());
    parts.push_back(FragmentVoicePart{ordinal_it->second, *stave_ordinal,
                                      item.voice, std::move(*clipped.content)});
  }

  std::vector<FragmentPedalSpan>    pedal_spans;
  std::vector<FragmentClefChange>   clef_changes;
  std::vector<FragmentStaveContext> stave_contexts;
  const Result stave_result = append_stave_scoped_fragments(
      project, *node, *timeline, distinct_staves, origin, end, pedal_spans,
      clef_changes, stave_contexts);
  if (!stave_result.ok())
    return fail(ResultCode::kInvalidArgument);

  std::vector<FragmentMeasureContext> measure_contexts =
      build_measure_contexts(*timeline, origin, end);

  return assemble(length, topology->track_shapes, std::move(parts),
                  std::move(pedal_spans), std::move(clef_changes),
                  std::move(stave_contexts), std::move(measure_contexts));
}

}  // namespace

NotationFragment::NotationFragment(
    Rational span_length, std::vector<FragmentTrackShape> tracks,
    std::vector<FragmentVoicePart>      parts,
    std::vector<FragmentPedalSpan>      pedal_spans,
    std::vector<FragmentClefChange>     clef_changes,
    std::vector<FragmentStaveContext>   stave_contexts,
    std::vector<FragmentMeasureContext> measure_contexts)
    : span_length_(span_length),
      tracks_(std::move(tracks)),
      parts_(std::move(parts)),
      pedal_spans_(std::move(pedal_spans)),
      clef_changes_(std::move(clef_changes)),
      stave_contexts_(std::move(stave_contexts)),
      measure_contexts_(std::move(measure_contexts)) {}

std::optional<NotationFragment> NotationFragment::create(
    Rational span_length, std::vector<FragmentTrackShape> tracks,
    std::vector<FragmentVoicePart>      parts,
    std::vector<FragmentPedalSpan>      pedal_spans,
    std::vector<FragmentClefChange>     clef_changes,
    std::vector<FragmentStaveContext>   stave_contexts,
    std::vector<FragmentMeasureContext> measure_contexts) {
  if (!(span_length > Rational(0)))
    return std::nullopt;
  if (tracks.empty() || parts.empty())
    return std::nullopt;

  auto in_range = [&](std::size_t track_ordinal, std::size_t stave_ordinal) {
    return track_ordinal < tracks.size() &&
           stave_ordinal < tracks[track_ordinal].stave_count;
  };

  for (const FragmentVoicePart& part : parts) {
    if (!in_range(part.track_ordinal, part.stave_ordinal))
      return std::nullopt;
  }
  for (const FragmentPedalSpan& span : pedal_spans) {
    if (!in_range(span.track_ordinal, span.stave_ordinal))
      return std::nullopt;
  }
  for (const FragmentClefChange& change : clef_changes) {
    if (!in_range(change.track_ordinal, change.stave_ordinal))
      return std::nullopt;
  }
  for (const FragmentStaveContext& context : stave_contexts) {
    if (!in_range(context.track_ordinal, context.stave_ordinal))
      return std::nullopt;
  }

  for (std::size_t i = 0; i < parts.size(); ++i) {
    for (std::size_t j = i + 1; j < parts.size(); ++j) {
      if (parts[i].track_ordinal == parts[j].track_ordinal &&
          parts[i].stave_ordinal == parts[j].stave_ordinal &&
          parts[i].voice == parts[j].voice)
        return std::nullopt;
    }
  }

  for (const FragmentVoicePart& part : parts) {
    if (!part.content.check_complete(span_length).ok())
      return std::nullopt;
    if (!part.content.validate().ok())
      return std::nullopt;
  }

  for (const FragmentPedalSpan& span : pedal_spans) {
    if (!(span.start >= Rational(0) && span.start < span.end &&
          span.end <= span_length))
      return std::nullopt;
  }
  for (const FragmentClefChange& change : clef_changes) {
    if (!(change.position >= Rational(0) && change.position < span_length))
      return std::nullopt;
  }
  for (const FragmentMeasureContext& context : measure_contexts) {
    if (!(context.position >= Rational(0) && context.position < span_length))
      return std::nullopt;
  }

  if (has_duplicate_id(parts))
    return std::nullopt;
  if (has_duplicate_stave_context(stave_contexts))
    return std::nullopt;
  if (has_duplicate_clef_change(clef_changes))
    return std::nullopt;
  if (has_duplicate_measure_context(measure_contexts))
    return std::nullopt;
  if (has_duplicate_pedal_span(pedal_spans))
    return std::nullopt;

  std::sort(parts.begin(), parts.end(),
            [](const FragmentVoicePart& a, const FragmentVoicePart& b) {
              if (a.track_ordinal != b.track_ordinal)
                return a.track_ordinal < b.track_ordinal;
              if (a.stave_ordinal != b.stave_ordinal)
                return a.stave_ordinal < b.stave_ordinal;
              return a.voice < b.voice;
            });
  std::sort(pedal_spans.begin(), pedal_spans.end(),
            [](const FragmentPedalSpan& a, const FragmentPedalSpan& b) {
              if (a.track_ordinal != b.track_ordinal)
                return a.track_ordinal < b.track_ordinal;
              if (a.stave_ordinal != b.stave_ordinal)
                return a.stave_ordinal < b.stave_ordinal;
              return a.start < b.start;
            });
  std::sort(clef_changes.begin(), clef_changes.end(),
            [](const FragmentClefChange& a, const FragmentClefChange& b) {
              if (a.track_ordinal != b.track_ordinal)
                return a.track_ordinal < b.track_ordinal;
              if (a.stave_ordinal != b.stave_ordinal)
                return a.stave_ordinal < b.stave_ordinal;
              return a.position < b.position;
            });
  std::sort(stave_contexts.begin(), stave_contexts.end(),
            [](const FragmentStaveContext& a, const FragmentStaveContext& b) {
              if (a.track_ordinal != b.track_ordinal)
                return a.track_ordinal < b.track_ordinal;
              return a.stave_ordinal < b.stave_ordinal;
            });
  std::sort(
      measure_contexts.begin(), measure_contexts.end(),
      [](const FragmentMeasureContext& a, const FragmentMeasureContext& b) {
        return a.position < b.position;
      });

  return NotationFragment(span_length, std::move(tracks), std::move(parts),
                          std::move(pedal_spans), std::move(clef_changes),
                          std::move(stave_contexts),
                          std::move(measure_contexts));
}

FragmentExtraction extract_fragment(const Project&   project,
                                    const Selection& selection) {
  if (!std::holds_alternative<FullMeasureSet>(selection) &&
      !std::holds_alternative<ArbitraryRangeSet>(selection)) {
    return fail(ResultCode::kInvalidArgument);
  }

  if (!validate_selection(project, selection).empty())
    return fail(ResultCode::kInvalidArgument);

  try {
    if (const auto* full_measure = std::get_if<FullMeasureSet>(&selection))
      return extract_full_measure(project, *full_measure);
    return extract_arbitrary_range(project,
                                   std::get<ArbitraryRangeSet>(selection));
  } catch (const std::bad_alloc&) {
    return fail(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return fail(ResultCode::kOutOfMemory);
  }
}

}  // namespace graphscore
