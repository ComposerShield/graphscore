// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

namespace graphscore {
namespace {

constexpr std::size_t kVoiceCount = Voice::kMax;

static_assert(!std::is_aggregate_v<ConnectorOccurrenceChange>);
static_assert(!std::is_default_constructible_v<ConnectorOccurrenceChange>);
static_assert(!std::is_constructible_v<ConnectorOccurrenceChange, ConnectorId>);
static_assert(
    std::is_constructible_v<ConnectorOccurrenceChange, ConnectorId, NodeId>);
static_assert(!std::is_aggregate_v<NotationOccurrenceChange>);
static_assert(!std::is_default_constructible_v<NotationOccurrenceChange>);
static_assert(
    !std::is_constructible_v<NotationOccurrenceChange, NotationEntityId>);
static_assert(std::is_constructible_v<NotationOccurrenceChange,
                                      NotationEntityId, NodeId>);

template <typename Enum>
  requires std::is_enum_v<Enum>
constexpr Enum malformed_enum_from_serialized_bits(const std::uint8_t bits) {
  static_assert(std::is_same_v<std::underlying_type_t<Enum>, std::uint8_t>);
  static_assert(sizeof(Enum) == sizeof(std::uint8_t));
  return std::bit_cast<Enum>(bits);
}

Measure common_measure() {
  return Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)};
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

SpelledPitch pitch(const Letter letter = Letter::kC) {
  return *SpelledPitch::create(letter, 4);
}

void complete_stave(TrackLane& lane, const StaveId stave_id,
                    const Rational length) {
  lane.ensure_stave(stave_id);
  StaveVoices* stave = lane.stave(stave_id);
  ASSERT_NE(stave, nullptr);
  for (std::uint8_t number = Voice::kMin; number <= Voice::kMax; ++number) {
    const std::optional<Voice> voice = Voice::create(number);
    ASSERT_TRUE(voice.has_value());
    ASSERT_TRUE(stave->voice(*voice).normalize(length).ok());
  }
}

struct ProjectFixture {
  Project  project{ProjectId::generate(), "Validation"};
  TrackId  track;
  StaveId  stave;
  NodeId   node;
  Rational node_end{1};
};

ProjectFixture make_clean_project(const std::size_t measure_count = 1) {
  ProjectFixture    fixture;
  const StaffLayout layout = StaffLayout::single_staff();
  fixture.stave            = layout.staves().front().id;
  const auto track =
      fixture.project.add_track("Track", layout, *MidiChannel::create(0));
  EXPECT_TRUE(track.has_value());
  fixture.track = *track;
  fixture.node  = fixture.project.add_node("Node");

  std::vector<Measure>        measures(measure_count, common_measure());
  std::optional<NodeTimeline> timeline =
      NodeTimeline::create(std::move(measures), layout.staves());
  EXPECT_TRUE(timeline.has_value());
  fixture.node_end = timeline->node_end();
  fixture.project.find_node(fixture.node)->set_timeline(std::move(*timeline));
  TrackLane* lane =
      fixture.project.find_node(fixture.node)->lane(fixture.track);
  EXPECT_NE(lane, nullptr);
  complete_stave(*lane, fixture.stave, fixture.node_end);
  return fixture;
}

bool has_code(const ValidationReport& report, const DiagnosticCode code) {
  return std::ranges::any_of(
      report.diagnostics,
      [code](const Diagnostic& diagnostic) { return diagnostic.code == code; });
}

bool scope_in(const std::vector<DiagnosticScope>& scopes,
              const DiagnosticScope&              scope) {
  return std::ranges::find(scopes, scope) != scopes.end();
}

bool entity_in(const std::vector<DiagnosticEntity>& entities,
               const DiagnosticEntity&              entity) {
  return std::ranges::find(entities, entity) != entities.end();
}

std::vector<Diagnostic> complete_findings_for_incremental_scope(
    const ValidationReport& complete, const ValidationReport& incremental) {
  std::vector<Diagnostic> expected;
  for (const Diagnostic& diagnostic : complete.diagnostics) {
    if (scope_in(incremental.affected_scopes,
                 {diagnostic.entity, diagnostic.node, diagnostic.track})) {
      expected.push_back(diagnostic);
    }
  }
  return expected;
}

void apply_replacement(std::vector<Diagnostic>& cache,
                       const ValidationReport&  replacement) {
  if (replacement.complete) {
    cache = replacement.diagnostics;
    return;
  }
  std::erase_if(cache, [&](const Diagnostic& diagnostic) {
    return scope_in(replacement.affected_scopes,
                    {diagnostic.entity, diagnostic.node, diagnostic.track}) ||
           std::ranges::any_of(replacement.invalidated_entities,
                               [&](const DiagnosticEntity& invalidated_entity) {
                                 return diagnostic_matches_invalidation(
                                     diagnostic, invalidated_entity);
                               });
  });
  cache.insert(cache.end(), replacement.diagnostics.begin(),
               replacement.diagnostics.end());
  std::ranges::sort(cache, DiagnosticLess{});
  cache.erase(std::unique(cache.begin(), cache.end()), cache.end());
}

Uuid fixed_uuid(const std::uint8_t final_byte) {
  std::array<std::uint8_t, Uuid::kSize> bytes{};
  bytes.back() = final_byte;
  return Uuid(bytes);
}

TEST(ValidationServiceTest, CleanProjectAndLegalEditorStatesHaveNoDiagnostics) {
  ValidationService service;
  ProjectFixture    fixture = make_clean_project();
  EXPECT_TRUE(service.validate_complete(fixture.project).diagnostics.empty());

  Project fresh(ProjectId::generate(), "Fresh");
  fresh.add_node("No timeline yet");
  EXPECT_TRUE(service.validate_complete(fresh).diagnostics.empty());

  Node*             node   = fixture.project.find_node(fixture.node);
  const ConnectorId output = node->add_output("Draft");
  node->find_output(output)->set_export_enabled(false);
  EXPECT_TRUE(service.validate_complete(fixture.project).diagnostics.empty());
}

TEST(ValidationServiceTest,
     DiagnosticsHaveStrongEntitiesErrorsCodesTextAndDeterministicOrder) {
  ProjectFixture fixture = make_clean_project();
  Node*          node    = fixture.project.find_node(fixture.node);
  [[maybe_unused]] const ConnectorId disconnected =
      node->add_output("Disconnected");
  [[maybe_unused]] const ConnectorId second_disconnected =
      node->add_output("Also disconnected");
  TrackLane* lane = node->lane(fixture.track);
  lane->stave(fixture.stave)->voice(*Voice::create(1)).clear();
  const NodeId other      = fixture.project.add_node("Same stave context");
  Node*        other_node = fixture.project.find_node(other);
  std::optional<NodeTimeline> timeline = NodeTimeline::create(
      {common_measure()},
      fixture.project.find_active_track(fixture.track)->layout().staves());
  ASSERT_TRUE(timeline.has_value());
  other_node->set_timeline(std::move(*timeline));
  other_node->lane(fixture.track)->ensure_stave(fixture.stave);

  const ValidationService service;
  const ValidationReport  first  = service.validate_complete(fixture.project);
  const ValidationReport  second = service.validate_complete(fixture.project);
  EXPECT_EQ(first, second);
  ASSERT_FALSE(first.diagnostics.empty());
  for (const Diagnostic& diagnostic : first.diagnostics) {
    EXPECT_EQ(diagnostic.severity, DiagnosticSeverity::kError);
    EXPECT_FALSE(diagnostic.text.empty());
    EXPECT_GE(diagnostic.entity.index(), 0u);
  }
  std::vector<Diagnostic> sorted = first.diagnostics;
  apply_replacement(sorted, ValidationReport{});
  EXPECT_EQ(first.diagnostics, sorted);
}

TEST(ValidationServiceTest, DiagnosticLessExercisesEveryCanonicalTieBreak) {
  const ProjectId project_low(fixed_uuid(1));
  const ProjectId project_high(fixed_uuid(2));
  const TrackId   track_entity(fixed_uuid(1));
  const NodeId    node_low(fixed_uuid(1));
  const NodeId    node_high(fixed_uuid(2));
  const TrackId   track_low(fixed_uuid(3));
  const TrackId   track_high(fixed_uuid(4));
  const auto      diagnostic =
      [](const DiagnosticEntity& entity, const DiagnosticSeverity severity,
         const DiagnosticCode code, const std::optional<NodeId> node,
         const std::optional<TrackId> track, std::string text) {
        return Diagnostic{entity, node, track, severity, code, std::move(text)};
      };
  const Diagnostic base =
      diagnostic(project_low, DiagnosticSeverity::kError,
                 DiagnosticCode::kNilUuid, std::nullopt, std::nullopt, "base");
  const auto expect_before = [](const Diagnostic& low, const Diagnostic& high) {
    const DiagnosticLess less;
    EXPECT_TRUE(less(low, high));
    EXPECT_FALSE(less(high, low));
  };
  expect_before(base, diagnostic(project_low, DiagnosticSeverity::kWarning,
                                 DiagnosticCode::kNilUuid, std::nullopt,
                                 std::nullopt, "base"));
  expect_before(base, diagnostic(project_low, DiagnosticSeverity::kError,
                                 DiagnosticCode::kDuplicateUuid, std::nullopt,
                                 std::nullopt, "base"));
  expect_before(base, diagnostic(track_entity, DiagnosticSeverity::kError,
                                 DiagnosticCode::kNilUuid, std::nullopt,
                                 std::nullopt, "base"));
  expect_before(base, diagnostic(project_high, DiagnosticSeverity::kError,
                                 DiagnosticCode::kNilUuid, std::nullopt,
                                 std::nullopt, "base"));
  const Diagnostic node_absent =
      diagnostic(project_low, DiagnosticSeverity::kError,
                 DiagnosticCode::kNilUuid, std::nullopt, std::nullopt, "base");
  const Diagnostic node_low_diagnostic =
      diagnostic(project_low, DiagnosticSeverity::kError,
                 DiagnosticCode::kNilUuid, node_low, std::nullopt, "base");
  expect_before(node_absent, node_low_diagnostic);
  expect_before(
      node_low_diagnostic,
      diagnostic(project_low, DiagnosticSeverity::kError,
                 DiagnosticCode::kNilUuid, node_high, std::nullopt, "base"));
  const Diagnostic track_absent = node_low_diagnostic;
  const Diagnostic track_low_diagnostic =
      diagnostic(project_low, DiagnosticSeverity::kError,
                 DiagnosticCode::kNilUuid, node_low, track_low, "base");
  expect_before(track_absent, track_low_diagnostic);
  expect_before(
      track_low_diagnostic,
      diagnostic(project_low, DiagnosticSeverity::kError,
                 DiagnosticCode::kNilUuid, node_low, track_high, "base"));
  expect_before(
      diagnostic(project_low, DiagnosticSeverity::kError,
                 DiagnosticCode::kNilUuid, node_low, track_low, "alpha"),
      diagnostic(project_low, DiagnosticSeverity::kError,
                 DiagnosticCode::kNilUuid, node_low, track_low, "omega"));
}

TEST(ValidationServiceTest, RhythmicCompletenessUsesTheNodeTimeline) {
  ProjectFixture fixture = make_clean_project();
  VoiceContent&  voice   = fixture.project.find_node(fixture.node)
                            ->lane(fixture.track)
                            ->stave(fixture.stave)
                            ->voice(*Voice::create(2));
  voice.clear();
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());

  const ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kRhythmIncomplete));
}

TEST(ValidationServiceTest, DuplicateArticulationSurfacesAsItsOwnCode) {
  ProjectFixture fixture = make_clean_project();
  VoiceContent&  voice   = fixture.project.find_node(fixture.node)
                            ->lane(fixture.track)
                            ->stave(fixture.stave)
                            ->voice(*Voice::create(1));
  voice.clear();
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      voice
          .append(make_note(pitch(), whole, false,
                            {Articulation::kAccent, Articulation::kAccent}))
          .ok());

  const ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kDuplicateArticulation));
}

TEST(ValidationServiceTest,
     UuidUniquenessIncludesArchivedEmbeddedChordGraceAndMarkingIds) {
  ProjectFixture    fixture       = make_clean_project();
  const StaffLayout second_layout = StaffLayout::single_staff(Clef::kBass);
  const StaveId     second_stave  = second_layout.staves().front().id;
  const auto second_track = fixture.project.add_track("Archived", second_layout,
                                                      *MidiChannel::create(1));
  ASSERT_TRUE(second_track.has_value());
  Node* node = fixture.project.find_node(fixture.node);
  ASSERT_TRUE(node->timeline()
                  ->create_clef_lane(second_stave, ClefLane(Clef::kBass))
                  .ok());
  TrackLane* second_lane = node->lane(*second_track);
  complete_stave(*second_lane, second_stave, fixture.node_end);

  const NotationEntityId duplicate_chord_note = NotationEntityId::generate();
  const NotationEntityId duplicate_grace_note = NotationEntityId::generate();
  const NotationEntityId duplicate_marking    = NotationEntityId::generate();
  auto add_chord_and_grace = [&](TrackLane& lane, const StaveId stave_id) {
    VoiceContent& voice = lane.stave(stave_id)->voice(*Voice::create(1));
    voice.clear();
    Chord chord = make_chord(
        quarter(), {ChordNote{duplicate_chord_note, pitch(Letter::kC), false},
                    ChordNote{.pitch = pitch(Letter::kE)}});
    ASSERT_TRUE(voice.append(chord).ok());
    GraceGroup group =
        make_grace_group(chord.id, {GraceNote{.id       = duplicate_grace_note,
                                              .pitch    = pitch(Letter::kD),
                                              .duration = quarter()}});
    ASSERT_TRUE(voice.add_grace_group(group).ok());
    ASSERT_TRUE(voice
                    .add_dynamic(DynamicMarking{duplicate_marking, chord.id,
                                                Dynamic::kMf})
                    .ok());
    ASSERT_TRUE(voice.normalize(fixture.node_end).ok());
  };
  add_chord_and_grace(*node->lane(fixture.track), fixture.stave);
  add_chord_and_grace(*second_lane, second_stave);
  ASSERT_TRUE(fixture.project.archive_track(*second_track).ok());

  const ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kDuplicateUuid));
  for (const NotationEntityId duplicate :
       {duplicate_chord_note, duplicate_grace_note, duplicate_marking}) {
    EXPECT_TRUE(
        std::ranges::any_of(report.diagnostics, [&](const Diagnostic& d) {
          return d.code == DiagnosticCode::kDuplicateUuid &&
                 d.entity == DiagnosticEntity(duplicate);
        }));
  }
}

TEST(ValidationServiceTest, UuidNamespaceIsHeterogeneousButComparedGlobally) {
  const Uuid shared = Uuid::generate();
  Project    project(ProjectId(shared), "Shared raw UUID");
  ASSERT_TRUE(project
                  .add_track_with_id(TrackId(shared), "Track",
                                     StaffLayout::single_staff(),
                                     *MidiChannel::create(0))
                  .ok());

  const ValidationReport report =
      ValidationService().validate_complete(project);
  EXPECT_TRUE(std::ranges::any_of(report.diagnostics, [&](const Diagnostic& d) {
    return d.code == DiagnosticCode::kDuplicateUuid &&
           std::holds_alternative<ProjectId>(d.entity);
  }));
  EXPECT_TRUE(std::ranges::any_of(report.diagnostics, [&](const Diagnostic& d) {
    return d.code == DiagnosticCode::kDuplicateUuid &&
           std::holds_alternative<TrackId>(d.entity);
  }));
}

TEST(ValidationServiceTest,
     OrphanLanesAreTraversedForOwnershipAndDuplicateNotationIds) {
  ProjectFixture   fixture   = make_clean_project();
  Node*            node      = fixture.project.find_node(fixture.node);
  const VoiceEvent duplicate = node->lane(fixture.track)
                                   ->stave(fixture.stave)
                                   ->voice(*Voice::create(1))
                                   .events()
                                   .front();
  const TrackId orphan_track = TrackId::generate();
  const StaveId orphan_stave = StaveId::generate();
  node->ensure_lane(orphan_track);
  node->lane(orphan_track)->ensure_stave(orphan_stave);
  ASSERT_TRUE(node->lane(orphan_track)
                  ->stave(orphan_stave)
                  ->voice(*Voice::create(1))
                  .append(duplicate)
                  .ok());

  const ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(std::ranges::any_of(report.diagnostics, [&](const Diagnostic& d) {
    return d.code == DiagnosticCode::kUnexpectedTrackLane &&
           d.entity == DiagnosticEntity(orphan_track) && d.node == fixture.node;
  }));
  EXPECT_EQ(std::ranges::count_if(
                report.diagnostics,
                [&](const Diagnostic& d) {
                  return d.code == DiagnosticCode::kDuplicateUuid &&
                         d.entity == DiagnosticEntity(event_id(duplicate));
                }),
            2);
  EXPECT_TRUE(std::ranges::all_of(
      report.diagnostics, [&](const Diagnostic& diagnostic) {
        return diagnostic.code != DiagnosticCode::kDuplicateUuid ||
               diagnostic.entity != DiagnosticEntity(event_id(duplicate)) ||
               diagnostic.track.has_value();
      }));
}

TEST(ValidationServiceTest,
     SameNodeLaneDiagnosticsRetainDistinctTrackOccurrences) {
  ProjectFixture    fixture = make_clean_project();
  const StaffLayout shared_layout =
      *StaffLayout::create({{fixture.stave, Clef::kTreble}});
  const auto second_track = fixture.project.add_track(
      "Shared stave", shared_layout, *MidiChannel::create(1));
  ASSERT_TRUE(second_track.has_value());
  Node* node = fixture.project.find_node(fixture.node);
  node->lane(*second_track)->ensure_stave(fixture.stave);

  const NotationEntityId missing_event = NotationEntityId::generate();
  const DynamicMarking   malformed =
      make_dynamic_marking(missing_event, Dynamic::kF);
  ASSERT_TRUE(node->lane(fixture.track)
                  ->stave(fixture.stave)
                  ->voice(*Voice::create(1))
                  .add_dynamic(malformed)
                  .ok());
  ASSERT_TRUE(node->lane(*second_track)
                  ->stave(fixture.stave)
                  ->voice(*Voice::create(1))
                  .add_dynamic(malformed)
                  .ok());

  const ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  const auto matching_tracks = [&](const DiagnosticCode    code,
                                   const DiagnosticEntity& entity) {
    std::vector<TrackId> tracks;
    for (const Diagnostic& diagnostic : report.diagnostics) {
      if (diagnostic.code == code && diagnostic.entity == entity &&
          diagnostic.node == fixture.node && diagnostic.track.has_value()) {
        tracks.push_back(*diagnostic.track);
      }
    }
    return tracks;
  };
  std::vector<TrackId> expected_tracks{fixture.track, *second_track};
  std::ranges::sort(expected_tracks, {},
                    [](const TrackId id) { return id.value().bytes(); });
  EXPECT_EQ(matching_tracks(DiagnosticCode::kDynamicDanglingReference,
                            DiagnosticEntity(malformed.id)),
            expected_tracks);
  EXPECT_EQ(matching_tracks(DiagnosticCode::kDuplicateUuid,
                            DiagnosticEntity(malformed.id)),
            expected_tracks);
}

TEST(ValidationServiceTest, FocusedNotationDiagnosticsAreReusedAndMapped) {
  ProjectFixture fixture = make_clean_project();
  VoiceContent&  voice   = fixture.project.find_node(fixture.node)
                            ->lane(fixture.track)
                            ->stave(fixture.stave)
                            ->voice(*Voice::create(1));
  const DynamicMarking dangling =
      make_dynamic_marking(NotationEntityId::generate(), Dynamic::kF);
  ASSERT_TRUE(voice.add_dynamic(dangling).ok());

  const ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(std::ranges::any_of(report.diagnostics, [&](const Diagnostic& d) {
    return d.entity == DiagnosticEntity(dangling.id) &&
           d.code == DiagnosticCode::kDynamicDanglingReference;
  }));
}

TEST(ValidationServiceTest, ActiveLaneAndStaveAlignmentAreValidated) {
  ProjectFixture fixture = make_clean_project();
  Node*          node    = fixture.project.find_node(fixture.node);
  node->remove_lane(fixture.track);
  ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kMissingTrackLane));

  node->ensure_lane(fixture.track);
  node->lane(fixture.track)->ensure_stave(StaveId::generate());
  report = ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kUnexpectedStave));
}

TEST(ValidationServiceTest,
     FixedLayoutsDiagnoseSingleAndGrandStaffOmissionsActiveAndArchived) {
  ProjectFixture fixture = make_clean_project();
  Node*          node    = fixture.project.find_node(fixture.node);
  node->remove_lane(fixture.track);
  node->ensure_lane(fixture.track);
  EXPECT_TRUE(has_code(ValidationService().validate_complete(fixture.project),
                       DiagnosticCode::kMissingStave));
  ASSERT_TRUE(fixture.project.archive_track(fixture.track).ok());
  EXPECT_TRUE(has_code(ValidationService().validate_complete(fixture.project),
                       DiagnosticCode::kMissingStave));

  const StaffLayout grand = StaffLayout::grand_staff();
  const auto        grand_track =
      fixture.project.add_track("Piano", grand, *MidiChannel::create(1));
  ASSERT_TRUE(grand_track.has_value());
  TrackLane* grand_lane = node->lane(*grand_track);
  ASSERT_NE(grand_lane, nullptr);
  grand_lane->ensure_stave(grand.staves().front().id);
  ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(std::ranges::any_of(report.diagnostics, [&](const Diagnostic& d) {
    return d.code == DiagnosticCode::kMissingStave &&
           d.entity == DiagnosticEntity(grand.staves().back().id) &&
           d.node == fixture.node;
  }));

  ASSERT_TRUE(fixture.project.archive_track(*grand_track).ok());
  report = ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(std::ranges::any_of(report.diagnostics, [&](const Diagnostic& d) {
    return d.code == DiagnosticCode::kMissingStave &&
           d.entity == DiagnosticEntity(grand.staves().back().id) &&
           d.node == fixture.node;
  }));
}

TEST(ValidationServiceTest, TrackAddedAfterTimelineRequiresItsFixedStaves) {
  ProjectFixture fixture = make_clean_project();
  const auto     track   = fixture.project.add_track(
      "Later", StaffLayout::single_staff(), *MidiChannel::create(1));
  ASSERT_TRUE(track.has_value());
  EXPECT_TRUE(has_code(ValidationService().validate_complete(fixture.project),
                       DiagnosticCode::kMissingStave));
}

TEST(ValidationServiceTest, FreshNoTimelineNodeWithEmptyAlignedLaneIsLegal) {
  Project project(ProjectId::generate(), "Fresh");
  ASSERT_TRUE(project
                  .add_track("Track", StaffLayout::single_staff(),
                             *MidiChannel::create(0))
                  .has_value());
  [[maybe_unused]] const NodeId node = project.add_node("No timeline");
  EXPECT_TRUE(
      ValidationService().validate_complete(project).diagnostics.empty());
}

TEST(ValidationServiceTest,
     NoTimelineStillValidatesAllVoiceReferencesAndPedalOrdering) {
  ProjectFixture fixture = make_clean_project();
  Node*          node    = fixture.project.find_node(fixture.node);
  node->clear_timeline();
  VoiceContent& voice =
      node->lane(fixture.track)->stave(fixture.stave)->voice(*Voice::create(1));
  voice.clear();
  ASSERT_TRUE(voice.append(make_note(pitch(), quarter(), true)).ok());
  const NotationEntityId missing = NotationEntityId::generate();
  ASSERT_TRUE(
      voice.add_dynamic(make_dynamic_marking(missing, Dynamic::kF)).ok());
  ASSERT_TRUE(voice
                  .add_hairpin(make_hairpin(missing, missing,
                                            HairpinDirection::kCrescendo))
                  .ok());
  ASSERT_TRUE(voice.add_slur(make_slur(missing, missing)).ok());
  ASSERT_TRUE(voice
                  .add_beam_override(
                      make_beam_override(BeamOverride::Kind::kBreak, {missing}))
                  .ok());
  const GraceNote grace{.pitch = pitch(), .duration = quarter()};
  ASSERT_TRUE(voice.add_grace_group(make_grace_group(missing, {grace})).ok());
  ASSERT_TRUE(node->lane(fixture.track)
                  ->add_pedal_span(fixture.stave,
                                   PedalSpan{NotationEntityId::generate(),
                                             Rational(1), Rational(0)})
                  .ok());

  const ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  for (const DiagnosticCode code :
       {DiagnosticCode::kDanglingTie, DiagnosticCode::kHairpinDanglingEndpoint,
        DiagnosticCode::kSlurDanglingEndpoint,
        DiagnosticCode::kInvalidBeamOverride,
        DiagnosticCode::kDynamicDanglingReference,
        DiagnosticCode::kGraceGroupPrincipalNotSounding,
        DiagnosticCode::kPedalSpanNotOrdered}) {
    EXPECT_TRUE(has_code(report, code));
  }
  EXPECT_FALSE(has_code(report, DiagnosticCode::kPedalSpanOutOfRange));
  EXPECT_FALSE(has_code(report, DiagnosticCode::kRhythmIncomplete));
}

TEST(ValidationServiceTest,
     SignatureTimelineLegalityAndApiRejectionsAreCovered) {
  ProjectFixture fixture  = make_clean_project();
  NodeTimeline*  timeline = fixture.project.find_node(fixture.node)->timeline();
  ASSERT_TRUE(
      timeline->add_clef_change(fixture.stave, fixture.node_end, Clef::kAlto)
          .ok());
  const Mode malformed_mode =
      malformed_enum_from_serialized_bits<Mode>(std::uint8_t{99});
  const KeySignature invalid_key = *KeySignature::create(0, malformed_mode);
  ASSERT_TRUE(timeline->set_measure_key_signature(0, invalid_key).ok());
  const Tempo tempo = *Tempo::create(Rational(120), NoteValue::kQuarter);
  const TempoSegmentKind malformed_segment_kind =
      malformed_enum_from_serialized_bits<TempoSegmentKind>(std::uint8_t{99});
  ASSERT_TRUE(
      timeline
          ->set_tempo({TempoPoint{Rational(0), tempo, malformed_segment_kind}})
          .ok());

  const ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kIllegalClefChange));
  EXPECT_TRUE(has_code(report, DiagnosticCode::kIllegalKeySignature));
  EXPECT_TRUE(has_code(report, DiagnosticCode::kIllegalTempoLane));

  EXPECT_FALSE(timeline->set_pickdown(Rational(1)).ok());
  EXPECT_FALSE(
      timeline->set_measure_time_signature(8, *TimeSignature::create(3, 4))
          .ok());
  EXPECT_FALSE(timeline->set_tempo({}).ok());
}

TEST(ValidationServiceTest,
     GraphDestinationsAndCrossEntityMutationApisRejectOrDiagnoseInvalidRefs) {
  ProjectFixture    fixture   = make_clean_project();
  Node*             node      = fixture.project.find_node(fixture.node);
  const ConnectorId output    = node->add_output("Output");
  OutputConnector*  connector = node->find_output(output);
  connector->set_destination(
      ConnectorDestination{NodeId::generate(), ConnectorId::generate()});

  ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kDanglingGraphDestination));

  connector->set_destination(std::nullopt);
  Graph graph(fixture.project);
  EXPECT_FALSE(graph
                   .connect(fixture.node, output, NodeId::generate(),
                            ConnectorId::generate())
                   .ok());
  EXPECT_FALSE(
      graph.bind_output_event(fixture.node, output, EventId::generate()).ok());
  EXPECT_FALSE(fixture.project.set_start_node(NodeId::generate()).ok());

  const EventId event = *fixture.project.events().add_event("Temporary");
  ASSERT_TRUE(graph.bind_output_event(fixture.node, output, event).ok());
  ASSERT_TRUE(fixture.project.events().remove_event(event).ok());
  report = ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kDanglingEventBinding));
}

TEST(ValidationServiceTest,
     EventNamesAreCaseSensitiveAndNormalApisRejectExactDuplicates) {
  Project project(ProjectId::generate(), "Events");
  EXPECT_TRUE(project.events().add_event("Attack").has_value());
  EXPECT_TRUE(project.events().add_event("attack").has_value());
  EXPECT_FALSE(project.events().add_event("Attack").has_value());
  EXPECT_FALSE(has_code(ValidationService().validate_complete(project),
                        DiagnosticCode::kDuplicateEventName));
}

TEST(ValidationServiceTest,
     ConnectorCardinalityBindingListenerAndRandomWeightRulesAreValidated) {
  ProjectFixture    fixture        = make_clean_project();
  const NodeId      destination_id = fixture.project.add_node("Destination");
  Node*             source         = fixture.project.find_node(fixture.node);
  Node*             destination    = fixture.project.find_node(destination_id);
  const ConnectorId input          = destination->add_input("Input");
  const ConnectorId first          = source->add_output("First");
  const ConnectorId second         = source->add_output("Second");
  Graph             graph(fixture.project);
  ASSERT_TRUE(graph.connect(fixture.node, first, destination_id, input).ok());
  EXPECT_FALSE(graph.connect(fixture.node, first, destination_id, input).ok());
  ASSERT_TRUE(graph.connect(fixture.node, second, destination_id, input).ok());

  ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kInvalidRandomWeightTotal));
  const Rational half = *Rational::create(1, 2);
  ASSERT_TRUE(source->find_output(first)->set_weight(half).ok());
  ASSERT_TRUE(source->find_output(second)->set_weight(half).ok());
  EXPECT_FALSE(has_code(ValidationService().validate_complete(fixture.project),
                        DiagnosticCode::kInvalidRandomWeightTotal));

  const ConnectorId vertical =
      source->add_output("Vertical", ConnectorType::kVertical);
  report = ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kExportDestinationRequired));
  EXPECT_TRUE(has_code(report, DiagnosticCode::kVerticalEventRequired));

  source->find_output(vertical)->set_export_enabled(false);
  const EventId event = *fixture.project.events().add_event("Go");
  ASSERT_TRUE(graph.bind_output_event(fixture.node, first, event).ok());
  const OutputConnector snapshot = *source->find_output(first);
  ASSERT_TRUE(source->remove_output(first).ok());
  ASSERT_TRUE(destination->restore_output(snapshot, std::nullopt).ok());
  report = ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kMissingEventListener));
}

TEST(ValidationServiceTest, CyclesAndSelfLoopsAreValid) {
  ProjectFixture    fixture = make_clean_project();
  Node*             node    = fixture.project.find_node(fixture.node);
  const ConnectorId input   = node->add_input("In");
  const ConnectorId output  = node->add_output("Out");
  Graph             graph(fixture.project);
  ASSERT_TRUE(graph.connect(fixture.node, output, fixture.node, input).ok());
  EXPECT_TRUE(ValidationService()
                  .validate_complete(fixture.project)
                  .diagnostics.empty());
}

TEST(ValidationServiceTest,
     ArchivedLanesRemainExpectedAndTheirRecoverableNotationIsValidated) {
  ProjectFixture fixture = make_clean_project();
  VoiceContent&  voice   = fixture.project.find_node(fixture.node)
                            ->lane(fixture.track)
                            ->stave(fixture.stave)
                            ->voice(*Voice::create(1));
  const DynamicMarking dangling =
      make_dynamic_marking(NotationEntityId::generate(), Dynamic::kP);
  ASSERT_TRUE(voice.add_dynamic(dangling).ok());
  ASSERT_TRUE(fixture.project.archive_track(fixture.track).ok());

  const ValidationReport report =
      ValidationService().validate_complete(fixture.project);
  EXPECT_TRUE(has_code(report, DiagnosticCode::kDynamicDanglingReference));
  EXPECT_FALSE(has_code(report, DiagnosticCode::kUnexpectedTrackLane));
  EXPECT_FALSE(has_code(report, DiagnosticCode::kRhythmIncomplete));
}

TEST(ValidationServiceTest,
     IncrementalNodeConnectorAndNotationScopesEqualCompleteAffectedResults) {
  ProjectFixture    fixture = make_clean_project();
  const NodeId      other   = fixture.project.add_node("Unrelated fresh node");
  Node*             node    = fixture.project.find_node(fixture.node);
  const ConnectorId connector = node->add_output("Disconnected");
  VoiceContent&     voice =
      node->lane(fixture.track)->stave(fixture.stave)->voice(*Voice::create(1));
  const NotationEntityId  notation_id = event_id(voice.events().front());
  const ValidationService service;
  const ValidationReport  complete = service.validate_complete(fixture.project);

  for (const ValidationChange change : {
           ValidationChange(fixture.node),
           ValidationChange(ConnectorOccurrenceChange{connector, fixture.node}),
           ValidationChange(
               NotationOccurrenceChange{notation_id, fixture.node}),
       }) {
    const std::array       changes{change};
    const ValidationReport incremental =
        service.validate_incremental(fixture.project, changes);
    EXPECT_FALSE(incremental.complete);
    EXPECT_EQ(incremental.diagnostics,
              complete_findings_for_incremental_scope(complete, incremental));
    EXPECT_FALSE(scope_in(incremental.affected_scopes,
                          {DiagnosticEntity(other), other, std::nullopt}));
  }
}

TEST(ValidationServiceTest,
     BareConnectorAndNotationIdsSafelyFallBackToCompleteValidation) {
  ProjectFixture          fixture   = make_clean_project();
  Node*                   node      = fixture.project.find_node(fixture.node);
  const ConnectorId       connector = node->add_output("Local output");
  const NotationEntityId  notation  = event_id(node->lane(fixture.track)
                                                   ->stave(fixture.stave)
                                                   ->voice(*Voice::create(1))
                                                   .events()
                                                   .front());
  const ValidationService service;
  const std::array        connector_change{ValidationChange(connector)};
  const std::array        notation_change{ValidationChange(notation)};
  const std::array        stale_context_change{ValidationChange(
      ConnectorOccurrenceChange{connector, NodeId::generate()})};

  EXPECT_TRUE(
      service.validate_incremental(fixture.project, connector_change).complete);
  EXPECT_TRUE(
      service.validate_incremental(fixture.project, notation_change).complete);
  EXPECT_TRUE(
      service.validate_incremental(fixture.project, stale_context_change)
          .complete);
}

TEST(ValidationServiceTest,
     NilPriorOccurrenceContextFallsBackEvenWhenNilNodeExists) {
  ProjectFixture fixture = make_clean_project();
  ASSERT_TRUE(
      fixture.project.add_node_with_id(NodeId{}, "Malformed nil node").ok());
  ASSERT_NE(fixture.project.find_node(NodeId{}), nullptr);
  Node*                   node      = fixture.project.find_node(fixture.node);
  const ConnectorId       connector = node->add_output("Changed connector");
  const NotationEntityId  notation  = event_id(node->lane(fixture.track)
                                                   ->stave(fixture.stave)
                                                   ->voice(*Voice::create(1))
                                                   .events()
                                                   .front());
  const ValidationService service;
  std::vector<Diagnostic> cache =
      service.validate_complete(fixture.project).diagnostics;

  for (const ValidationChange change : {
           ValidationChange(ConnectorOccurrenceChange(connector, NodeId{})),
           ValidationChange(NotationOccurrenceChange(notation, NodeId{})),
       }) {
    const std::array       changes{change};
    const ValidationReport replacement =
        service.validate_incremental(fixture.project, changes);
    EXPECT_TRUE(replacement.complete);
    apply_replacement(cache, replacement);
    EXPECT_EQ(cache, service.validate_complete(fixture.project).diagnostics);
  }
}

TEST(ValidationServiceTest,
     UnresolvedLocalChangesFallBackAndReplaceDeletedDiagnosticCache) {
  ProjectFixture          fixture = make_clean_project();
  const ValidationService service;
  Node*                   node   = fixture.project.find_node(fixture.node);
  const ConnectorId       output = node->add_output("Malformed");
  std::vector<Diagnostic> cache =
      service.validate_complete(fixture.project).diagnostics;
  ASSERT_TRUE(has_code(service.validate_complete(fixture.project),
                       DiagnosticCode::kExportDestinationRequired));
  ASSERT_TRUE(node->remove_output(output).ok());
  const std::array       connector_change{ValidationChange(output)};
  const ValidationReport connector_replacement =
      service.validate_incremental(fixture.project, connector_change);
  EXPECT_TRUE(connector_replacement.complete);
  apply_replacement(cache, connector_replacement);
  EXPECT_EQ(cache, service.validate_complete(fixture.project).diagnostics);

  VoiceContent& voice =
      node->lane(fixture.track)->stave(fixture.stave)->voice(*Voice::create(1));
  const DynamicMarking dangling =
      make_dynamic_marking(NotationEntityId::generate(), Dynamic::kF);
  ASSERT_TRUE(voice.add_dynamic(dangling).ok());
  cache = service.validate_complete(fixture.project).diagnostics;
  ASSERT_TRUE(voice.remove_dynamic(dangling.id).ok());
  const std::array       notation_change{ValidationChange(dangling.id)};
  const ValidationReport notation_replacement =
      service.validate_incremental(fixture.project, notation_change);
  EXPECT_TRUE(notation_replacement.complete);
  apply_replacement(cache, notation_replacement);
  EXPECT_EQ(cache, service.validate_complete(fixture.project).diagnostics);

  const NodeId      removed = fixture.project.add_node("Removed target");
  const ConnectorId input =
      fixture.project.find_node(removed)->add_input("Target");
  node                            = fixture.project.find_node(fixture.node);
  const ConnectorId source_output = node->add_output("Cascade source");
  ASSERT_TRUE(Graph(fixture.project)
                  .connect(fixture.node, source_output, removed, input)
                  .ok());
  cache = service.validate_complete(fixture.project).diagnostics;
  ASSERT_TRUE(fixture.project.remove_node(removed).ok());
  const std::array       node_change{ValidationChange(removed)};
  const ValidationReport node_replacement =
      service.validate_incremental(fixture.project, node_change);
  EXPECT_TRUE(node_replacement.complete);
  apply_replacement(cache, node_replacement);
  EXPECT_EQ(cache, service.validate_complete(fixture.project).diagnostics);
  EXPECT_TRUE(
      has_code(node_replacement, DiagnosticCode::kExportDestinationRequired));

  const std::array stale_change{ValidationChange(NotationEntityId::generate())};
  EXPECT_TRUE(
      service.validate_incremental(fixture.project, stale_change).complete);
}

TEST(ValidationServiceTest,
     TypedContextInvalidationRemovesDeletedChildWithoutCrossKindClearing) {
  ProjectFixture          fixture = make_clean_project();
  const ValidationService service;
  Node*                   node   = fixture.project.find_node(fixture.node);
  const ConnectorId       output = node->add_output("Deleted malformed child");
  std::vector<Diagnostic> cache =
      service.validate_complete(fixture.project).diagnostics;
  ASSERT_TRUE(std::ranges::any_of(cache, [&](const Diagnostic& diagnostic) {
    return diagnostic.entity == DiagnosticEntity(output) &&
           diagnostic.node == fixture.node;
  }));

  ASSERT_TRUE(node->remove_output(output).ok());
  const std::array       changes{ValidationChange(fixture.node)};
  const ValidationReport replacement =
      service.validate_incremental(fixture.project, changes);
  EXPECT_FALSE(replacement.complete);
  apply_replacement(cache, replacement);
  EXPECT_EQ(cache, service.validate_complete(fixture.project).diagnostics);

  const Uuid       shared_uuid = fixed_uuid(42);
  const NodeId     node_id(shared_uuid);
  const TrackId    track_id(shared_uuid);
  const Diagnostic node_context{ProjectId::generate(),
                                node_id,
                                std::nullopt,
                                DiagnosticSeverity::kError,
                                DiagnosticCode::kNilUuid,
                                "node context"};
  const Diagnostic track_context{ProjectId::generate(),
                                 std::nullopt,
                                 track_id,
                                 DiagnosticSeverity::kError,
                                 DiagnosticCode::kNilUuid,
                                 "track context"};
  EXPECT_TRUE(diagnostic_matches_invalidation(node_context, node_id));
  EXPECT_FALSE(diagnostic_matches_invalidation(node_context, track_id));
  EXPECT_TRUE(diagnostic_matches_invalidation(track_context, track_id));
  EXPECT_FALSE(diagnostic_matches_invalidation(track_context, node_id));
}

TEST(ValidationServiceTest,
     IncrementalReplacementRemovesStaleDuplicateNotationOccurrence) {
  ProjectFixture fixture = make_clean_project();
  Node*          node    = fixture.project.find_node(fixture.node);
  VoiceContent&  initial =
      node->lane(fixture.track)->stave(fixture.stave)->voice(*Voice::create(1));
  initial.clear();
  ASSERT_TRUE(
      initial.append(make_note(pitch(), quarter(), /*tied_to_next=*/true))
          .ok());
  ASSERT_TRUE(initial.append(make_note(pitch(), quarter())).ok());
  ASSERT_TRUE(initial.append(make_note(pitch(Letter::kD), quarter())).ok());
  ASSERT_TRUE(initial.append(make_note(pitch(Letter::kD), quarter())).ok());
  const VoiceEvent       duplicate  = initial.events()[1];
  const NotationEntityId changed_id = event_id(duplicate);
  const NodeId other    = fixture.project.add_node("Surviving occurrence");
  TrackLane* other_lane = fixture.project.find_node(other)->lane(fixture.track);
  other_lane->ensure_stave(fixture.stave);
  VoiceContent& survivor =
      other_lane->stave(fixture.stave)->voice(*Voice::create(1));
  ASSERT_TRUE(survivor.append(duplicate).ok());
  VoiceContent& source = fixture.project.find_node(fixture.node)
                             ->lane(fixture.track)
                             ->stave(fixture.stave)
                             ->voice(*Voice::create(1));
  const DynamicMarking dependent =
      make_dynamic_marking(changed_id, Dynamic::kF);
  ASSERT_TRUE(source.add_dynamic(dependent).ok());

  const ValidationService service;
  std::vector<Diagnostic> cache =
      service.validate_complete(fixture.project).diagnostics;
  ASSERT_EQ(std::ranges::count_if(
                cache,
                [&](const Diagnostic& diagnostic) {
                  return diagnostic.code == DiagnosticCode::kDuplicateUuid &&
                         diagnostic.entity == DiagnosticEntity(changed_id);
                }),
            2);
  ASSERT_TRUE(source.remove_event(quarter().resolved(), fixture.node_end).ok());
  const std::array changes{
      ValidationChange(NotationOccurrenceChange{changed_id, fixture.node})};
  const ValidationReport replacement =
      service.validate_incremental(fixture.project, changes);
  EXPECT_FALSE(replacement.complete);
  EXPECT_TRUE(entity_in(replacement.invalidated_entities, changed_id));
  apply_replacement(cache, replacement);
  EXPECT_EQ(cache, service.validate_complete(fixture.project).diagnostics);
  EXPECT_TRUE(std::ranges::any_of(cache, [&](const Diagnostic& diagnostic) {
    return diagnostic.code == DiagnosticCode::kTiePitchMismatch &&
           diagnostic.node == fixture.node;
  }));
  EXPECT_TRUE(std::ranges::any_of(cache, [&](const Diagnostic& diagnostic) {
    return diagnostic.code == DiagnosticCode::kDynamicDanglingReference &&
           diagnostic.entity == DiagnosticEntity(dependent.id) &&
           diagnostic.node == fixture.node;
  }));
}

TEST(ValidationServiceTest,
     IncrementalReplacementRevalidatesFormerDuplicateConnectorAggregate) {
  ProjectFixture    fixture     = make_clean_project();
  Node*             source      = fixture.project.find_node(fixture.node);
  const ConnectorId destination = source->add_input("Destination");
  const ConnectorId changed_id  = source->add_output("Duplicate output");
  const ConnectorId sibling     = source->add_output("Remaining half");
  source->find_output(changed_id)
      ->set_destination(ConnectorDestination{fixture.node, destination});
  source->find_output(sibling)->set_destination(
      ConnectorDestination{fixture.node, destination});
  const Rational half = *Rational::create(1, 2);
  ASSERT_TRUE(source->find_output(changed_id)->set_weight(half).ok());
  ASSERT_TRUE(source->find_output(sibling)->set_weight(half).ok());
  const OutputConnector duplicate = *source->find_output(changed_id);
  const NodeId other = fixture.project.add_node("Surviving occurrence");
  ASSERT_TRUE(fixture.project.find_node(other)
                  ->restore_output(duplicate, std::nullopt)
                  .ok());

  const ValidationService service;
  std::vector<Diagnostic> cache =
      service.validate_complete(fixture.project).diagnostics;
  ASSERT_EQ(std::ranges::count_if(
                cache,
                [&](const Diagnostic& diagnostic) {
                  return diagnostic.code == DiagnosticCode::kDuplicateUuid &&
                         diagnostic.entity == DiagnosticEntity(changed_id);
                }),
            2);
  source = fixture.project.find_node(fixture.node);
  ASSERT_TRUE(source->remove_output(changed_id).ok());
  const std::array changes{
      ValidationChange(ConnectorOccurrenceChange{changed_id, fixture.node})};
  const ValidationReport replacement =
      service.validate_incremental(fixture.project, changes);
  EXPECT_FALSE(replacement.complete);
  EXPECT_TRUE(entity_in(replacement.invalidated_entities, changed_id));
  apply_replacement(cache, replacement);
  EXPECT_EQ(cache, service.validate_complete(fixture.project).diagnostics);
  EXPECT_TRUE(std::ranges::any_of(cache, [&](const Diagnostic& diagnostic) {
    return diagnostic.code == DiagnosticCode::kInvalidRandomWeightTotal &&
           diagnostic.entity == DiagnosticEntity(fixture.node) &&
           diagnostic.node == fixture.node;
  }));
}

TEST(ValidationServiceTest,
     NodeScopedOwnershipPreservesAndReplacesSameStaveDiagnostics) {
  ProjectFixture              fixture    = make_clean_project();
  const NodeId                other      = fixture.project.add_node("Other");
  Node*                       other_node = fixture.project.find_node(other);
  std::optional<NodeTimeline> timeline   = NodeTimeline::create(
      {common_measure()},
      fixture.project.find_active_track(fixture.track)->layout().staves());
  ASSERT_TRUE(timeline.has_value());
  other_node->set_timeline(std::move(*timeline));
  complete_stave(*other_node->lane(fixture.track), fixture.stave,
                 fixture.node_end);

  fixture.project.find_node(fixture.node)
      ->lane(fixture.track)
      ->stave(fixture.stave)
      ->voice(*Voice::create(1))
      .clear();
  const ValidationService service;
  const ValidationReport  complete = service.validate_complete(fixture.project);
  ASSERT_TRUE(
      std::ranges::any_of(complete.diagnostics, [&](const Diagnostic& d) {
        return d.code == DiagnosticCode::kRhythmIncomplete &&
               d.node == fixture.node;
      }));
  std::vector<Diagnostic> cache = complete.diagnostics;
  const std::array        changes{ValidationChange(other)};
  const ValidationReport  replacement =
      service.validate_incremental(fixture.project, changes);
  EXPECT_FALSE(replacement.complete);
  apply_replacement(cache, replacement);
  EXPECT_EQ(cache, complete.diagnostics);

  other_node->lane(fixture.track)
      ->stave(fixture.stave)
      ->voice(*Voice::create(1))
      .clear();
  const ValidationReport both = service.validate_complete(fixture.project);
  EXPECT_TRUE(std::ranges::any_of(both.diagnostics, [&](const Diagnostic& d) {
    return d.code == DiagnosticCode::kRhythmIncomplete && d.node == other;
  }));
  EXPECT_GT(std::ranges::count_if(
                both.diagnostics,
                [&](const Diagnostic& d) {
                  return d.code == DiagnosticCode::kRhythmIncomplete &&
                         d.entity == DiagnosticEntity(fixture.stave);
                }),
            1);
}

TEST(ValidationServiceTest,
     GlobalAndUnknownChangesFallBackToCompleteValidation) {
  ProjectFixture          fixture = make_clean_project();
  const ValidationService service;
  const ValidationReport  complete = service.validate_complete(fixture.project);
  const std::array project_change{ValidationChange(fixture.project.id())};
  const std::array unknown_change{ValidationChange(UnknownValidationChange{})};
  EXPECT_EQ(service.validate_incremental(fixture.project, project_change),
            complete);
  EXPECT_EQ(service.validate_incremental(fixture.project, unknown_change),
            complete);
  EXPECT_EQ(service.validate_incremental(fixture.project, {}), complete);
}

TEST(ValidationServiceTest,
     RepresentativeSixtyFourBySixtyFourProjectIsPracticalStructurally) {
  Project                      project(ProjectId::generate(), "Representative");
  std::vector<StaveDefinition> staves;
  std::vector<TrackId>         tracks;
  staves.reserve(Project::kMaxActiveTracks);
  tracks.reserve(Project::kMaxActiveTracks);
  for (std::size_t index = 0; index < Project::kMaxActiveTracks; ++index) {
    StaffLayout layout = StaffLayout::single_staff();
    staves.push_back(layout.staves().front());
    const auto track = project.add_track(
        "Track " + std::to_string(index), layout,
        *MidiChannel::create(static_cast<std::uint8_t>(index % 16)));
    ASSERT_TRUE(track.has_value());
    tracks.push_back(*track);
  }
  const NodeId                node_id = project.add_node("64 x 64");
  std::optional<NodeTimeline> timeline =
      NodeTimeline::create(std::vector<Measure>(64, common_measure()), staves);
  ASSERT_TRUE(timeline.has_value());
  const Rational node_end = timeline->node_end();
  Node*          node     = project.find_node(node_id);
  node->set_timeline(std::move(*timeline));
  for (std::size_t index = 0; index < tracks.size(); ++index)
    complete_stave(*node->lane(tracks[index]), staves[index].id, node_end);

  const ValidationService service;
  EXPECT_TRUE(service.validate_complete(project).diagnostics.empty());
  Project copy = project;
  copy.find_node(node_id)->set_name("Mutated copy");
  EXPECT_TRUE(service.validate_complete(copy).diagnostics.empty());
  EXPECT_NE(project.find_node(node_id)->name(),
            copy.find_node(node_id)->name());
  EXPECT_EQ(staves.size(), 64u);
  EXPECT_EQ(node->timeline()->measures().measure_count(), 64u);
  EXPECT_EQ(kVoiceCount, 4u);
}

}  // namespace
}  // namespace graphscore
