// SPDX-License-Identifier: Apache-2.0

#include "clipboard_test_support.hpp"

#include <array>
#include <utility>
#include <vector>

namespace clipboard_test {

namespace {

VoiceContent quarter_notes(const std::array<Letter, 8>& letters,
                           std::int8_t                  octave) {
  std::vector<VoiceEvent> events;
  events.reserve(letters.size());
  for (const Letter letter : letters)
    events.push_back(make_note(pitch(letter, octave), quarter()));
  return build_voice(std::move(events));
}

VoiceContent repeated_quarter_notes(Letter letter, std::int8_t octave) {
  std::vector<VoiceEvent> events;
  events.reserve(16);
  for (std::size_t i = 0; i < 16; ++i)
    events.push_back(make_note(pitch(letter, octave), quarter()));
  return build_voice(std::move(events));
}

void expect_same_events_at(const VoiceContent&          before,
                           const VoiceContent&          after,
                           const std::vector<Rational>& positions) {
  for (const Rational position : positions) {
    const std::optional<std::size_t> before_index =
        before.find_event_index_at(position);
    const std::optional<std::size_t> after_index =
        after.find_event_index_at(position);
    ASSERT_TRUE(before_index.has_value());
    ASSERT_TRUE(after_index.has_value());
    EXPECT_EQ(after.events()[*after_index], before.events()[*before_index]);
  }
}

const VoiceEvent* event_at(const VoiceContent& content, Rational position) {
  const std::optional<std::size_t> index =
      content.find_event_index_at(position);
  return index.has_value() ? &content.events()[*index] : nullptr;
}

}  // namespace

TEST(ClipboardCommandTest,
     DefaultPasteRestrictsReplacementToMappedTimeStaffAndVoiceScopes) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            repeated_quarter_notes(Letter::kC, 4));
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2,
            repeated_quarter_notes(Letter::kD, 4));
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice3,
            repeated_quarter_notes(Letter::kE, 4));
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice4,
            repeated_quarter_notes(Letter::kF, 4));
  fx.assign(fx.track_a, fx.stave_a_bass, kVoice1,
            quarter_notes({Letter::kC, Letter::kD, Letter::kE, Letter::kF,
                           Letter::kG, Letter::kA, Letter::kB, Letter::kC},
                          3));
  fx.assign(fx.track_a, fx.stave_a_bass, kVoice2,
            repeated_quarter_notes(Letter::kA, 2));
  fx.assign(fx.track_a, fx.stave_a_bass, kVoice3,
            repeated_quarter_notes(Letter::kB, 2));
  fx.assign(fx.track_a, fx.stave_a_bass, kVoice4,
            repeated_quarter_notes(Letter::kC, 2));
  fx.assign(fx.track_b, fx.stave_b, kVoice1,
            repeated_quarter_notes(Letter::kD, 5));
  fx.assign(fx.track_b, fx.stave_b, kVoice2,
            quarter_notes({Letter::kG, Letter::kA, Letter::kB, Letter::kC,
                           Letter::kD, Letter::kE, Letter::kF, Letter::kG},
                          4));
  fx.assign(fx.track_b, fx.stave_b, kVoice3,
            repeated_quarter_notes(Letter::kE, 5));
  fx.assign(fx.track_b, fx.stave_b, kVoice4,
            repeated_quarter_notes(Letter::kF, 5));

  const TrackLane     before_a = fx.lane_of(fx.track_a);
  const TrackLane     before_b = fx.lane_of(fx.track_b);
  const VoiceContent& before_bass =
      before_a.stave(fx.stave_a_bass)->voice(kVoice1);
  const VoiceContent& before_track_b =
      before_b.stave(fx.stave_b)->voice(kVoice2);

  VoiceContent sparse_part =
      build_voice({make_note(pitch(Letter::kF, 5), quarter())});
  ASSERT_TRUE(sparse_part.normalize(rat(1, 2)).ok());
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, std::move(sparse_part)},
       FragmentVoicePart{
           0, 1, kVoice2,
           build_voice({make_note(pitch(Letter::kE, 2), half())})}});
  const PasteAnchor anchor{
      fx.node_id,
      fx.track_a,
      fx.stave_a_bass,
      rat(1, 2),
      {{fx.track_a, fx.stave_a_bass}, {fx.track_b, fx.stave_b}}};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const TrackLane&    after_a = *fx.node()->lane(fx.track_a);
  const TrackLane&    after_b = *fx.node()->lane(fx.track_b);
  const VoiceContent& bass    = after_a.stave(fx.stave_a_bass)->voice(kVoice1);
  const VoiceContent& track_b = after_b.stave(fx.stave_b)->voice(kVoice2);

  const std::vector<Rational> outside_positions{
      Rational(0), rat(1, 4), Rational(1), rat(5, 4), rat(3, 2), rat(7, 4)};
  expect_same_events_at(before_bass, bass, outside_positions);
  expect_same_events_at(before_track_b, track_b, outside_positions);
  const VoiceEvent* pasted_bass_note = event_at(bass, rat(1, 2));
  ASSERT_NE(pasted_bass_note, nullptr);
  ASSERT_TRUE(std::holds_alternative<Note>(*pasted_bass_note));
  EXPECT_EQ(std::get<Note>(*pasted_bass_note).pitch, pitch(Letter::kF, 5));
  const VoiceEvent* pasted_bass_rest = event_at(bass, rat(3, 4));
  ASSERT_NE(pasted_bass_rest, nullptr);
  ASSERT_TRUE(std::holds_alternative<Rest>(*pasted_bass_rest));
  EXPECT_EQ(std::get<Rest>(*pasted_bass_rest).duration, quarter());

  const VoiceEvent* pasted_track_b_note = event_at(track_b, rat(1, 2));
  ASSERT_NE(pasted_track_b_note, nullptr);
  ASSERT_TRUE(std::holds_alternative<Note>(*pasted_track_b_note));
  EXPECT_EQ(std::get<Note>(*pasted_track_b_note).pitch, pitch(Letter::kE, 2));

  for (const Voice voice : {kVoice1, kVoice2, kVoice3, kVoice4}) {
    EXPECT_TRUE(after_a.stave(fx.stave_a_treble)->voice(voice) ==
                before_a.stave(fx.stave_a_treble)->voice(voice));
  }
  for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
    EXPECT_TRUE(after_a.stave(fx.stave_a_bass)->voice(voice) ==
                before_a.stave(fx.stave_a_bass)->voice(voice));
  }
  for (const Voice voice : {kVoice1, kVoice3, kVoice4}) {
    EXPECT_TRUE(after_b.stave(fx.stave_b)->voice(voice) ==
                before_b.stave(fx.stave_b)->voice(voice));
  }
  EXPECT_TRUE(bass.check_complete(fx.node_end()).ok());
  EXPECT_TRUE(track_b.check_complete(fx.node_end()).ok());
}

TEST(ClipboardCommandTest, MultiScopePasteRejectionIsFailureAtomic) {
  Fixture fx;
  fx.assign(fx.track_b, fx.stave_b, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  const TrackLane before_a = fx.lane_of(fx.track_a);
  const TrackLane before_b = fx.lane_of(fx.track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1,
                         build_voice({make_note(pitch(Letter::kF), eighth())})},
       FragmentVoicePart{
           0, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kG), eighth())})}});
  const PasteAnchor anchor{
      fx.node_id,
      fx.track_a,
      fx.stave_a_treble,
      rat(1, 24),
      {{fx.track_a, fx.stave_a_treble}, {fx.track_b, fx.stave_b}}};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
}

}  // namespace clipboard_test
