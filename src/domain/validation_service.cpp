// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/validation_service.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/weighted_selection.hpp>

namespace graphscore {

namespace {

using UuidBytes = std::array<std::uint8_t, Uuid::kSize>;

const Uuid& entity_uuid(const DiagnosticEntity& entity) {
  return std::visit([](const auto& id) -> const Uuid& { return id.value(); },
                    entity);
}

bool entity_less(const DiagnosticEntity& lhs, const DiagnosticEntity& rhs) {
  if (lhs.index() != rhs.index())
    return lhs.index() < rhs.index();
  return entity_uuid(lhs).bytes() < entity_uuid(rhs).bytes();
}

bool scope_less(const DiagnosticScope& lhs, const DiagnosticScope& rhs) {
  if (entity_less(lhs.entity, rhs.entity))
    return true;
  if (entity_less(rhs.entity, lhs.entity))
    return false;
  if (lhs.node.has_value() != rhs.node.has_value())
    return !lhs.node.has_value();
  if (lhs.node.has_value() &&
      lhs.node->value().bytes() != rhs.node->value().bytes()) {
    return lhs.node->value().bytes() < rhs.node->value().bytes();
  }
  if (lhs.track.has_value() != rhs.track.has_value())
    return !lhs.track.has_value();
  if (!lhs.track.has_value())
    return false;
  return lhs.track->value().bytes() < rhs.track->value().bytes();
}

bool valid_clef(const Clef clef) {
  return clef_from_index(to_index(clef)).has_value();
}

bool valid_mode(const Mode mode) {
  return mode == Mode::kMajor || mode == Mode::kMinor;
}

bool valid_note_value(const NoteValue value) {
  return value >= NoteValue::kWhole && value <= NoteValue::kSixtyFourth;
}

bool valid_tempo_segment(const TempoSegmentKind kind) {
  return kind == TempoSegmentKind::kStep || kind == TempoSegmentKind::kLinear ||
         kind == TempoSegmentKind::kSmooth;
}

DiagnosticCode notation_code(const NotationDiagnosticCode code) {
  switch (code) {
    case NotationDiagnosticCode::kDanglingTie:
      return DiagnosticCode::kDanglingTie;
    case NotationDiagnosticCode::kTiePitchMismatch:
      return DiagnosticCode::kTiePitchMismatch;
    case NotationDiagnosticCode::kConflictingDurationArticulation:
      return DiagnosticCode::kConflictingDurationArticulation;
    case NotationDiagnosticCode::kHairpinDanglingEndpoint:
      return DiagnosticCode::kHairpinDanglingEndpoint;
    case NotationDiagnosticCode::kHairpinNotOrdered:
      return DiagnosticCode::kHairpinNotOrdered;
    case NotationDiagnosticCode::kSlurDanglingEndpoint:
      return DiagnosticCode::kSlurDanglingEndpoint;
    case NotationDiagnosticCode::kSlurNotOrdered:
      return DiagnosticCode::kSlurNotOrdered;
    case NotationDiagnosticCode::kSlurAttachedToRest:
      return DiagnosticCode::kSlurAttachedToRest;
    case NotationDiagnosticCode::kPedalSpanNotOrdered:
      return DiagnosticCode::kPedalSpanNotOrdered;
    case NotationDiagnosticCode::kPedalSpanOutOfRange:
      return DiagnosticCode::kPedalSpanOutOfRange;
    case NotationDiagnosticCode::kIncompleteTupletGroup:
      return DiagnosticCode::kIncompleteTupletGroup;
    case NotationDiagnosticCode::kInvalidBeamOverride:
      return DiagnosticCode::kInvalidBeamOverride;
    case NotationDiagnosticCode::kDynamicDanglingReference:
      return DiagnosticCode::kDynamicDanglingReference;
    case NotationDiagnosticCode::kGraceGroupPrincipalNotSounding:
      return DiagnosticCode::kGraceGroupPrincipalNotSounding;
    case NotationDiagnosticCode::kDuplicateArticulation:
      return DiagnosticCode::kDuplicateArticulation;
  }
  return DiagnosticCode::kDynamicDanglingReference;
}

void append_voice_entities(const VoiceContent&            voice,
                           std::vector<DiagnosticEntity>& entities) {
  for (const VoiceEvent& event : voice.events()) {
    entities.emplace_back(event_id(event));
    if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const ChordNote& note : chord->notes)
        entities.emplace_back(note.id);
    }
  }
  for (const DynamicMarking& marking : voice.dynamics())
    entities.emplace_back(marking.id);
  for (const Hairpin& marking : voice.hairpins())
    entities.emplace_back(marking.id);
  for (const Slur& marking : voice.slurs())
    entities.emplace_back(marking.id);
  for (const BeamOverride& marking : voice.beam_overrides())
    entities.emplace_back(marking.id);
  for (const GraceGroup& group : voice.grace_groups()) {
    entities.emplace_back(group.id);
    for (const GraceNote& note : group.notes)
      entities.emplace_back(note.id);
  }
}

void append_lane_entities(const TrackLane&               lane,
                          std::vector<DiagnosticEntity>& entities) {
  for (const StaveId stave_id : lane.stave_ids()) {
    const StaveVoices* stave = lane.stave(stave_id);
    if (stave == nullptr)
      continue;
    for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
      const std::optional<Voice> voice = Voice::create(index);
      assert(voice.has_value());
      append_voice_entities(stave->voice(*voice), entities);
    }
    if (const std::vector<PedalSpan>* spans = lane.pedal_spans(stave_id)) {
      for (const PedalSpan& span : *spans)
        entities.emplace_back(span.id);
    }
  }
}

const StaveDefinition* find_stave(const Project& project, const StaveId id) {
  const auto in_tracks =
      [id](const std::vector<Track>& tracks) -> const StaveDefinition* {
    for (const Track& track : tracks) {
      for (const StaveDefinition& stave : track.layout().staves()) {
        if (stave.id == id)
          return &stave;
      }
    }
    return nullptr;
  };
  if (const StaveDefinition* stave = in_tracks(project.active_tracks()))
    return stave;
  return in_tracks(project.archived_tracks());
}

class Validator {
 public:
  Validator(const Project& project, const bool complete,
            std::unordered_set<NodeId>    selected_nodes,
            std::vector<DiagnosticEntity> invalidated_entities = {})
      : project_(project),
        complete_(complete),
        selected_nodes_(std::move(selected_nodes)) {
    report_.complete             = complete;
    report_.invalidated_entities = std::move(invalidated_entities);
  }

  ValidationReport run() {
    if (validate_uuid_namespace() && !complete_) {
      // A duplicate makes occurrence ownership ambiguous: two different
      // locations can have the same strongly typed diagnostic entity. Expand
      // globally so the affected-entity replacement contract remains exact.
      complete_        = true;
      report_.complete = true;
    }
    if (complete_) {
      mark(project_.id());
      validate_project_references();
      validate_event_registry();
    }

    for (const Node& node : project_.nodes()) {
      if (complete_ || selected_nodes_.contains(node.id()))
        validate_node(node);
    }
    if (!complete_)
      validate_inbound_dependencies();

    std::ranges::sort(report_.diagnostics, DiagnosticLess{});
    report_.diagnostics.erase(
        std::unique(report_.diagnostics.begin(), report_.diagnostics.end()),
        report_.diagnostics.end());
    std::ranges::sort(report_.affected_scopes, scope_less);
    report_.affected_scopes.erase(std::unique(report_.affected_scopes.begin(),
                                              report_.affected_scopes.end()),
                                  report_.affected_scopes.end());
    std::ranges::sort(report_.invalidated_entities, entity_less);
    report_.invalidated_entities.erase(
        std::unique(report_.invalidated_entities.begin(),
                    report_.invalidated_entities.end()),
        report_.invalidated_entities.end());
    return std::move(report_);
  }

 private:
  template <typename Id>
  void mark(const Id id, const std::optional<NodeId> node = std::nullopt) {
    report_.affected_scopes.push_back(
        {DiagnosticEntity(id), node, std::nullopt});
  }

  void add(const DiagnosticEntity& entity, const DiagnosticCode code,
           std::string text, const std::optional<NodeId> node = std::nullopt,
           const std::optional<TrackId> track = std::nullopt) {
    assert(!text.empty());
    report_.diagnostics.push_back(Diagnostic{entity, node, track,
                                             DiagnosticSeverity::kError, code,
                                             std::move(text)});
    report_.affected_scopes.push_back({entity, node, track});
  }

  [[nodiscard]] std::vector<DiagnosticScope> all_identity_occurrences() const {
    std::vector<DiagnosticScope> entities;
    entities.push_back({project_.id(), std::nullopt, std::nullopt});
    const auto append_tracks = [&entities](const std::vector<Track>& tracks) {
      for (const Track& track : tracks) {
        entities.push_back({track.id(), std::nullopt, std::nullopt});
        for (const StaveDefinition& stave : track.layout().staves())
          entities.push_back({stave.id, std::nullopt, track.id()});
      }
    };
    append_tracks(project_.active_tracks());
    append_tracks(project_.archived_tracks());
    for (const EventDefinition& event : project_.events().definitions())
      entities.push_back({event.id, std::nullopt, std::nullopt});
    for (const Node& node : project_.nodes()) {
      entities.push_back({node.id(), node.id(), std::nullopt});
      for (const InputConnector& input : node.inputs())
        entities.push_back({input.id(), node.id(), std::nullopt});
      for (const OutputConnector& output : node.outputs())
        entities.push_back({output.id(), node.id(), std::nullopt});
      for (const TrackId track_id : node.lane_ids()) {
        const TrackLane* lane = node.lane(track_id);
        assert(lane != nullptr);
        std::vector<DiagnosticEntity> lane_entities;
        append_lane_entities(*lane, lane_entities);
        for (const DiagnosticEntity& entity : lane_entities)
          entities.push_back({entity, node.id(), track_id});
      }
    }
    return entities;
  }

  bool validate_uuid_namespace() {
    std::map<UuidBytes, std::vector<DiagnosticScope>> occurrences;
    for (const DiagnosticScope& occurrence : all_identity_occurrences()) {
      occurrences[entity_uuid(occurrence.entity).bytes()].push_back(occurrence);
      if (entity_uuid(occurrence.entity) == Uuid{}) {
        add(occurrence.entity, DiagnosticCode::kNilUuid,
            "stable entity identity must not be the nil UUID", occurrence.node,
            occurrence.track);
      }
    }
    bool found_duplicate = false;
    for (const auto& [bytes, entities] : occurrences) {
      static_cast<void>(bytes);
      if (entities.size() < 2)
        continue;
      found_duplicate = true;
      for (const DiagnosticScope& occurrence : entities) {
        add(occurrence.entity, DiagnosticCode::kDuplicateUuid,
            "stable UUID is used by more than one project entity",
            occurrence.node, occurrence.track);
      }
    }
    return found_duplicate;
  }

  void validate_project_references() {
    const std::optional<NodeId> start    = project_.start_node();
    const NodeId                start_id = start.value_or(NodeId{});
    if (start.has_value() && project_.find_node(start_id) == nullptr) {
      add(start_id, DiagnosticCode::kDanglingStartNode,
          "designated start node is not owned by this project");
    }
    if (!valid_note_value(project_.default_tempo().beat_unit())) {
      add(project_.id(), DiagnosticCode::kIllegalTempoLane,
          "project default tempo has an illegal beat unit");
    }
    const auto validate_track_layout =
        [this](const std::vector<Track>& tracks) {
          for (const Track& track : tracks) {
            for (const StaveDefinition& stave : track.layout().staves()) {
              if (!valid_clef(stave.default_clef)) {
                add(stave.id, DiagnosticCode::kIllegalClefChange,
                    "track stave has an illegal default clef");
              }
            }
          }
        };
    validate_track_layout(project_.active_tracks());
    validate_track_layout(project_.archived_tracks());
  }

  void validate_event_registry() {
    std::map<std::string, std::vector<EventId>> names;
    for (const EventDefinition& event : project_.events().definitions()) {
      mark(event.id);
      names[event.name].push_back(event.id);
    }
    for (const auto& [name, ids] : names) {
      static_cast<void>(name);
      if (ids.size() < 2)
        continue;
      for (const EventId id : ids) {
        add(id, DiagnosticCode::kDuplicateEventName,
            "registered event name is not unique under case-sensitive "
            "comparison");
      }
    }
  }

  void validate_node(const Node& node) {
    mark(node.id(), node.id());
    validate_lane_alignment(node);
    validate_timeline(node);
    validate_outputs(node, true);
  }

  void validate_lane_alignment(const Node& node) {
    std::size_t known_lane_count = 0;
    for (const Track& track : project_.active_tracks()) {
      mark(track.id(), node.id());
      const TrackLane* lane = node.lane(track.id());
      if (lane == nullptr) {
        add(node.id(), DiagnosticCode::kMissingTrackLane,
            "active track has no aligned lane in this node", node.id(),
            track.id());
        continue;
      }
      ++known_lane_count;
      validate_lane(node, track, *lane);
    }
    for (const Track& track : project_.archived_tracks()) {
      mark(track.id(), node.id());
      if (const TrackLane* lane = node.lane(track.id())) {
        ++known_lane_count;
        validate_lane(node, track, *lane);
      }
    }
    if (node.lane_count() != known_lane_count) {
      for (const TrackId track_id : node.lane_ids()) {
        if (project_.find_active_track(track_id) != nullptr ||
            project_.find_archived_track(track_id) != nullptr) {
          continue;
        }
        add(track_id, DiagnosticCode::kUnexpectedTrackLane,
            "node contains a lane for no active or archived project track",
            node.id(), track_id);
        const TrackLane* lane = node.lane(track_id);
        assert(lane != nullptr);
        validate_lane_contents(node, track_id, *lane);
      }
    }
  }

  void validate_lane(const Node& node, const Track& track,
                     const TrackLane& lane) {
    const NodeTimeline* timeline = node.timeline();
    const bool          structurally_applicable =
        timeline != nullptr || lane.stave_count() != 0;
    if (structurally_applicable) {
      for (const StaveDefinition& definition : track.layout().staves()) {
        if (!lane.has_stave(definition.id)) {
          add(definition.id, DiagnosticCode::kMissingStave,
              "track lane is missing a stave from the track's fixed layout",
              node.id(), track.id());
        }
      }
    }
    for (const StaveId stave_id : lane.stave_ids()) {
      report_.affected_scopes.push_back({stave_id, node.id(), track.id()});
      const auto& definitions = track.layout().staves();
      const auto  definition =
          std::ranges::find(definitions, stave_id, &StaveDefinition::id);
      if (definition == definitions.end()) {
        add(stave_id, DiagnosticCode::kUnexpectedStave,
            "track lane contains a stave outside the track's fixed layout",
            node.id(), track.id());
        const StaveVoices* voices = lane.stave(stave_id);
        assert(voices != nullptr);
        validate_stave_contents(node, track.id(), lane, stave_id, *voices);
        continue;
      }
      if (!valid_clef(definition->default_clef)) {
        add(stave_id, DiagnosticCode::kIllegalClefChange,
            "track stave has an illegal default clef", node.id(), track.id());
      }
      if (timeline != nullptr && !timeline->has_clef_lane(stave_id)) {
        add(stave_id, DiagnosticCode::kMissingClefLane,
            "notation stave has no matching clef lane in the node timeline",
            node.id(), track.id());
      }
      const StaveVoices* voices = lane.stave(stave_id);
      assert(voices != nullptr);
      validate_stave_contents(node, track.id(), lane, stave_id, *voices);
    }
  }

  void validate_stave_contents(const Node& node, const TrackId track_id,
                               const TrackLane& lane, const StaveId stave_id,
                               const StaveVoices& voices) {
    const NodeTimeline* timeline = node.timeline();
    for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
      const std::optional<Voice> voice = Voice::create(index);
      assert(voice.has_value());
      const VoiceContent& content = voices.voice(*voice);
      for (const NotationDiagnostic& diagnostic :
           validate_voice_references(content)) {
        add(diagnostic.entity_id, notation_code(diagnostic.code),
            diagnostic.message, node.id(), track_id);
      }
      if (timeline != nullptr &&
          !content.check_complete(timeline->node_end()).ok()) {
        add(stave_id, DiagnosticCode::kRhythmIncomplete,
            "voice does not rhythmically tile the complete node timeline",
            node.id(), track_id);
      }
      std::vector<DiagnosticEntity> entities;
      append_voice_entities(content, entities);
      for (const DiagnosticEntity& entity : entities)
        report_.affected_scopes.push_back({entity, node.id(), track_id});
    }
    if (const std::vector<PedalSpan>* spans = lane.pedal_spans(stave_id)) {
      if (timeline != nullptr) {
        for (const NotationDiagnostic& diagnostic :
             validate_pedal_spans(*spans, timeline->node_end())) {
          add(diagnostic.entity_id, notation_code(diagnostic.code),
              diagnostic.message, node.id(), track_id);
        }
      } else {
        // Ordering is intrinsic to the span and remains checkable without
        // timeline extent; only the out-of-range check needs node_end().
        for (const PedalSpan& span : *spans) {
          if (!(span.start < span.end)) {
            add(span.id, DiagnosticCode::kPedalSpanNotOrdered,
                "pedal span start does not strictly precede its end", node.id(),
                track_id);
          }
        }
      }
      for (const PedalSpan& span : *spans)
        report_.affected_scopes.push_back({span.id, node.id(), track_id});
    }
  }

  void validate_lane_contents(const Node& node, const TrackId track_id,
                              const TrackLane& lane) {
    for (const StaveId stave_id : lane.stave_ids()) {
      report_.affected_scopes.push_back({stave_id, node.id(), track_id});
      const StaveVoices* voices = lane.stave(stave_id);
      assert(voices != nullptr);
      validate_stave_contents(node, track_id, lane, stave_id, *voices);
    }
  }

  void validate_timeline(const Node& node) {
    const NodeTimeline* timeline = node.timeline();
    if (timeline == nullptr)
      return;

    const MeasureMap& measures = timeline->measures();
    for (std::size_t index = 0; index < measures.measure_count(); ++index) {
      const Measure& measure = measures.measure(index);
      if (!TimeSignature::create(measure.time_signature.numerator(),
                                 measure.time_signature.denominator())
               .has_value()) {
        add(node.id(), DiagnosticCode::kIllegalTimeSignature,
            "measure contains an illegal time signature", node.id());
      }
      if (!valid_mode(measure.key_signature.mode()) ||
          !KeySignature::create(measure.key_signature.fifths(),
                                measure.key_signature.mode())
               .has_value()) {
        add(node.id(), DiagnosticCode::kIllegalKeySignature,
            "measure contains an illegal key signature", node.id());
      }
    }

    const std::optional<Rational> pickdown = timeline->pickdown_duration();
    if (pickdown.has_value()) {
      const Rational duration = pickdown.value_or(Rational(0));
      const Rational boundary_measure =
          measures.measure_length(measures.measure_count() - 1);
      if (duration <= Rational(0) || duration >= boundary_measure) {
        add(node.id(), DiagnosticCode::kIllegalPickdown,
            "pickdown must be positive and shorter than its boundary measure",
            node.id());
      }
    }

    for (const StaveId stave_id : timeline->clef_stave_ids()) {
      mark(stave_id, node.id());
      const StaveDefinition* definition = find_stave(project_, stave_id);
      if (definition == nullptr) {
        add(stave_id, DiagnosticCode::kUnexpectedClefLane,
            "timeline clef lane references no project stave", node.id());
        continue;
      }
      const ClefLane* lane = timeline->clef_lane(stave_id);
      assert(lane != nullptr);
      if (!valid_clef(lane->default_clef())) {
        add(stave_id, DiagnosticCode::kIllegalClefChange,
            "timeline clef lane has an illegal default clef", node.id());
      } else if (lane->default_clef() != definition->default_clef) {
        add(stave_id, DiagnosticCode::kClefDefaultMismatch,
            "clef lane default differs from the stave layout default",
            node.id());
      }
      for (const ClefChange& change : lane->changes()) {
        if (change.position < Rational(0) ||
            change.position >= timeline->node_end() ||
            !valid_clef(change.clef)) {
          add(stave_id, DiagnosticCode::kIllegalClefChange,
              "clef change position or clef value is outside the timeline "
              "contract",
              node.id());
        }
      }
    }

    if (const TempoLane* tempo = timeline->tempo()) {
      const std::optional<TempoLane> rebuilt =
          TempoLane::create(tempo->points(), Rational(0), timeline->node_end());
      const bool points_legal =
          std::ranges::all_of(tempo->points(), [](const TempoPoint& point) {
            return valid_note_value(point.tempo.beat_unit()) &&
                   valid_tempo_segment(point.segment_kind);
          });
      if (!rebuilt.has_value() || tempo->start() != Rational(0) ||
          tempo->end() != timeline->node_end() || !points_legal) {
        add(node.id(), DiagnosticCode::kIllegalTempoLane,
            "tempo lane does not legally cover the complete node timeline",
            node.id());
      }
    }
  }

  void validate_outputs(const Node& node, const bool validate_weight) {
    std::vector<WeightedCandidate> candidates;
    for (const InputConnector& input : node.inputs())
      mark(input.id(), node.id());
    for (const OutputConnector& output : node.outputs()) {
      validate_output(node, output);
      if (output.type() == ConnectorType::kSequential &&
          output.destination().has_value()) {
        candidates.push_back({output.id(), output.weight()});
      }
    }
    if (!validate_weight)
      return;
    const WeightGroupValidity validity = validate_weight_group(candidates);
    if (validity == WeightGroupValidity::kInvalidTotal ||
        validity == WeightGroupValidity::kDenominatorOverflow) {
      add(node.id(), DiagnosticCode::kInvalidRandomWeightTotal,
          "destination-bearing sequential output weights must total exactly "
          "one and remain exactly representable",
          node.id());
    }
  }

  void validate_output(const Node& node, const OutputConnector& output) {
    mark(output.id(), node.id());
    if (output.export_enabled() && !output.destination().has_value()) {
      add(output.id(), DiagnosticCode::kExportDestinationRequired,
          "export-enabled output must have exactly one destination", node.id());
    }
    if (output.export_enabled() && output.type() == ConnectorType::kVertical &&
        !output.event_binding().has_value()) {
      add(output.id(), DiagnosticCode::kVerticalEventRequired,
          "export-enabled vertical output must be bound to an event",
          node.id());
    }
    if (output.destination().has_value()) {
      const ConnectorDestination destination = output.destination().value_or(
          ConnectorDestination{NodeId{}, ConnectorId{}});
      const Node* target = project_.find_node(destination.node);
      if (target == nullptr ||
          target->find_input(destination.connector) == nullptr) {
        add(output.id(), DiagnosticCode::kDanglingGraphDestination,
            "output destination does not resolve to an input connector",
            node.id());
      }
    }
    if (!output.event_binding().has_value())
      return;

    const EventId event = output.event_binding().value_or(EventId{});
    if (project_.events().find_by_id(event) == nullptr) {
      add(output.id(), DiagnosticCode::kDanglingEventBinding,
          "output event binding is not registered in this project", node.id());
    }
    const EventListener* listener = node.find_listener(event);
    if (listener == nullptr) {
      add(output.id(), DiagnosticCode::kMissingEventListener,
          "bound output has no shared node/event listener", node.id());
      return;
    }
    if (listener->bound_type() != output.type()) {
      add(output.id(), DiagnosticCode::kEventListenerTypeMismatch,
          "shared event listener type differs from its bound output type",
          node.id());
    }
    if (listener->policy() == QueuePolicy::kFifo && listener->capacity() == 0) {
      add(output.id(), DiagnosticCode::kInvalidListenerCapacity,
          "FIFO event listener must have non-zero capacity", node.id());
    }
  }

  void validate_inbound_dependencies() {
    for (const Node& source : project_.nodes()) {
      if (selected_nodes_.contains(source.id()))
        continue;
      for (const OutputConnector& output : source.outputs()) {
        const std::optional<ConnectorDestination>& destination =
            output.destination();
        const NodeId destination_node =
            destination.value_or(ConnectorDestination{NodeId{}, ConnectorId{}})
                .node;
        if (destination.has_value() &&
            selected_nodes_.contains(destination_node)) {
          validate_output(source, output);
        }
      }
    }
  }

  const Project&             project_;
  bool                       complete_;
  std::unordered_set<NodeId> selected_nodes_;
  ValidationReport           report_;
};

bool voice_contains(const VoiceContent& voice, const NotationEntityId id) {
  std::vector<DiagnosticEntity> entities;
  append_voice_entities(voice, entities);
  return std::ranges::any_of(entities, [id](const DiagnosticEntity& entity) {
    const auto* notation_id = std::get_if<NotationEntityId>(&entity);
    return notation_id != nullptr && *notation_id == id;
  });
}

bool lane_contains(const TrackLane& lane, const NotationEntityId id) {
  for (const StaveId stave_id : lane.stave_ids()) {
    const StaveVoices* stave = lane.stave(stave_id);
    if (stave == nullptr)
      continue;
    for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
      const std::optional<Voice> voice = Voice::create(index);
      assert(voice.has_value());
      if (voice_contains(stave->voice(*voice), id))
        return true;
    }
    if (const std::vector<PedalSpan>* spans = lane.pedal_spans(stave_id)) {
      if (std::ranges::any_of(
              *spans, [id](const PedalSpan& span) { return span.id == id; }))
        return true;
    }
  }
  return false;
}

bool voice_references(const VoiceContent& voice, const NotationEntityId id) {
  if (std::ranges::any_of(voice.dynamics(),
                          [id](const DynamicMarking& marking) {
                            return marking.at_event == id;
                          })) {
    return true;
  }
  if (std::ranges::any_of(voice.hairpins(), [id](const Hairpin& hairpin) {
        return hairpin.start_event == id || hairpin.end_event == id;
      })) {
    return true;
  }
  if (std::ranges::any_of(voice.slurs(), [id](const Slur& slur) {
        return slur.start_event == id || slur.end_event == id;
      })) {
    return true;
  }
  if (std::ranges::any_of(voice.beam_overrides(),
                          [id](const BeamOverride& override) {
                            return std::ranges::find(override.events, id) !=
                                   override.events.end();
                          })) {
    return true;
  }
  return std::ranges::any_of(
      voice.grace_groups(),
      [id](const GraceGroup& group) { return group.principal_event == id; });
}

void select_notation_dependents(const Project&              project,
                                const NotationEntityId      id,
                                std::unordered_set<NodeId>& selected_nodes) {
  for (const Node& node : project.nodes()) {
    for (const TrackId track_id : node.lane_ids()) {
      const TrackLane* lane = node.lane(track_id);
      assert(lane != nullptr);
      for (const StaveId stave_id : lane->stave_ids()) {
        const StaveVoices* stave = lane->stave(stave_id);
        assert(stave != nullptr);
        for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
          const std::optional<Voice> voice = Voice::create(index);
          assert(voice.has_value());
          if (voice_references(stave->voice(*voice), id))
            selected_nodes.insert(node.id());
        }
      }
    }
  }
}

void select_connector_dependents(const Project& project, const ConnectorId id,
                                 std::unordered_set<NodeId>& selected_nodes) {
  for (const Node& node : project.nodes()) {
    if (std::ranges::any_of(
            node.outputs(), [id](const OutputConnector& output) {
              const std::optional<ConnectorDestination>& destination =
                  output.destination();
              return destination.has_value() && destination->connector == id;
            })) {
      selected_nodes.insert(node.id());
    }
  }
}

void select_notation_owners(const Project& project, const NotationEntityId id,
                            std::unordered_set<NodeId>& selected_nodes) {
  for (const Node& node : project.nodes()) {
    for (const TrackId track_id : node.lane_ids()) {
      if (const TrackLane* lane = node.lane(track_id);
          lane != nullptr && lane_contains(*lane, id))
        selected_nodes.insert(node.id());
    }
  }
}

void select_connector_owners(const Project& project, const ConnectorId id,
                             std::unordered_set<NodeId>& selected_nodes) {
  for (const Node& node : project.nodes()) {
    if (node.find_input(id) != nullptr || node.find_output(id) != nullptr)
      selected_nodes.insert(node.id());
  }
}

}  // namespace

bool DiagnosticLess::operator()(const Diagnostic& lhs,
                                const Diagnostic& rhs) const {
  if (lhs.severity != rhs.severity)
    return lhs.severity < rhs.severity;
  if (lhs.code != rhs.code)
    return lhs.code < rhs.code;
  if (entity_less(lhs.entity, rhs.entity))
    return true;
  if (entity_less(rhs.entity, lhs.entity))
    return false;
  if (lhs.node != rhs.node) {
    if (!lhs.node.has_value())
      return true;
    if (!rhs.node.has_value())
      return false;
    return lhs.node->value().bytes() < rhs.node->value().bytes();
  }
  if (lhs.track != rhs.track) {
    if (!lhs.track.has_value())
      return true;
    if (!rhs.track.has_value())
      return false;
    return lhs.track->value().bytes() < rhs.track->value().bytes();
  }
  return lhs.text < rhs.text;
}

bool diagnostic_matches_invalidation(
    const Diagnostic& diagnostic, const DiagnosticEntity& invalidated_entity) {
  if (diagnostic.entity == invalidated_entity)
    return true;
  if (const auto* node_id = std::get_if<NodeId>(&invalidated_entity))
    return diagnostic.node == *node_id;
  if (const auto* track_id = std::get_if<TrackId>(&invalidated_entity))
    return diagnostic.track == *track_id;
  return false;
}

ValidationReport ValidationService::validate_complete(
    const Project& project) const {
  return Validator(project, true, {}).run();
}

ValidationReport ValidationService::validate_incremental(
    const Project&                          project,
    const std::span<const ValidationChange> changes) const {
  if (changes.empty())
    return validate_complete(project);

  std::unordered_set<NodeId>    selected_nodes;
  std::vector<DiagnosticEntity> invalidated_entities;
  const NodeId                  nil_node;
  for (const ValidationChange& change : changes) {
    if (std::holds_alternative<UnknownValidationChange>(change) ||
        std::holds_alternative<ProjectId>(change) ||
        std::holds_alternative<TrackId>(change) ||
        std::holds_alternative<StaveId>(change) ||
        std::holds_alternative<EventId>(change)) {
      return validate_complete(project);
    }
    if (const auto* node_id = std::get_if<NodeId>(&change)) {
      if (project.find_node(*node_id) == nullptr)
        return validate_complete(project);
      selected_nodes.insert(*node_id);
      invalidated_entities.emplace_back(*node_id);
      continue;
    }
    if (std::holds_alternative<ConnectorId>(change)) {
      return validate_complete(project);
    }
    if (std::holds_alternative<NotationEntityId>(change)) {
      return validate_complete(project);
    }
    if (const auto* connector =
            std::get_if<ConnectorOccurrenceChange>(&change)) {
      if (connector->prior_node() == nil_node ||
          project.find_node(connector->prior_node()) == nullptr)
        return validate_complete(project);
      selected_nodes.insert(connector->prior_node());
      select_connector_owners(project, connector->id(), selected_nodes);
      select_connector_dependents(project, connector->id(), selected_nodes);
      invalidated_entities.emplace_back(connector->id());
      continue;
    }
    if (const auto* notation = std::get_if<NotationOccurrenceChange>(&change)) {
      if (notation->prior_node() == nil_node ||
          project.find_node(notation->prior_node()) == nullptr)
        return validate_complete(project);
      selected_nodes.insert(notation->prior_node());
      select_notation_owners(project, notation->id(), selected_nodes);
      select_notation_dependents(project, notation->id(), selected_nodes);
      invalidated_entities.emplace_back(notation->id());
    }
  }
  return Validator(project, false, std::move(selected_nodes),
                   std::move(invalidated_entities))
      .run();
}

}  // namespace graphscore
