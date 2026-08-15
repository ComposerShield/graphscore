// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>
#include <graphscore/domain/tuplet_command.hpp>
#include <graphscore/notation/notation_editing.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {
namespace {

struct RangeTarget {
  ArbitraryRangeItem            item;
  std::vector<NotationEntityId> events;
};

[[nodiscard]] std::optional<RangeTarget> resolve_range(
    const Project& project, const Selection& selection) {
  const auto* set = std::get_if<ArbitraryRangeSet>(&selection);
  if (set == nullptr || set->items().size() != 1u ||
      !validate_selection(project, selection).empty()) {
    return std::nullopt;
  }
  const ArbitraryRangeItem& item = set->items().front();
  if (item.span.start == item.span.end)
    return std::nullopt;
  const Node*        node = project.find_node(item.node);
  const TrackLane*   lane = node == nullptr ? nullptr : node->lane(item.track);
  const StaveVoices* stave =
      lane == nullptr ? nullptr : lane->stave(item.stave);
  if (stave == nullptr)
    return std::nullopt;
  const VoiceContent& voice = stave->voice(item.voice);

  Rational                      onset;
  bool                          started = false;
  std::vector<NotationEntityId> ids;
  for (const VoiceEvent& event : voice.events()) {
    const Rational end = onset + event_duration(event).resolved();
    if (!started && onset == item.span.start)
      started = true;
    if (started && onset < item.span.end) {
      if (event_tuplet_group(event).has_value() ||
          event_duration(event).tuplet().has_value()) {
        return std::nullopt;
      }
      ids.push_back(event_id(event));
      if (end == item.span.end)
        return RangeTarget{item, std::move(ids)};
      if (end > item.span.end)
        return std::nullopt;
    }
    onset = end;
  }
  return std::nullopt;
}

struct MarkingTarget {
  MarkingItem   item;
  TupletGroupId group;
};

[[nodiscard]] std::optional<MarkingTarget> resolve_marking(
    const Project& project, const Selection& selection) {
  const auto* set = std::get_if<MarkingSet>(&selection);
  if (set == nullptr || set->items().size() != 1u ||
      set->items().front().kind != MarkingKind::kTuplet ||
      !set->items().front().voice.has_value() ||
      !validate_selection(project, selection).empty()) {
    return std::nullopt;
  }
  const MarkingItem&  item  = set->items().front();
  const Node*         node  = project.find_node(item.node);
  const TrackLane*    lane  = node->lane(item.track);
  const VoiceContent& voice = lane->stave(item.stave)->voice(*item.voice);
  for (const VoiceEvent& event : voice.events()) {
    if (event_id(event) == item.anchor) {
      const auto& group = event_tuplet_group(event);
      if (group.has_value())
        return MarkingTarget{item, *group};
      break;
    }
  }
  return std::nullopt;
}

}  // namespace

std::string tuplet_label(TupletRatio ratio) {
  std::string label = std::to_string(ratio.played());
  if (!is_conventional_tuplet_ratio(ratio))
    label += ":" + std::to_string(ratio.normal());
  return label;
}

std::unique_ptr<Command> make_tuplet_create_command(const Project&   project,
                                                    const Selection& selection,
                                                    TupletRatio      ratio) {
  const auto target = resolve_range(project, selection);
  if (!target.has_value() || ratio.played() == ratio.normal())
    return nullptr;
  auto    command = std::make_unique<TupletCommand>(TupletCommand::create_group(
      target->item.node, target->item.track, target->item.stave,
      target->item.voice, target->events, ratio));
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return nullptr;
  return std::make_unique<TupletCommand>(TupletCommand::create_group(
      target->item.node, target->item.track, target->item.stave,
      target->item.voice, target->events, ratio));
}

std::unique_ptr<Command> make_tuplet_change_command(const Project&   project,
                                                    const Selection& selection,
                                                    TupletRatio      ratio) {
  const auto target = resolve_marking(project, selection);
  if (!target.has_value() || ratio.played() == ratio.normal())
    return nullptr;
  auto    command = std::make_unique<TupletCommand>(TupletCommand::change_group(
      target->item.node, target->item.track, target->item.stave,
      *target->item.voice, target->group, ratio));
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return nullptr;
  return std::make_unique<TupletCommand>(TupletCommand::change_group(
      target->item.node, target->item.track, target->item.stave,
      *target->item.voice, target->group, ratio));
}

std::unique_ptr<Command> make_tuplet_remove_command(
    const Project& project, const Selection& selection) {
  const auto target = resolve_marking(project, selection);
  if (!target.has_value())
    return nullptr;
  auto    command = std::make_unique<TupletCommand>(TupletCommand::remove_group(
      target->item.node, target->item.track, target->item.stave,
      *target->item.voice, target->group));
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return nullptr;
  return std::make_unique<TupletCommand>(TupletCommand::remove_group(
      target->item.node, target->item.track, target->item.stave,
      *target->item.voice, target->group));
}

std::optional<Selection> selection_after_tuplet_create(
    const Project& project, const Selection& selection) {
  const auto target = resolve_range(project, selection);
  if (!target.has_value())
    return std::nullopt;
  auto set = MarkingSet::create(
      {MarkingItem{target->item.node, target->item.track, target->item.stave,
                   target->item.voice, MarkingKind::kTuplet,
                   target->events.front(), std::nullopt}});
  if (!set.has_value())
    return std::nullopt;
  return Selection{*std::move(set)};
}

}  // namespace graphscore
