// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/notation_editing.hpp>

#include "notation_style_edit_internal.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/add_dynamic_command.hpp>
#include <graphscore/domain/add_hairpin_command.hpp>
#include <graphscore/domain/add_pedal_span_command.hpp>
#include <graphscore/domain/marking_style_command.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/remove_dynamic_command.hpp>
#include <graphscore/domain/remove_hairpin_command.hpp>
#include <graphscore/domain/remove_pedal_span_command.hpp>

namespace graphscore {

using style_edit::resolve_selected_event;
using style_edit::resolve_voice;
using style_edit::selects_grace_note;
using style_edit::unavailable;

namespace {

[[nodiscard]] bool valid_edit(MarkingEdit edit) {
  return edit == MarkingEdit::kApply || edit == MarkingEdit::kChange ||
         edit == MarkingEdit::kRemove;
}

[[nodiscard]] bool valid_dynamic(Dynamic value) {
  return std::find(kAllDynamics.begin(), kAllDynamics.end(), value) !=
         kAllDynamics.end();
}

[[nodiscard]] bool valid_direction(HairpinDirection direction) {
  return direction == HairpinDirection::kCrescendo ||
         direction == HairpinDirection::kDiminuendo;
}

// The single marking item a one-item MarkingSet of `kind` names, or nullptr.
// validate_selection has already checked that the anchor resolves to a live
// record of that kind in the addressed scope.
[[nodiscard]] const MarkingItem* resolve_marking(const Project&   project,
                                                 const Selection& selection,
                                                 MarkingKind      kind) {
  const auto* set = std::get_if<MarkingSet>(&selection);
  if (set == nullptr || set->items().size() != 1u ||
      set->items().front().kind != kind ||
      !validate_selection(project, selection).empty()) {
    return nullptr;
  }
  return &set->items().front();
}

// One voice-scoped range resolved to the complete contiguous events it covers,
// following make_tuplet_create_command's own resolution rules: exactly one
// non-empty ArbitraryRangeItem whose span coincides with complete events of a
// single staff and voice. A partial event at either end rejects.
struct VoiceRangeTarget {
  NodeId                        node;
  TrackId                       track;
  StaveId                       stave;
  Voice                         voice;
  std::vector<NotationEntityId> events;
};

[[nodiscard]] std::optional<VoiceRangeTarget> resolve_voice_range(
    const Project& project, const Selection& selection) {
  const auto* set = std::get_if<ArbitraryRangeSet>(&selection);
  if (set == nullptr || set->items().size() != 1u ||
      !validate_selection(project, selection).empty()) {
    return std::nullopt;
  }
  const ArbitraryRangeItem& item = set->items().front();
  if (item.span.start == item.span.end)
    return std::nullopt;
  const VoiceContent* voice =
      resolve_voice(project, item.node, item.track, item.stave, item.voice);
  if (voice == nullptr)
    return std::nullopt;

  Rational                      onset;
  bool                          started = false;
  std::vector<NotationEntityId> ids;
  for (const VoiceEvent& event : voice->events()) {
    const Rational end = onset + event_duration(event).resolved();
    if (!started && onset == item.span.start)
      started = true;
    if (started && onset < item.span.end) {
      ids.push_back(event_id(event));
      if (end == item.span.end) {
        return VoiceRangeTarget{item.node, item.track, item.stave, item.voice,
                                std::move(ids)};
      }
      if (end > item.span.end)
        return std::nullopt;
    }
    onset = end;
  }
  return std::nullopt;
}

// One stave-scoped range: every item of a non-empty ArbitraryRangeSet names the
// same node, track, and stave, and the resolved bounds are the union of their
// spans. Pedal is stave-scoped, so a range covering several voices of one stave
// is a legitimate pedal target even though it is not a legitimate hairpin one.
struct StaveRangeTarget {
  NodeId   node;
  TrackId  track;
  StaveId  stave;
  Rational start;
  Rational end;
};

[[nodiscard]] std::optional<StaveRangeTarget> resolve_stave_range(
    const Project& project, const Selection& selection) {
  const auto* set = std::get_if<ArbitraryRangeSet>(&selection);
  if (set == nullptr || set->items().empty() ||
      !validate_selection(project, selection).empty()) {
    return std::nullopt;
  }
  const ArbitraryRangeItem& first = set->items().front();
  StaveRangeTarget          target{first.node, first.track, first.stave,
                          first.span.start, first.span.end};
  for (const ArbitraryRangeItem& item : set->items()) {
    if (item.node != target.node || item.track != target.track ||
        item.stave != target.stave) {
      return std::nullopt;
    }
    target.start = std::min(target.start, item.span.start);
    target.end   = std::max(target.end, item.span.end);
  }
  return target;
}

// The stave-scoped pedal commands snapshot and revalidate the WHOLE TrackLane,
// which requires every voice of every stave on that lane to be rhythmically
// complete. Checking it here turns an opaque command failure into an
// actionable reason.
[[nodiscard]] bool lane_rhythm_complete(const Project& project, NodeId node_id,
                                        TrackId track_id) {
  const Node*      node = project.find_node(node_id);
  const TrackLane* lane = node == nullptr ? nullptr : node->lane(track_id);
  if (node == nullptr || lane == nullptr || node->timeline() == nullptr)
    return false;
  const Rational node_end = node->timeline()->node_end();
  for (const StaveId stave_id : lane->stave_ids()) {
    const StaveVoices* stave = lane->stave(stave_id);
    if (stave == nullptr)
      continue;
    for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
      const std::optional<Voice> voice = Voice::create(index);
      if (voice.has_value() && stave->voice(*voice).total_length() != node_end)
        return false;
    }
  }
  return true;
}

}  // namespace

NotationEditCommandResult make_dynamic_edit_command(const Project&   project,
                                                    const Selection& selection,
                                                    MarkingEdit      edit,
                                                    Dynamic          value) {
  if (!valid_edit(edit))
    return unavailable("unknown marking edit");
  if (edit != MarkingEdit::kRemove && !valid_dynamic(value))
    return unavailable("unknown dynamic");

  if (edit == MarkingEdit::kApply) {
    if (selects_grace_note(project, selection))
      return unavailable("grace notes do not support marking editing");
    const auto target = resolve_selected_event(project, selection);
    if (!target.has_value())
      return unavailable("requires one live note or chord event");
    const DynamicMarking marking = make_dynamic_marking(target->event, value);
    auto                 command = std::make_unique<AddDynamicCommand>(
        target->node, target->track, target->stave, target->voice, marking);
    Project candidate = project;
    if (!command->execute(candidate).ok())
      return unavailable("target changed or the edit is invalid");
    return {
        std::make_unique<AddDynamicCommand>(
            target->node, target->track, target->stave, target->voice, marking),
        ""};
  }

  const MarkingItem* item =
      resolve_marking(project, selection, MarkingKind::kDynamic);
  const VoiceContent* voice =
      item == nullptr || !item->voice.has_value()
          ? nullptr
          : resolve_voice(project, item->node, item->track, item->stave,
                          *item->voice);
  if (voice == nullptr)
    return unavailable("requires one live dynamic marking");
  const auto found = std::ranges::find_if(
      voice->dynamics(),
      [&](const DynamicMarking& record) { return record.id == item->anchor; });
  if (found == voice->dynamics().end())
    return unavailable("requires one live dynamic marking");

  if (edit == MarkingEdit::kRemove) {
    auto command = std::make_unique<RemoveDynamicCommand>(
        item->node, item->track, item->stave, *item->voice, item->anchor);
    Project candidate = project;
    if (!command->execute(candidate).ok())
      return unavailable("target changed or the edit is invalid");
    return {
        std::make_unique<RemoveDynamicCommand>(
            item->node, item->track, item->stave, *item->voice, item->anchor),
        ""};
  }

  if (found->value == value)
    return unavailable("dynamic is already set");
  auto command = std::make_unique<MarkingStyleCommand>(
      item->node, item->track, item->stave, *item->voice, item->anchor, value);
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return unavailable("target changed or the edit is invalid");
  return {std::make_unique<MarkingStyleCommand>(item->node, item->track,
                                                item->stave, *item->voice,
                                                item->anchor, value),
          ""};
}

NotationEditCommandResult make_hairpin_edit_command(
    const Project& project, const Selection& selection, MarkingEdit edit,
    HairpinDirection direction) {
  if (!valid_edit(edit))
    return unavailable("unknown marking edit");
  if (edit != MarkingEdit::kRemove && !valid_direction(direction))
    return unavailable("unknown hairpin direction");

  if (edit == MarkingEdit::kApply) {
    const auto target = resolve_voice_range(project, selection);
    if (!target.has_value())
      return unavailable(
          "requires a range of complete events on one staff "
          "and voice");
    if (target->events.size() < 2u)
      return unavailable("requires a range of at least two events");
    const Hairpin hairpin =
        make_hairpin(target->events.front(), target->events.back(), direction);
    auto command = std::make_unique<AddHairpinCommand>(
        target->node, target->track, target->stave, target->voice, hairpin);
    Project candidate = project;
    if (!command->execute(candidate).ok())
      return unavailable("target changed or the edit is invalid");
    return {
        std::make_unique<AddHairpinCommand>(
            target->node, target->track, target->stave, target->voice, hairpin),
        ""};
  }

  const MarkingItem* item =
      resolve_marking(project, selection, MarkingKind::kHairpin);
  const VoiceContent* voice =
      item == nullptr || !item->voice.has_value()
          ? nullptr
          : resolve_voice(project, item->node, item->track, item->stave,
                          *item->voice);
  if (voice == nullptr)
    return unavailable("requires one live hairpin marking");
  const auto found = std::ranges::find_if(
      voice->hairpins(),
      [&](const Hairpin& record) { return record.id == item->anchor; });
  if (found == voice->hairpins().end())
    return unavailable("requires one live hairpin marking");

  if (edit == MarkingEdit::kRemove) {
    auto command = std::make_unique<RemoveHairpinCommand>(
        item->node, item->track, item->stave, *item->voice, item->anchor);
    Project candidate = project;
    if (!command->execute(candidate).ok())
      return unavailable("target changed or the edit is invalid");
    return {
        std::make_unique<RemoveHairpinCommand>(
            item->node, item->track, item->stave, *item->voice, item->anchor),
        ""};
  }

  if (found->direction == direction)
    return unavailable("hairpin direction is already set");
  auto command = std::make_unique<MarkingStyleCommand>(
      item->node, item->track, item->stave, *item->voice, item->anchor,
      direction);
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return unavailable("target changed or the edit is invalid");
  return {std::make_unique<MarkingStyleCommand>(item->node, item->track,
                                                item->stave, *item->voice,
                                                item->anchor, direction),
          ""};
}

NotationEditCommandResult make_pedal_span_edit_command(
    const Project& project, const Selection& selection, MarkingEdit edit) {
  if (!valid_edit(edit))
    return unavailable("unknown marking edit");
  if (edit == MarkingEdit::kChange)
    return unavailable("pedal spans cannot be changed");

  if (edit == MarkingEdit::kApply) {
    const auto target = resolve_stave_range(project, selection);
    if (!target.has_value())
      return unavailable("requires a range on one staff");
    const Node* node = project.find_node(target->node);
    if (node == nullptr || node->timeline() == nullptr)
      return unavailable("requires a range on one staff");
    const Rational node_end = node->timeline()->node_end();
    if (!(target->start < target->end) || target->start < Rational(0) ||
        target->end > node_end) {
      return unavailable("requires a non-empty range inside the node timeline");
    }
    if (!lane_rhythm_complete(project, target->node, target->track))
      return unavailable(
          "requires complete rhythm in every voice on the track");
    const PedalSpan span    = make_pedal_span(target->start, target->end);
    auto            command = std::make_unique<AddPedalSpanCommand>(
        target->node, target->track, target->stave, span);
    Project candidate = project;
    if (!command->execute(candidate).ok())
      return unavailable("target changed or the edit is invalid");
    return {std::make_unique<AddPedalSpanCommand>(target->node, target->track,
                                                  target->stave, span),
            ""};
  }

  const MarkingItem* item =
      resolve_marking(project, selection, MarkingKind::kPedalSpan);
  if (item == nullptr || item->voice.has_value())
    return unavailable("requires one live pedal span marking");
  if (!lane_rhythm_complete(project, item->node, item->track))
    return unavailable("requires complete rhythm in every voice on the track");
  auto command = std::make_unique<RemovePedalSpanCommand>(
      item->node, item->track, item->stave, item->anchor);
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return unavailable("target changed or the edit is invalid");
  return {std::make_unique<RemovePedalSpanCommand>(item->node, item->track,
                                                   item->stave, item->anchor),
          ""};
}

}  // namespace graphscore
