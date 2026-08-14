// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_editing.hpp>
#include <graphscore/notation/notation_selection.hpp>

#include "notation_ids.hpp"
#include "note_entry_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

namespace {

// One eligible staff-step source, reduced to what the walk actually needs.
// `notehead_ids` are the ids whose "<id>/notehead" GlyphCommand carries the
// source's own horizontal origin, in preference order: the selected
// notehead for a NoteheadSet, every ChordNote for a ChordSet, and none at
// all for a rest or caret source (which have no notehead, and so degrade to
// the musical tie-break -- see selection_after_staff_step's contract).
struct StaffStepSource {
  NodeId                        node;
  TrackId                       track;
  StaveId                       stave;
  Voice                         voice;
  Rational                      position;
  std::vector<NotationEntityId> notehead_ids;
};

[[nodiscard]] std::optional<StaffStepSource> staff_step_source(
    const Project& project, const Selection& selection) {
  if (!validate_selection(project, selection).empty())
    return std::nullopt;

  if (const auto* set = std::get_if<NoteheadSet>(&selection)) {
    if (set->items().size() != 1u)
      return std::nullopt;
    const NoteheadItem& item  = set->items().front();
    const VoiceContent* voice = resolve_voice_content(
        project, item.node, item.track, item.stave, item.voice);
    if (voice == nullptr)
      return std::nullopt;
    const std::optional<Rational> position =
        voice->position_of_event(item.entity);
    if (!position.has_value())
      return std::nullopt;
    return StaffStepSource{item.node,  item.track, item.stave,
                           item.voice, *position,  {item.entity}};
  }

  if (const auto* set = std::get_if<ChordSet>(&selection)) {
    if (set->items().size() != 1u)
      return std::nullopt;
    const ChordItem&    item  = set->items().front();
    const VoiceContent* voice = resolve_voice_content(
        project, item.node, item.track, item.stave, item.voice);
    if (voice == nullptr)
      return std::nullopt;
    const std::optional<Rational> position =
        voice->position_of_event(item.entity);
    if (!position.has_value())
      return std::nullopt;
    // A Chord's own id owns no notehead glyph -- the engraver emits one per
    // ChordNote -- so the chord column's x is read from its noteheads.
    std::vector<NotationEntityId>    notehead_ids;
    const std::optional<std::size_t> index =
        voice->find_event_index_at(*position);
    if (index.has_value() && *index < voice->events().size()) {
      if (const auto* chord = std::get_if<Chord>(&voice->events()[*index])) {
        notehead_ids.reserve(chord->notes.size());
        for (const ChordNote& note : chord->notes)
          notehead_ids.push_back(note.id);
      }
    }
    return StaffStepSource{item.node,  item.track, item.stave,
                           item.voice, *position,  std::move(notehead_ids)};
  }

  if (const auto* set = std::get_if<RestSet>(&selection)) {
    if (set->items().size() != 1u)
      return std::nullopt;
    const RestItem&     item  = set->items().front();
    const VoiceContent* voice = resolve_voice_content(
        project, item.node, item.track, item.stave, item.voice);
    if (voice == nullptr)
      return std::nullopt;
    const std::optional<Rational> position =
        voice->position_of_event(item.entity);
    if (!position.has_value())
      return std::nullopt;
    return StaffStepSource{item.node,  item.track, item.stave,
                           item.voice, *position,  {}};
  }

  if (const auto* set = std::get_if<InsertionCaretSet>(&selection)) {
    if (set->items().size() != 1u)
      return std::nullopt;
    const InsertionCaretItem& item = set->items().front();
    return StaffStepSource{item.node,  item.track,    item.stave,
                           item.voice, item.position, {}};
  }

  return std::nullopt;
}

// Each event's own onset, in event order. VoiceContent is contiguous from
// position 0, so this is the running sum of resolved durations and its
// front() is always Rational(0).
[[nodiscard]] std::vector<Rational> staff_step_onsets(
    const VoiceContent& voice) {
  std::vector<Rational> onsets;
  onsets.reserve(voice.events().size());
  Rational onset(0);
  for (const VoiceEvent& event : voice.events()) {
    onsets.push_back(onset);
    onset = onset + event_duration(event).resolved();
  }
  return onsets;
}

[[nodiscard]] Rational staff_step_distance(Rational left, Rational right) {
  return left < right ? right - left : left - right;
}

// The x origin of `id`'s own notehead glyph in `layout`, or std::nullopt
// when the layout draws none for it (an id that is not a notehead, or one
// that was never laid out).
[[nodiscard]] std::optional<double> staff_step_notehead_x(
    const NotationLayout& layout, const NotationEntityId& id) {
  const std::string target = id.to_string() + "/notehead";
  for (const NotationCommand& command : layout.commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value == target)
      return glyph->origin.x;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<double> staff_step_source_x(
    const NotationLayout&                layout,
    const std::vector<NotationEntityId>& notehead_ids) {
  for (const NotationEntityId& id : notehead_ids) {
    const std::optional<double> x = staff_step_notehead_x(layout, id);
    if (x.has_value())
      return x;
  }
  return std::nullopt;
}

// The insertion caret a staff step falls back to: `position` snapped to the
// nearest legal caret position in `voice`, ties resolving to the earlier
// one. The legal set is exactly what validate_insertion_caret_set accepts:
// position 0, every event boundary in the voice, and the lane's own
// total_length() (which a carried position past the end snaps to).
[[nodiscard]] std::optional<Selection> staff_step_caret(
    const StaffStepSource& source, const MeasureScope& target,
    const TrackLane& lane, const VoiceContent& voice, Rational position) {
  std::vector<Rational> legal{Rational(0)};
  for (const Rational& onset : staff_step_onsets(voice))
    legal.push_back(onset);
  legal.push_back(lane.total_length());

  Rational snapped = legal.front();
  for (const Rational& candidate : legal) {
    if (staff_step_distance(candidate, position) <
        staff_step_distance(snapped, position))
      snapped = candidate;
  }

  const auto caret = InsertionCaretSet::create({InsertionCaretItem{
      source.node, target.track_id, target.stave_id, source.voice, snapped}});
  if (!caret.has_value())
    return std::nullopt;
  return Selection{*caret};
}

}  // namespace

std::optional<Selection> selection_after_staff_step(
    const Project& project, const NotationLayout& layout,
    const Selection& selection, StaffStepDirection direction) {
  const std::optional<StaffStepSource> resolved_source =
      staff_step_source(project, selection);
  if (!resolved_source.has_value())
    return std::nullopt;
  const StaffStepSource& source = *resolved_source;

  const Node* node = project.find_node(source.node);
  if (node == nullptr)
    return std::nullopt;

  // The project's score order, filtered to the staves this node actually
  // carries: a track/stave the node engraves nothing for is not a landing
  // place, so stepping must skip it entirely rather than resolve onto a
  // lane that does not exist.
  std::vector<MeasureScope> carried;
  for (const MeasureScope& scope : score_ordered_staves(project)) {
    const TrackLane* scope_lane = node->lane(scope.track_id);
    if (scope_lane == nullptr || scope_lane->stave(scope.stave_id) == nullptr)
      continue;
    carried.push_back(scope);
  }
  // A single-staff node has nowhere to step: wrapping onto the source staff
  // itself and re-resolving would move the composer's selection with no
  // staff change to explain it.
  if (carried.size() < 2u)
    return std::nullopt;

  const auto current =
      std::ranges::find(carried, MeasureScope{source.track, source.stave});
  if (current == carried.end())
    return std::nullopt;
  const auto index =
      static_cast<std::size_t>(std::distance(carried.begin(), current));
  const std::size_t target_index =
      direction == StaffStepDirection::kPrevious
          ? (index == 0u ? carried.size() - 1u : index - 1u)
          : (index + 1u == carried.size() ? 0u : index + 1u);
  const MeasureScope target = carried[target_index];

  const TrackLane* lane = node->lane(target.track_id);
  if (lane == nullptr)
    return std::nullopt;
  const StaveVoices* stave = lane->stave(target.stave_id);
  if (stave == nullptr)
    return std::nullopt;
  const VoiceContent& voice = stave->voice(source.voice);

  std::optional<Selection> result;
  if (voice.events().empty()) {
    // The only legal caret in an empty voice. The source voice never falls
    // through to another voice on the target staff.
    const auto caret = InsertionCaretSet::create(
        {InsertionCaretItem{source.node, target.track_id, target.stave_id,
                            source.voice, Rational(0)}});
    if (!caret.has_value())
      return std::nullopt;
    result = Selection{*caret};
  } else {
    const std::vector<Rational> onsets = staff_step_onsets(voice);
    const std::size_t           none   = voice.events().size();

    // Nearest by onset. Events are visited in ascending onset order and a
    // strictly-smaller distance is required to displace the incumbent, so
    // an equidistant pair resolves to the earlier onset. Rests are not
    // candidates -- a rest-only target is exactly what "or places a caret"
    // describes -- and neither are grace notes, which are not top-level
    // events at all.
    std::size_t nearest = none;
    for (std::size_t i = 0; i < voice.events().size(); ++i) {
      if (std::get_if<Rest>(&voice.events()[i]) != nullptr)
        continue;
      if (nearest == none ||
          staff_step_distance(onsets[i], source.position) <
              staff_step_distance(onsets[nearest], source.position))
        nearest = i;
    }

    if (nearest == none) {
      result = staff_step_caret(source, target, *lane, voice, source.position);
    } else {
      // The candidate's whole tie chain: the union of its noteheads' own
      // chains, so a partially-tied chord resolves through whichever of its
      // noteheads carries the tie. An untied candidate yields a one-member
      // chain and the loop below leaves `nearest` untouched.
      std::vector<ChainNotehead> chain;
      for (std::size_t n = 0; n < notehead_count(voice.events()[nearest]);
           ++n) {
        for (const ChainNotehead& member : build_tie_chain(voice, nearest, n)) {
          const bool known =
              std::ranges::any_of(chain, [&](const ChainNotehead& seen) {
                return seen.event_index == member.event_index &&
                       seen.note_index == member.note_index;
              });
          if (!known)
            chain.push_back(member);
        }
      }
      // Each per-notehead walk is itself ascending, but their concatenation
      // is not: notehead 0 tying forward while notehead 1 ties backward
      // visits a later event before an earlier one. Sorting restores the
      // global ascending order the visual tie-break below relies on.
      std::ranges::sort(chain,
                        [](const ChainNotehead& a, const ChainNotehead& b) {
                          return std::tie(a.event_index, a.note_index) <
                                 std::tie(b.event_index, b.note_index);
                        });

      // Visually nearest chain member. Without a source x this leaves
      // `nearest` in place, which IS the chain member nearest by musical
      // onset (the nearest rule above already minimized that distance over
      // a superset of the chain).
      std::size_t                 landing = nearest;
      const std::optional<double> resolved =
          staff_step_source_x(layout, source.notehead_ids);
      if (resolved.has_value()) {
        const double source_x    = *resolved;
        bool         have_visual = false;
        double       best_visual = 0.0;
        for (const ChainNotehead& member : chain) {
          const std::optional<NotationEntityId> id = notehead_id_at(
              voice.events()[member.event_index], member.note_index);
          if (!id.has_value())
            continue;
          const std::optional<double> member_x =
              staff_step_notehead_x(layout, *id);
          if (!member_x.has_value())
            continue;
          const double distance = std::abs(*member_x - source_x);
          // `chain` is in ascending (event, notehead) order, so requiring a
          // strictly smaller distance resolves an exact visual tie to the
          // earlier musical onset.
          if (!have_visual || distance < best_visual) {
            have_visual = true;
            best_visual = distance;
            landing     = member.event_index;
          }
        }
      }

      const VoiceEvent& event = voice.events()[landing];
      if (const auto* note = std::get_if<Note>(&event)) {
        const auto set = NoteheadSet::create(
            {NoteheadItem{source.node, target.track_id, target.stave_id,
                          source.voice, note->id}});
        if (!set.has_value())
          return std::nullopt;
        result = Selection{*set};
      } else if (const auto* chord = std::get_if<Chord>(&event)) {
        const auto set = ChordSet::create(
            {ChordItem{source.node, target.track_id, target.stave_id,
                       source.voice, chord->id}});
        if (!set.has_value())
          return std::nullopt;
        result = Selection{*set};
      } else {
        // Unreachable: a Rest owns no notehead, so it can be neither the
        // nearest candidate nor a tie-chain member.
        return std::nullopt;
      }
    }
  }

  if (!result.has_value() || !validate_selection(project, *result).empty())
    return std::nullopt;
  return result;
}

}  // namespace graphscore
