// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/notation_editing.hpp>

#include "notation_style_edit_internal.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/add_beam_override_command.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/remove_beam_override_command.hpp>
#include <graphscore/domain/replace_beam_override_command.hpp>

namespace graphscore {
namespace {

constexpr const char* kRangeReason =
    "requires an exact range of complete events on one live staff and voice";

struct BeamRangeTarget {
  NodeId                        node;
  TrackId                       track;
  StaveId                       stave;
  Voice                         voice;
  std::vector<NotationEntityId> events;
  const VoiceContent*           content;
};

struct IndexedEvent {
  std::size_t      index;
  NotationEntityId id;
};

[[nodiscard]] std::optional<std::vector<IndexedEvent>> events_in_span(
    const VoiceContent& content, MusicalSpan span) {
  if (!(span.start < span.end))
    return std::nullopt;
  Rational                  onset;
  bool                      started = false;
  std::vector<IndexedEvent> selected;
  for (std::size_t index = 0; index < content.events().size(); ++index) {
    const VoiceEvent& event = content.events()[index];
    const Rational    end   = onset + event_duration(event).resolved();
    if (!started && onset == span.start)
      started = true;
    if (started && onset < span.end) {
      selected.push_back({index, event_id(event)});
      if (end == span.end)
        return selected;
      if (end > span.end)
        return std::nullopt;
    }
    onset = end;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<BeamRangeTarget> resolve_target(
    const Project& project, const Selection& selection,
    std::string& unavailable_reason) {
  const auto* set = std::get_if<ArbitraryRangeSet>(&selection);
  if (set == nullptr || set->items().empty() ||
      !validate_selection(project, selection).empty()) {
    unavailable_reason = kRangeReason;
    return std::nullopt;
  }
  const ArbitraryRangeItem& first   = set->items().front();
  const VoiceContent*       content = style_edit::resolve_voice(
      project, first.node, first.track, first.stave, first.voice);
  if (content == nullptr) {
    unavailable_reason = kRangeReason;
    return std::nullopt;
  }

  std::vector<IndexedEvent> selected;
  for (const ArbitraryRangeItem& item : set->items()) {
    if (item.node != first.node || item.track != first.track ||
        item.stave != first.stave || item.voice != first.voice) {
      unavailable_reason = kRangeReason;
      return std::nullopt;
    }
    auto span_events = events_in_span(*content, item.span);
    if (!span_events.has_value()) {
      unavailable_reason = kRangeReason;
      return std::nullopt;
    }
    selected.insert(selected.end(), span_events->begin(), span_events->end());
  }
  std::ranges::sort(selected, {}, &IndexedEvent::index);
  if (selected.size() < 2u) {
    unavailable_reason = "requires a range of at least two events";
    return std::nullopt;
  }
  for (std::size_t index = 1; index < selected.size(); ++index) {
    if (selected[index].index != selected[index - 1].index + 1u) {
      unavailable_reason = "selected events must be contiguous";
      return std::nullopt;
    }
  }
  if (std::ranges::any_of(selected, [&](const IndexedEvent& item) {
        return !event_is_beamable(content->events()[item.index]);
      })) {
    unavailable_reason = "every selected event must be beamable";
    return std::nullopt;
  }

  std::vector<NotationEntityId> ids;
  ids.reserve(selected.size());
  for (const IndexedEvent& item : selected)
    ids.push_back(item.id);
  return BeamRangeTarget{first.node,  first.track,    first.stave,
                         first.voice, std::move(ids), content};
}

[[nodiscard]] std::unique_ptr<Command> add_command(
    const BeamRangeTarget& target, BeamOverride override) {
  return std::make_unique<AddBeamOverrideCommand>(target.node, target.track,
                                                  target.stave, target.voice,
                                                  std::move(override));
}

[[nodiscard]] std::unique_ptr<Command> remove_command(
    const BeamRangeTarget& target, NotationEntityId id) {
  return std::make_unique<RemoveBeamOverrideCommand>(
      target.node, target.track, target.stave, target.voice, id);
}

[[nodiscard]] std::unique_ptr<Command> replace_command(
    const BeamRangeTarget& target, const BeamOverride& existing,
    BeamOverride::Kind kind) {
  BeamOverride replacement{existing.id, kind, target.events};
  return std::make_unique<ReplaceBeamOverrideCommand>(
      target.node, target.track, target.stave, target.voice, existing.id,
      std::move(replacement));
}

}  // namespace

NotationEditCommandResult make_beam_override_edit_command(
    const Project& project, const Selection& selection, MarkingEdit edit,
    BeamOverride::Kind kind) {
  if (edit != MarkingEdit::kApply && edit != MarkingEdit::kRemove)
    return style_edit::unavailable("beam overrides cannot be changed");
  if (kind != BeamOverride::Kind::kBreak && kind != BeamOverride::Kind::kJoin) {
    return style_edit::unavailable("unknown beam override kind");
  }

  std::string reason;
  const auto  target = resolve_target(project, selection, reason);
  if (!target.has_value())
    return style_edit::unavailable(std::move(reason));

  std::vector<const BeamOverride*> exact;
  for (const BeamOverride& override_ : target->content->beam_overrides()) {
    if (override_.events == target->events)
      exact.push_back(&override_);
  }
  if (exact.size() > 1u)
    return style_edit::unavailable(
        "conflicting beam overrides exist on this exact range");

  std::unique_ptr<Command>    command;
  std::optional<BeamOverride> added;
  if (edit == MarkingEdit::kRemove) {
    if (exact.empty())
      return style_edit::unavailable(
          "no beam override exists on this exact range");
    command = remove_command(*target, exact.front()->id);
  } else if (exact.empty()) {
    added   = make_beam_override(kind, target->events);
    command = add_command(*target, *added);
  } else if (exact.front()->kind == kind) {
    return style_edit::unavailable(
        kind == BeamOverride::Kind::kBreak
            ? "beam break is already applied to this exact range"
            : "beam join is already applied to this exact range");
  } else {
    command = replace_command(*target, *exact.front(), kind);
  }
  if (command == nullptr)
    return style_edit::unavailable("could not construct beam override edit");

  Project candidate = project;
  if (!command->execute(candidate).ok())
    return style_edit::unavailable("target changed or the edit is invalid");

  // Trial execution consumes a command's state, so return an identical fresh
  // operation for the caller's history transaction.
  if (edit == MarkingEdit::kRemove) {
    command = remove_command(*target, exact.front()->id);
  } else if (exact.empty()) {
    command = add_command(*target, *added);
  } else {
    command = replace_command(*target, *exact.front(), kind);
  }
  if (command == nullptr)
    return style_edit::unavailable("could not construct beam override edit");
  return {std::move(command), ""};
}

}  // namespace graphscore
