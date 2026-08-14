// SPDX-License-Identifier: Apache-2.0

#include "selftest_fixtures.hpp"

#include "selftest_support.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {
// Builds a three-track, two-measure (4/4 each) fixture: track_ids[0] and
// track_ids[1] each carry one whole note per measure, spanning the full
// [0, 2) node timeline. track_ids[2] carries a single whole note in the
// first measure only, so its content spans exactly [0, 1) and does not
// overlap [1, 2) at all -- reproducing the "staff at the extreme of the
// range whose own voices carry no overlapping content" scenario
// extend_range_selection's own doc comment on graphscore_notation.hpp
// describes, for the staff-endpoint-preservation test below.
[[nodiscard]] std::optional<KeySelectionProject> build_key_selection_project(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project                project{graphscore::ProjectId::generate(),
                              "KeySelection"};
  std::array<graphscore::TrackId, 3> track_ids{};
  for (std::size_t i = 0; i < track_ids.size(); ++i) {
    const auto midi_channel =
        graphscore::MidiChannel::create(static_cast<std::uint8_t>(i));
    if (!midi_channel.has_value()) {
      return std::nullopt;
    }
    const auto added = project.add_track(
        "Track",
        graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
        *midi_channel);
    if (!added.has_value()) {
      return std::nullopt;
    }
    track_ids[i] = *added;
  }

  const graphscore::NodeId                 node_id = project.add_node("Node");
  std::array<graphscore::StaveId, 3>       stave_ids{};
  std::vector<graphscore::StaveDefinition> stave_defs;
  for (std::size_t i = 0; i < track_ids.size(); ++i) {
    auto* lane = project.find_node(node_id)->lane(track_ids[i]);
    const graphscore::StaveId stave_id =
        project.active_tracks()[i].layout().staves()[0].id;
    stave_ids[i] = stave_id;
    lane->ensure_stave(stave_id);
    stave_defs.push_back(project.active_tracks()[i].layout().staves()[0]);
  }

  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto whole_duration =
      graphscore::Duration::create(graphscore::NoteValue::kWhole, 0);
  if (!whole_duration.has_value()) {
    return std::nullopt;
  }
  const graphscore::Duration whole  = *whole_duration;
  const auto                 voice1 = graphscore::Voice::create(1);
  if (!voice1.has_value()) {
    return std::nullopt;
  }
  const auto pitch =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  if (!pitch.has_value()) {
    return std::nullopt;
  }

  for (std::size_t i = 0; i < 2; ++i) {
    graphscore::VoiceContent& vc = project.find_node(node_id)
                                       ->lane(track_ids[i])
                                       ->stave(stave_ids[i])
                                       ->voice(*voice1);
    if (!vc.append(graphscore::make_note(*pitch, whole)).ok()) {
      return std::nullopt;
    }
    if (!vc.append(graphscore::make_note(*pitch, whole)).ok()) {
      return std::nullopt;
    }
  }
  {
    graphscore::VoiceContent& vc = project.find_node(node_id)
                                       ->lane(track_ids[2])
                                       ->stave(stave_ids[2])
                                       ->voice(*voice1);
    if (!vc.append(graphscore::make_note(*pitch, whole)).ok()) {
      return std::nullopt;
    }
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return KeySelectionProject{std::move(project), node_id, track_ids, stave_ids,
                             std::move(*layout_result.layout)};
}

// One single-staff node with a complete voice: two quarter notes (C4, D4)
// followed by normalized rests filling a 4/4 measure. Complete so the move
// command's normalize step is a no-op and undo/redo stay exact.
[[nodiscard]] std::optional<NoteheadMoveFixture> build_notehead_move_fixture(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project project{graphscore::ProjectId::generate(),
                              "NoteheadMove"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      1, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter_dur =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  if (!quarter_dur.has_value()) {
    return std::nullopt;
  }
  const graphscore::Duration quarter = *quarter_dur;
  const auto                 voice1  = graphscore::Voice::create(1);
  if (!voice1.has_value()) {
    return std::nullopt;
  }
  graphscore::VoiceContent& vc = lane->stave(stave_id)->voice(*voice1);

  const auto pitch_c4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto pitch_d4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kD, 4);
  if (!pitch_c4.has_value() || !pitch_d4.has_value()) {
    return std::nullopt;
  }
  if (!vc.append(graphscore::make_note(*pitch_c4, quarter)).ok()) {
    return std::nullopt;
  }
  if (!vc.append(graphscore::make_note(*pitch_d4, quarter)).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  const graphscore::NotationEntityId first_id =
      graphscore::event_id(vc.events()[0]);
  const graphscore::NotationEntityId second_id =
      graphscore::event_id(vc.events()[1]);

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return NoteheadMoveFixture{std::move(project),
                             node_id,
                             track_id,
                             stave_id,
                             first_id,
                             second_id,
                             std::move(*layout_result.layout)};
}

[[nodiscard]] std::optional<NoteheadClickFixture> build_notehead_click_fixture(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Click"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      1, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  const auto eighth =
      graphscore::Duration::create(graphscore::NoteValue::kEighth, 0);
  if (!quarter.has_value() || !eighth.has_value()) {
    return std::nullopt;
  }
  const graphscore::Voice   voice1 = voice_one();
  graphscore::VoiceContent& vc     = lane->stave(stave_id)->voice(voice1);

  const auto c4 = graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto d4 = graphscore::SpelledPitch::create(graphscore::Letter::kD, 4);
  const auto e4 = graphscore::SpelledPitch::create(graphscore::Letter::kE, 4);
  const auto g4 = graphscore::SpelledPitch::create(graphscore::Letter::kG, 4);
  const auto f4 = graphscore::SpelledPitch::create(graphscore::Letter::kF, 4);
  if (!c4.has_value() || !d4.has_value() || !e4.has_value() ||
      !g4.has_value() || !f4.has_value()) {
    return std::nullopt;
  }

  if (!vc.append(graphscore::make_note(*c4, *quarter)).ok()) {
    return std::nullopt;
  }
  const graphscore::Note principal = graphscore::make_note(*d4, *quarter);
  if (!vc.append(principal).ok()) {
    return std::nullopt;
  }
  const graphscore::ChordNote moved{graphscore::NotationEntityId::generate(),
                                    *e4, false};
  const graphscore::ChordNote other{graphscore::NotationEntityId::generate(),
                                    *g4, false};
  const graphscore::NotationEntityId chord_note_id  = moved.id;
  const graphscore::NotationEntityId chord_other_id = other.id;
  const graphscore::Chord            chord =
      graphscore::make_chord(*quarter, {moved, other});
  const graphscore::NotationEntityId chord_id = chord.id;
  if (!vc.append(chord).ok()) {
    return std::nullopt;
  }
  const graphscore::GraceGroup group = graphscore::make_grace_group(
      principal.id, {graphscore::GraceNote{
                        graphscore::NotationEntityId::generate(), *f4, *eighth,
                        graphscore::GraceNoteType::kAcciaccatura, true}});
  const graphscore::NotationEntityId grace_id = group.notes[0].id;
  if (!vc.add_grace_group(group).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return NoteheadClickFixture{
      std::move(project), node_id,  track_id,
      stave_id,           chord_id, chord_note_id,
      chord_other_id,     grace_id, std::move(*layout_result.layout)};
}

[[nodiscard]] std::optional<CrossMeasureTieFixture>
build_cross_measure_tie_fixture(
    const graphscore::GlyphMetrics&          metrics,
    const graphscore::NotationLayoutOptions& options) {
  graphscore::Project project{graphscore::ProjectId::generate(), "CrossTie"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  if (!quarter.has_value()) {
    return std::nullopt;
  }
  const auto pitch_c4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  if (!pitch_c4.has_value()) {
    return std::nullopt;
  }
  const graphscore::Voice   voice1 = voice_one();
  graphscore::VoiceContent& vc     = lane->stave(stave_id)->voice(voice1);

  for (int i = 0; i < 3; ++i) {
    if (!vc.append(graphscore::make_rest(*quarter)).ok()) {
      return std::nullopt;
    }
  }
  const graphscore::Note first =
      graphscore::make_note(*pitch_c4, *quarter, true);
  const graphscore::NotationEntityId first_id = first.id;
  if (!vc.append(first).ok()) {
    return std::nullopt;
  }
  const graphscore::Note second = graphscore::make_note(*pitch_c4, *quarter);
  const graphscore::NotationEntityId second_id = second.id;
  if (!vc.append(second).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics, options);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return CrossMeasureTieFixture{std::move(project),
                                node_id,
                                track_id,
                                stave_id,
                                first_id,
                                second_id,
                                std::move(*layout_result.layout)};
}

[[nodiscard]] std::optional<CrossMeasureChordTieFixture>
build_cross_measure_chord_tie_fixture(
    const graphscore::GlyphMetrics&          metrics,
    const graphscore::NotationLayoutOptions& options) {
  graphscore::Project project{graphscore::ProjectId::generate(),
                              "CrossChordTie"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  if (!quarter.has_value()) {
    return std::nullopt;
  }
  const auto pitch_c4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto pitch_e4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kE, 4);
  if (!pitch_c4.has_value() || !pitch_e4.has_value()) {
    return std::nullopt;
  }
  const graphscore::Voice   voice1 = voice_one();
  graphscore::VoiceContent& vc     = lane->stave(stave_id)->voice(voice1);

  for (int i = 0; i < 3; ++i) {
    if (!vc.append(graphscore::make_rest(*quarter)).ok()) {
      return std::nullopt;
    }
  }

  const graphscore::ChordNote measure0_c4{
      graphscore::NotationEntityId::generate(), *pitch_c4, false};
  const graphscore::ChordNote measure0_e4{
      graphscore::NotationEntityId::generate(), *pitch_e4, true};
  const graphscore::NotationEntityId measure0_e4_id = measure0_e4.id;
  const graphscore::Chord            measure0_chord =
      graphscore::make_chord(*quarter, {measure0_c4, measure0_e4});
  if (!vc.append(measure0_chord).ok()) {
    return std::nullopt;
  }

  const graphscore::ChordNote measure1_c4{
      graphscore::NotationEntityId::generate(), *pitch_c4, false};
  const graphscore::ChordNote measure1_e4{
      graphscore::NotationEntityId::generate(), *pitch_e4, false};
  const graphscore::NotationEntityId measure1_c4_id = measure1_c4.id;
  const graphscore::Chord            measure1_chord =
      graphscore::make_chord(*quarter, {measure1_c4, measure1_e4});
  const graphscore::NotationEntityId measure1_chord_id = measure1_chord.id;
  if (!vc.append(measure1_chord).ok()) {
    return std::nullopt;
  }

  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics, options);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return CrossMeasureChordTieFixture{
      std::move(project), node_id,
      track_id,           stave_id,
      measure0_e4_id,     measure1_chord_id,
      measure1_c4_id,     std::move(*layout_result.layout)};
}

[[nodiscard]] std::optional<TwoSystemLocalFixture>
build_two_system_local_fixture(
    const graphscore::GlyphMetrics&          metrics,
    const graphscore::NotationLayoutOptions& options) {
  graphscore::Project project{graphscore::ProjectId::generate(), "TwoSystem"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  if (!quarter.has_value()) {
    return std::nullopt;
  }
  const auto pitch_c4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto pitch_e4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kE, 4);
  if (!pitch_c4.has_value() || !pitch_e4.has_value()) {
    return std::nullopt;
  }
  const graphscore::Voice   voice1 = voice_one();
  graphscore::VoiceContent& vc     = lane->stave(stave_id)->voice(voice1);

  const graphscore::Note first = graphscore::make_note(*pitch_c4, *quarter);
  const graphscore::NotationEntityId first_id = first.id;
  if (!vc.append(first).ok()) {
    return std::nullopt;
  }
  // Three explicit rests fill the rest of measure 0, so measure 1's note
  // starts exactly on the barline.
  for (int i = 0; i < 3; ++i) {
    if (!vc.append(graphscore::make_rest(*quarter)).ok()) {
      return std::nullopt;
    }
  }
  const graphscore::Note second = graphscore::make_note(*pitch_e4, *quarter);
  const graphscore::NotationEntityId second_id = second.id;
  if (!vc.append(second).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics, options);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return TwoSystemLocalFixture{std::move(project),
                               node_id,
                               track_id,
                               stave_id,
                               first_id,
                               second_id,
                               std::move(*layout_result.layout)};
}

// Single-staff, one-measure fixture with the given key signature and one
// C4 quarter note (plus normalized rests). `source_id` names that note.
[[nodiscard]] std::optional<IntervalEntryFixture> build_interval_note_fixture(
    const graphscore::GlyphMetrics& metrics, std::int8_t fifths) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Interval"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  const auto key_sig  = graphscore::KeySignature::create(fifths);
  if (!time_sig.has_value() || !key_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      1, graphscore::Measure{*time_sig, *key_sig});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  if (!quarter.has_value()) {
    return std::nullopt;
  }
  const graphscore::Voice   voice1 = voice_one();
  graphscore::VoiceContent& vc     = lane->stave(stave_id)->voice(voice1);
  const auto c4 = graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  if (!c4.has_value()) {
    return std::nullopt;
  }
  const graphscore::Note note = graphscore::make_note(*c4, *quarter);
  if (!vc.append(note).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }
  return IntervalEntryFixture{
      std::move(project), node_id, track_id,
      stave_id,           note.id, std::move(*layout_result.layout)};
}

// Single-staff, one-measure fixture with the given key signature and one
// two-note C4/E4 quarter chord. `source_id` names the C4 ChordNote.
[[nodiscard]] std::optional<IntervalEntryFixture> build_interval_chord_fixture(
    const graphscore::GlyphMetrics& metrics, std::int8_t fifths) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Interval"};

  auto note_fixture = [&]() -> std::optional<IntervalEntryFixture> {
    const auto midi_channel = graphscore::MidiChannel::create(0);
    if (!midi_channel.has_value()) {
      return std::nullopt;
    }
    const auto track_added = project.add_track(
        "Track",
        graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
        *midi_channel);
    if (!track_added.has_value()) {
      return std::nullopt;
    }
    const graphscore::TrackId track_id = *track_added;
    const graphscore::NodeId  node_id  = project.add_node("Node");
    auto*                     lane = project.find_node(node_id)->lane(track_id);
    const graphscore::StaveId stave_id =
        project.active_tracks()[0].layout().staves()[0].id;
    lane->ensure_stave(stave_id);

    std::vector<graphscore::StaveDefinition> stave_defs;
    stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
    const auto time_sig = graphscore::TimeSignature::create(4, 4);
    const auto key_sig  = graphscore::KeySignature::create(fifths);
    if (!time_sig.has_value() || !key_sig.has_value()) {
      return std::nullopt;
    }
    std::vector<graphscore::Measure> measures(
        1, graphscore::Measure{*time_sig, *key_sig});
    auto timeline =
        graphscore::NodeTimeline::create(std::move(measures), stave_defs);
    if (!timeline.has_value()) {
      return std::nullopt;
    }
    project.find_node(node_id)->set_timeline(std::move(*timeline));

    const auto quarter =
        graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
    if (!quarter.has_value()) {
      return std::nullopt;
    }
    const graphscore::Voice   voice1 = voice_one();
    graphscore::VoiceContent& vc     = lane->stave(stave_id)->voice(voice1);
    const auto c4 = graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
    const auto e4 = graphscore::SpelledPitch::create(graphscore::Letter::kE, 4);
    if (!c4.has_value() || !e4.has_value()) {
      return std::nullopt;
    }
    const graphscore::ChordNote c_note{graphscore::NotationEntityId::generate(),
                                       *c4, false};
    const graphscore::ChordNote e_note{graphscore::NotationEntityId::generate(),
                                       *e4, false};
    const graphscore::NotationEntityId c_id = c_note.id;
    if (!vc.append(graphscore::make_chord(*quarter, {c_note, e_note})).ok()) {
      return std::nullopt;
    }
    const graphscore::Rational node_end =
        project.find_node(node_id)->timeline()->node_end();
    if (!vc.normalize(node_end).ok()) {
      return std::nullopt;
    }

    graphscore::NotationLayoutResult layout_result =
        graphscore::layout_notation(project, node_id, metrics);
    if (!layout_result || !layout_result.layout.has_value()) {
      return std::nullopt;
    }
    return IntervalEntryFixture{
        std::move(project), node_id, track_id,
        stave_id,           c_id,    std::move(*layout_result.layout)};
  }();

  return note_fixture;
}

}  // namespace graphscore::writer_app
