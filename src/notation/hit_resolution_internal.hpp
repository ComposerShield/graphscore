// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <graphscore/core/rational.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/core/voice.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/notation/notation_types.hpp>

namespace graphscore {
class NodeTimeline;
class Project;
class TrackLane;
class VoiceContent;

struct ResolvedStaffSite {
  const SystemLayout*      system = nullptr;
  const StaffSystemLayout* staff  = nullptr;
};

struct ResolvedInsertionSite {
  const StaffSystemLayout* staff    = nullptr;
  const MeasureLayout*     measure  = nullptr;
  const NodeTimeline*      timeline = nullptr;
  Rational                 resolved_onset;
  const TrackLane*         lane          = nullptr;
  const VoiceContent*      voice_content = nullptr;
};
enum class ResolvedEntityKind : std::uint8_t {
  kNone,
  kNote,
  kChord,
  kRest,
  kChordNote,
  kGraceNote
};

struct ResolvedEntity {
  ResolvedEntityKind kind = ResolvedEntityKind::kNone;
  NotationEntityId   id;
  const VoiceEvent*  event = nullptr;
};

struct ResolvedVoiceEntity {
  const SystemLayout*      system = nullptr;
  const StaffSystemLayout* staff  = nullptr;
  Voice                    voice;
  ResolvedEntity           entity;
  const VoiceContent*      content = nullptr;
};

[[nodiscard]] std::optional<ResolvedStaffSite> resolve_staff_at(
    const NotationLayout&, NotationPoint);
[[nodiscard]] const MeasureLayout* resolve_measure_at(const SystemLayout&,
                                                      NotationPoint);
[[nodiscard]] std::optional<ResolvedInsertionSite> resolve_insertion_site(
    const Project&, const NotationLayout&, Voice, NotationPoint);
[[nodiscard]] std::optional<ResolvedVoiceEntity> resolve_entity_constrained(
    const Project&, const NotationLayout&, const HitRegion&);
[[nodiscard]] std::optional<ResolvedVoiceEntity> resolve_hit_entity(
    const Project&, const NotationLayout&, const HitResult&);
[[nodiscard]] bool hit_id_ends_with(const NotationId&, std::string_view);
[[nodiscard]] std::optional<Selection> resolve_marking_selection(
    const Project&, const NotationLayout&, const HitResult&);
}  // namespace graphscore
