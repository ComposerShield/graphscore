// SPDX-License-Identifier: Apache-2.0

#include "app_project.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>

#include <optional>
#include <utility>
#include <vector>

namespace graphscore::writer_app {
// ---- default project (M05 in-memory stub, replaced by M03 persistence) -----

// Builds one single-staff node with two quarter notes spanning one 4/4
// measure. This is the same fixture the notation tests use; it lives here
// because the app owns the project and layout for the handler to read.
//
// `metrics` supplies the glyph metrics — use production BravuraFont when
// rendering to the window; use SelfTestMetrics only for headless tests.
[[nodiscard]] std::optional<DefaultProject> build_default_project(
    const graphscore::GlyphMetrics& metrics) {
  using graphscore::Clef;
  using graphscore::Duration;
  using graphscore::KeySignature;
  using graphscore::layout_notation;
  using graphscore::Letter;
  using graphscore::make_note;
  using graphscore::Measure;
  using graphscore::MidiChannel;
  using graphscore::NodeId;
  using graphscore::NodeTimeline;
  using graphscore::NotationLayoutOptions;
  using graphscore::NotationLayoutResult;
  using graphscore::NoteValue;
  using graphscore::ProjectId;
  using graphscore::SpelledPitch;
  using graphscore::StaffLayout;
  using graphscore::StaveDefinition;
  using graphscore::StaveId;
  using graphscore::TimeSignature;
  using graphscore::TrackId;
  using graphscore::Voice;
  using graphscore::VoiceContent;

  graphscore::Project project{ProjectId::generate(), "Default"};
  const auto          midi_channel = MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", StaffLayout::single_staff(Clef::kTreble), *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const TrackId track_id = *track_added;
  const NodeId  node_id  = project.add_node("Node");
  auto*         lane     = project.find_node(node_id)->lane(track_id);
  const StaveId stave_id = project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<Measure> measures(1, Measure{*time_sig, KeySignature{}});
  auto timeline = NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter_dur = Duration::create(NoteValue::kQuarter, 0);
  if (!quarter_dur.has_value()) {
    return std::nullopt;
  }
  const Duration quarter = *quarter_dur;
  const auto     voice1  = Voice::create(1);
  if (!voice1.has_value()) {
    return std::nullopt;
  }
  VoiceContent& vc = lane->stave(stave_id)->voice(*voice1);

  const auto pitch_c4 = SpelledPitch::create(Letter::kC, 4);
  if (!pitch_c4.has_value()) {
    return std::nullopt;
  }
  if (!vc.append(make_note(*pitch_c4, quarter)).ok()) {
    return std::nullopt;
  }
  const auto pitch_d4 = SpelledPitch::create(Letter::kD, 4);
  if (!pitch_d4.has_value()) {
    return std::nullopt;
  }
  if (!vc.append(make_note(*pitch_d4, quarter)).ok()) {
    return std::nullopt;
  }

  NotationLayoutResult layout_result =
      layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return DefaultProject{std::move(project), node_id, track_id, stave_id,
                        std::move(*layout_result.layout)};
}

}  // namespace graphscore::writer_app
