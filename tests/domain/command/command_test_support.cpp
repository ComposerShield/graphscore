// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

Project make_project() {
  return Project(ProjectId::generate(), "Test Project");
}

NotationSetup make_notation_setup() {
  Project    project = make_project();
  const auto t       = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  assert(t.has_value());
  const TrackId track_id = *t;

  const NodeId node_id = project.add_node("Node");

  std::vector<Measure> measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto tl = NodeTimeline::create(std::move(measures), {});
  assert(tl.has_value());
  project.find_node(node_id)->set_timeline(std::move(*tl));
  const Rational node_end(1);  // One 4/4 measure = 1 whole note

  Node*                    node  = project.find_node(node_id);
  const graphscore::Track* track = project.find_active_track(track_id);
  StaveId                  stave_id;
  for (const graphscore::StaveDefinition& stave_def :
       track->layout().staves()) {
    node->lane(track_id)->ensure_stave(stave_def.id);
    stave_id = stave_def.id;
  }

  return NotationSetup{std::move(project), node_id, track_id, stave_id,
                       node_end};
}

SpelledPitch pitch_c4() {
  return *SpelledPitch::create(Letter::kC, 4);
}

SpelledPitch pitch_d4() {
  return *SpelledPitch::create(Letter::kD, 4);
}

SpelledPitch pitch_e4() {
  return *SpelledPitch::create(Letter::kE, 4);
}

SpelledPitch pitch_g4() {
  return *SpelledPitch::create(Letter::kG, 4);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Duration half() {
  return *Duration::create(NoteValue::kHalf, 0);
}

Duration whole() {
  return *Duration::create(NoteValue::kWhole, 0);
}

Duration eighth() {
  return *Duration::create(NoteValue::kEighth, 0);
}

Duration dotted_half() {
  return *Duration::create(NoteValue::kHalf, 1);
}

// Fill all four voices on a stave with one quarter note each and
// normalize to `node_end`.  Required for whole-TrackLane candidate
// validation now that validate_lane_candidate checks rhythmic
// completeness of every voice in every stave.
void fill_all_voices(TrackLane* lane, StaveId stave_id, Rational node_end) {
  for (int v = 1; v <= 4; ++v) {
    VoiceContent* vc = &lane->stave(stave_id)->voice(
        *Voice::create(static_cast<std::uint8_t>(v)));
    ASSERT_NE(vc, nullptr);
    ASSERT_TRUE(vc->append(make_note(pitch_c4(), quarter())).ok());
    ASSERT_TRUE(vc->normalize(node_end).ok());
  }
}
