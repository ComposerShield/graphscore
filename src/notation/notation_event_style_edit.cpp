// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/notation_editing.hpp>

#include "notation_style_edit_internal.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/event_style_command.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

using style_edit::EventTarget;
using style_edit::resolve_selected_event;
using style_edit::resolve_top_level_event;
using style_edit::resolve_voice;
using style_edit::selects_grace_note;
using style_edit::unavailable;

namespace {

[[nodiscard]] bool valid_articulation(Articulation articulation) {
  return std::find(kAllArticulations.begin(), kAllArticulations.end(),
                   articulation) != kAllArticulations.end();
}

[[nodiscard]] bool valid_stem(StemDirection stem) {
  return stem == StemDirection::kAuto || stem == StemDirection::kUp ||
         stem == StemDirection::kDown;
}

[[nodiscard]] std::optional<std::pair<EventTarget, Articulation>>
resolve_selected_articulation(const Project&   project,
                              const Selection& selection) {
  const auto* markings = std::get_if<MarkingSet>(&selection);
  if (markings == nullptr || markings->items().size() != 1u ||
      !validate_selection(project, selection).empty())
    return std::nullopt;
  const MarkingItem& item = markings->items().front();
  if (item.kind != MarkingKind::kArticulation || !item.voice.has_value() ||
      !item.articulation.has_value())
    return std::nullopt;
  const VoiceContent* voice =
      resolve_voice(project, item.node, item.track, item.stave, *item.voice);
  auto target = voice == nullptr ? std::nullopt
                                 : resolve_top_level_event(
                                       *voice, item.node, item.track,
                                       item.stave, *item.voice, item.anchor);
  if (!target.has_value())
    return std::nullopt;
  return std::pair{*target, *item.articulation};
}

}  // namespace

NotationEditCommandResult make_articulation_edit_command(
    const Project& project, const Selection& selection, ArticulationEdit edit,
    Articulation articulation) {
  if (edit != ArticulationEdit::kRemove && !valid_articulation(articulation))
    return unavailable("unknown articulation");
  if (selects_grace_note(project, selection))
    return unavailable("grace notes do not support event style editing");
  std::optional<EventTarget>  target;
  std::optional<Articulation> replaced;
  if (edit == ArticulationEdit::kApply) {
    target = resolve_selected_event(project, selection);
    if (!target.has_value())
      return unavailable("requires one live note or chord event");
  } else {
    const auto marking = resolve_selected_articulation(project, selection);
    if (!marking.has_value())
      return unavailable("requires one live articulation marking");
    target   = marking->first;
    replaced = marking->second;
    if (edit == ArticulationEdit::kRemove)
      articulation = *replaced;
  }
  const std::vector<Articulation>& values =
      *event_articulations(*target->value);
  if (edit != ArticulationEdit::kRemove &&
      std::find(values.begin(), values.end(), articulation) != values.end()) {
    return unavailable("articulation is already present");
  }
  if (edit != ArticulationEdit::kRemove &&
      is_duration_articulation(articulation) &&
      std::any_of(values.begin(), values.end(), [&](Articulation value) {
        return is_duration_articulation(value) && value != replaced;
      })) {
    return unavailable("conflicts with the existing duration articulation");
  }
  auto command = std::make_unique<EventStyleCommand>(
      target->node, target->track, target->stave, target->voice, target->event,
      edit, articulation, replaced);
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return unavailable("target changed or the edit is invalid");
  return {std::make_unique<EventStyleCommand>(
              target->node, target->track, target->stave, target->voice,
              target->event, edit, articulation, replaced),
          ""};
}

NotationEditCommandResult make_stem_edit_command(const Project&   project,
                                                 const Selection& selection,
                                                 StemDirection    stem) {
  if (!valid_stem(stem))
    return unavailable("unknown stem direction");
  if (selects_grace_note(project, selection))
    return unavailable("grace notes do not support event style editing");
  const auto target = resolve_selected_event(project, selection);
  if (!target.has_value())
    return unavailable("requires one live note or chord event");
  if (event_stem(*target->value) == stem)
    return unavailable("stem direction is already set");
  auto command = std::make_unique<EventStyleCommand>(
      target->node, target->track, target->stave, target->voice, target->event,
      stem);
  Project candidate = project;
  if (!command->execute(candidate).ok())
    return unavailable("target changed or the edit is invalid");
  return {std::make_unique<EventStyleCommand>(target->node, target->track,
                                              target->stave, target->voice,
                                              target->event, stem),
          ""};
}

}  // namespace graphscore
