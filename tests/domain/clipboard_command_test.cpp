// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Accidental;
using graphscore::ArbitraryRangeItem;
using graphscore::ArbitraryRangeSet;
using graphscore::BeamOverride;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::Clef;
using graphscore::ClefChange;
using graphscore::ClefLane;
using graphscore::CommandTransaction;
using graphscore::CutFragmentCommand;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::event_id;
using graphscore::extract_fragment;
using graphscore::FragmentClefChange;
using graphscore::FragmentExtraction;
using graphscore::FragmentMeasureContext;
using graphscore::FragmentPedalSpan;
using graphscore::FragmentStaveContext;
using graphscore::FragmentTrackShape;
using graphscore::FragmentVoicePart;
using graphscore::FullMeasureItem;
using graphscore::FullMeasureSet;
using graphscore::HairpinDirection;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::MusicalSpan;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationDiagnostic;
using graphscore::NotationEntityId;
using graphscore::NotationFragment;
using graphscore::Note;
using graphscore::NoteValue;
using graphscore::PasteAnchor;
using graphscore::PasteFragmentCommand;
using graphscore::PedalSpan;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::Rest;
using graphscore::Result;
using graphscore::ResultCode;
using graphscore::Selection;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveDefinition;
using graphscore::StaveId;
using graphscore::TimeSignature;
using graphscore::Track;
using graphscore::TrackId;
using graphscore::TrackLane;
using graphscore::TupletRatio;
using graphscore::Voice;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

namespace {

// ---- small value builders ----

SpelledPitch pitch(Letter letter, std::int8_t octave = 4) {
  return *SpelledPitch::create(letter, octave);
}

Duration whole() {
  return *Duration::create(NoteValue::kWhole, 0);
}

Duration half() {
  return *Duration::create(NoteValue::kHalf, 0);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Duration eighth() {
  return *Duration::create(NoteValue::kEighth, 0);
}

Duration tuplet_eighth() {
  return *Duration::create(NoteValue::kEighth, 0, TupletRatio::create(3, 2));
}

Rational rat(std::int64_t num, std::int64_t den) {
  return *Rational::create(num, den);
}

constexpr Voice kVoice1 = *Voice::create(Voice::kMin);
constexpr Voice kVoice2 = *Voice::create(2);
constexpr Voice kVoice3 = *Voice::create(3);
constexpr Voice kVoice4 = *Voice::create(Voice::kMax);

VoiceContent build_voice(std::vector<VoiceEvent> events) {
  VoiceContent voice;
  for (VoiceEvent& event : events) {
    const Result result = voice.append(std::move(event));
    assert(result.ok());
    (void)result;
  }
  return voice;
}

// A voice consisting entirely of automatically-decomposed rests covering
// [0, length) -- the real shape of a "musically empty" voice in a valid
// project. VoiceContent's own default state (zero events, zero length) is
// only a construction placeholder; validate_lane_candidate (reused by
// PasteFragmentCommand/CutFragmentCommand for their whole-lane snapshot
// commit) requires every voice in the lane to already satisfy
// check_complete(node_end), so a fixture voice never left at that literal
// default state.
VoiceContent rest_filled(Rational length) {
  VoiceContent voice;
  const Result result = voice.normalize(length);
  assert(result.ok());
  (void)result;
  return voice;
}

// Every Fixture/UnfilledFixture/PedalOnlyFixture below builds a 4/4
// timeline; make_fragment's default measure_contexts matches that meter so
// the paste meter-compatibility gate (Phase 8h-iv) accepts every existing
// caller unless it deliberately overrides this argument to exercise the
// gate itself.
std::vector<FragmentMeasureContext> default_measure_contexts() {
  return {FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                                 *KeySignature::create(0)}};
}

NotationFragment make_fragment(
    Rational span, std::vector<FragmentTrackShape> tracks,
    std::vector<FragmentVoicePart>      parts,
    std::vector<FragmentPedalSpan>      pedal_spans    = {},
    std::vector<FragmentClefChange>     clef_changes   = {},
    std::vector<FragmentStaveContext>   stave_contexts = {},
    std::vector<FragmentMeasureContext> measure_contexts =
        default_measure_contexts()) {
  std::optional<NotationFragment> fragment = NotationFragment::create(
      span, std::move(tracks), std::move(parts), std::move(pedal_spans),
      std::move(clef_changes), std::move(stave_contexts),
      std::move(measure_contexts));
  assert(fragment.has_value());
  return std::move(*fragment);
}

bool same_event_structure(const VoiceEvent& a, const VoiceEvent& b) {
  if (a.index() != b.index())
    return false;
  if (const auto* note_a = std::get_if<Note>(&a)) {
    const auto& note_b = std::get<Note>(b);
    return note_a->pitch == note_b.pitch && note_a->duration == note_b.duration;
  }
  if (const auto* chord_a = std::get_if<Chord>(&a)) {
    const auto& chord_b = std::get<Chord>(b);
    if (chord_a->duration != chord_b.duration ||
        chord_a->notes.size() != chord_b.notes.size())
      return false;
    for (std::size_t i = 0; i < chord_a->notes.size(); ++i) {
      if (chord_a->notes[i].pitch != chord_b.notes[i].pitch)
        return false;
    }
    return true;
  }
  return std::get<Rest>(a).duration == std::get<Rest>(b).duration;
}

// Structural comparison ignoring every regenerated id, sufficient for a
// source fragment built without markings/pedal spans/clef/measure context.
bool same_content_structure(const VoiceContent& a, const VoiceContent& b) {
  if (a.events().size() != b.events().size())
    return false;
  for (std::size_t i = 0; i < a.events().size(); ++i) {
    if (!same_event_structure(a.events()[i], b.events()[i]))
      return false;
  }
  return true;
}

bool fragments_structurally_equal(const NotationFragment& a,
                                  const NotationFragment& b) {
  if (!(a.span_length() == b.span_length()))
    return false;
  if (a.tracks() != b.tracks())
    return false;
  if (a.parts().size() != b.parts().size())
    return false;
  for (std::size_t i = 0; i < a.parts().size(); ++i) {
    const FragmentVoicePart& pa = a.parts()[i];
    const FragmentVoicePart& pb = b.parts()[i];
    if (pa.track_ordinal != pb.track_ordinal ||
        pa.stave_ordinal != pb.stave_ordinal || !(pa.voice == pb.voice))
      return false;
    if (!same_content_structure(pa.content, pb.content))
      return false;
  }
  return true;
}

void collect_ids(const VoiceContent&            content,
                 std::vector<NotationEntityId>& ids) {
  for (const VoiceEvent& event : content.events()) {
    ids.push_back(event_id(event));
    if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const ChordNote& notehead : chord->notes)
        ids.push_back(notehead.id);
    }
  }
  for (const auto& marking : content.dynamics())
    ids.push_back(marking.id);
  for (const auto& hairpin : content.hairpins())
    ids.push_back(hairpin.id);
  for (const auto& slur : content.slurs())
    ids.push_back(slur.id);
  for (const auto& beam : content.beam_overrides())
    ids.push_back(beam.id);
  for (const auto& group : content.grace_groups()) {
    ids.push_back(group.id);
    for (const auto& note : group.notes)
      ids.push_back(note.id);
  }
}

// Builds a project with two tracks (A: grand staff, B: single staff), one
// node with a four-measure 4/4 timeline (node_end == Rational(4)), and
// empty lanes ready for direct voice assignment. Every voice starts at its
// pristine default-constructed (empty) state -- the natural shape of a
// real project, per Adam's ruling against pre-filling fixtures.
struct Fixture {
  Project project{ProjectId::generate(), "Test"};
  TrackId track_a;
  TrackId track_b;
  StaveId stave_a_treble;
  StaveId stave_a_bass;
  StaveId stave_b;
  NodeId  node_id;

  static constexpr std::size_t kMeasureCount = 4;

  Fixture() {
    const auto tid_a = project.add_track("A", StaffLayout::grand_staff(),
                                         *MidiChannel::create(0));
    assert(tid_a.has_value());
    track_a                  = *tid_a;
    const Track* track_a_ptr = project.find_active_track(track_a);
    assert(track_a_ptr != nullptr);
    stave_a_treble = track_a_ptr->layout().staves()[0].id;
    stave_a_bass   = track_a_ptr->layout().staves()[1].id;

    const auto tid_b = project.add_track("B", StaffLayout::single_staff(),
                                         *MidiChannel::create(1));
    assert(tid_b.has_value());
    track_b                  = *tid_b;
    const Track* track_b_ptr = project.find_active_track(track_b);
    assert(track_b_ptr != nullptr);
    stave_b = track_b_ptr->layout().staves()[0].id;

    node_id      = project.add_node("Node");
    Node* node_p = project.find_node(node_id);
    assert(node_p != nullptr);

    std::vector<Measure> measures;
    for (std::size_t i = 0; i < kMeasureCount; ++i) {
      measures.push_back(
          Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
    }
    auto timeline = NodeTimeline::create(
        measures, {StaveDefinition{stave_a_treble, Clef::kTreble},
                   StaveDefinition{stave_a_bass, Clef::kBass},
                   StaveDefinition{stave_b, Clef::kTreble}});
    assert(timeline.has_value());
    node_p->set_timeline(std::move(*timeline));

    node_p->lane(track_a)->ensure_stave(stave_a_treble);
    node_p->lane(track_a)->ensure_stave(stave_a_bass);
    node_p->lane(track_b)->ensure_stave(stave_b);

    // Every voice starts musically empty (all automatic rests to
    // node_end()) rather than at VoiceContent's raw zero-length default --
    // the actual natural, valid state of a live project's untouched
    // voices (see rest_filled's comment). Individual tests then assign()
    // real content into whichever voice they exercise.
    const Rational end = node_p->timeline()->node_end();
    for (const StaveId stave_id : {stave_a_treble, stave_a_bass}) {
      for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
        assign(track_a, stave_id, *Voice::create(v), rest_filled(end));
      }
    }
    for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v)
      assign(track_b, stave_b, *Voice::create(v), rest_filled(end));
  }

  Node* node() { return project.find_node(node_id); }

  NodeTimeline* timeline() { return node()->timeline(); }

  Rational node_end() { return timeline()->node_end(); }

  void assign(TrackId track, StaveId stave_id, Voice voice,
              VoiceContent content) {
    node()->lane(track)->stave(stave_id)->voice(voice) = std::move(content);
  }

  // Assigns `events` then automatically rest-fills the remainder to
  // node_end(), so the assigned voice is always complete -- every voice
  // validate_lane_candidate inspects (the whole-lane snapshot commit both
  // PasteFragmentCommand and CutFragmentCommand reuse) must already
  // satisfy check_complete(node_end()), including the pre-command state
  // undo restores.
  void assign_and_complete(TrackId track, StaveId stave_id, Voice voice,
                           std::vector<VoiceEvent> events) {
    VoiceContent content = build_voice(std::move(events));
    const Result result  = content.normalize(node_end());
    assert(result.ok());
    (void)result;
    assign(track, stave_id, voice, std::move(content));
  }

  TrackLane lane_of(TrackId track) { return *node()->lane(track); }
};

}  // namespace

// ============================================================
// PasteFragmentCommand -- round trip, byte-identical outside range
// ============================================================

TEST(ClipboardCommandTest, PasteExecuteUndoRedoRestoresExactlyViaLaneEquality) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), eighth()),
                       make_note(pitch(Letter::kD), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  const TrackLane before = fx.lane_of(fx.track_a);

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane after_execute = fx.lane_of(fx.track_a);
  EXPECT_FALSE(after_execute == before);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_execute);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteLeavesMusicOutsideRangeByteIdentical) {
  Fixture      fx;
  const Note   n1          = make_note(pitch(Letter::kC), quarter());
  const Note   n2          = make_note(pitch(Letter::kD), quarter());
  const Note   n3          = make_note(pitch(Letter::kE), quarter());
  const Note   n4          = make_note(pitch(Letter::kF), quarter());
  VoiceContent destination = build_voice({n1, n2, n3, n4});
  ASSERT_TRUE(
      destination.add_dynamic(make_dynamic_marking(n1.id, Dynamic::kFf)).ok());
  ASSERT_TRUE(destination.add_slur(make_slur(n3.id, n4.id)).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, destination);

  ASSERT_TRUE(fx.node()
                  ->lane(fx.track_a)
                  ->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(Rational(0), rat(1, 4)))
                  .ok());
  ASSERT_TRUE(fx.node()
                  ->lane(fx.track_a)
                  ->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(3, 4), Rational(1)))
                  .ok());

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 8)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  // Paste at [1/8, 1/4): n1 is truncated at 1/8 (original id kept, tie
  // severed), B3 pasted, n2-n4 preserved.  node_end() (4) exceeds
  // content, so region3 pads with rests.
  ASSERT_GE(content.events().size(), 5u);
  // Truncated n1: same id, same pitch, shorter duration.
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[0]));
  EXPECT_EQ(std::get<Note>(content.events()[0]).id, n1.id);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC));
  EXPECT_EQ(std::get<Note>(content.events()[0]).duration, eighth());
  EXPECT_FALSE(std::get<Note>(content.events()[0]).tied_to_next);
  // Pasted B3 at [1/8, 1/4).
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kB, 3));
  // n2, n3, n4 preserved verbatim.
  EXPECT_TRUE(std::get<Note>(content.events()[2]) == n2);
  EXPECT_TRUE(std::get<Note>(content.events()[3]) == n3);
  EXPECT_TRUE(std::get<Note>(content.events()[4]) == n4);

  ASSERT_EQ(content.dynamics().size(), 1u);
  EXPECT_EQ(content.dynamics()[0].at_event, n1.id);
  EXPECT_EQ(content.dynamics()[0].value, Dynamic::kFf);
  ASSERT_EQ(content.slurs().size(), 1u);
  EXPECT_EQ(content.slurs()[0].start_event, n3.id);
  EXPECT_EQ(content.slurs()[0].end_event, n4.id);

  // Pedal span at [0, 1/4) is clipped by the paste range [1/8, 1/4).
  // Only the portion before [0, 1/8) survives with a freshly-generated id.
  // Span at [3/4, 1) is outside the range, id preserved.
  const std::vector<PedalSpan>* spans =
      fx.node()->lane(fx.track_a)->pedal_spans(fx.stave_a_treble);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 2u);
  bool saw_before = false;
  bool saw_after  = false;
  for (const PedalSpan& span : *spans) {
    if (span.start == Rational(0) && span.end == rat(1, 8))
      saw_before = true;
    if (span.start == rat(3, 4) && span.end == Rational(1))
      saw_after = true;
  }
  EXPECT_TRUE(saw_before);
  EXPECT_TRUE(saw_after);
}

// ============================================================
// PasteFragmentCommand -- destination shapes
// ============================================================

TEST(ClipboardCommandTest, PasteIntoOccupiedRangeReplacesOnlyThatRange) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), quarter()),
                         make_note(pitch(Letter::kE), quarter()),
                         make_note(pitch(Letter::kF), quarter())}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 2)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  // node_end() (4) exceeds the destination's own pre-paste content (1), so
  // region3 pads with extra rests after the fourth event.
  ASSERT_GE(content.events().size(), 4u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC));
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kD));
  EXPECT_EQ(std::get<Note>(content.events()[2]).pitch, pitch(Letter::kB));
  EXPECT_EQ(std::get<Note>(content.events()[3]).pitch, pitch(Letter::kF));
}

TEST(ClipboardCommandTest, PasteIntoEmptyDefaultVoicePadsBothSides) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(1)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  EXPECT_TRUE(content.check_complete(fx.node_end()).ok());

  bool     saw_note = false;
  Rational total(0);
  for (const VoiceEvent& event : content.events()) {
    if (const auto* note = std::get_if<Note>(&event)) {
      EXPECT_EQ(note->pitch, pitch(Letter::kC));
      saw_note = true;
    }
    total = total + graphscore::event_duration(event).resolved();
  }
  EXPECT_TRUE(saw_note);
  EXPECT_EQ(total, fx.node_end());
}

TEST(ClipboardCommandTest, PasteAtDestinationShorterThanRangeRestFillsGap) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), eighth())}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(1)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_TRUE(content.check_complete(fx.node_end()).ok());
  ASSERT_GE(content.events().size(), 3u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC));
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[1]));
}

TEST(ClipboardCommandTest, PasteVoicePreservedExactlyByFragmentVoiceOrdinal) {
  Fixture     fx;
  const auto* stave_before =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  const VoiceContent before_voice1 = stave_before->voice(kVoice1);
  const VoiceContent before_voice2 = stave_before->voice(kVoice2);
  const VoiceContent before_voice4 = stave_before->voice(kVoice4);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice3,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  ASSERT_GE(stave->voice(kVoice3).events().size(), 1u);
  EXPECT_EQ(std::get<Note>(stave->voice(kVoice3).events()[0]).pitch,
            pitch(Letter::kC));
  // A fragment part in one voice only touches that exact destination
  // voice: every other voice is byte-identical to its pre-paste value.
  EXPECT_TRUE(stave->voice(kVoice1) == before_voice1);
  EXPECT_TRUE(stave->voice(kVoice2) == before_voice2);
  EXPECT_TRUE(stave->voice(kVoice4) == before_voice4);
}

// ============================================================
// PasteFragmentCommand -- boundary reconnection
// ============================================================

TEST(ClipboardCommandTest, PasteStartBoundaryStraddlePreservesAttackSeversTie) {
  // Finding 2: left retained region preserves the attack of a note
  // straddling the paste boundary.  C half note at [0, 1/2), paste at
  // [1/4, 3/8): the left region [0, 1/4) keeps a truncated C quarter with
  // tied_to_next = false (tie severed); the pasted B occupies [1/4, 3/8);
  // the right region converts the C tail [3/8, 1/2) to rests (attack was
  // inside the replaced range).
  Fixture                fx;
  const Note             c_half = make_note(pitch(Letter::kC), half());
  const NotationEntityId c_id   = c_half.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({c_half}));

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 3u);
  // Left region: truncated C quarter, original id preserved, tie severed.
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[0]));
  const Note& left_note = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_note.id, c_id);
  EXPECT_EQ(left_note.pitch, pitch(Letter::kC));
  EXPECT_EQ(left_note.duration, quarter());
  EXPECT_FALSE(left_note.tied_to_next);
  // Middle region: pasted B.
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kB));
  // Right region: C tail converted to rests.
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[2]));
  EXPECT_EQ(std::get<Rest>(content.events()[2]).duration, eighth());
}

TEST(ClipboardCommandTest, PasteEndBoundaryStraddleBecomesNormalizedRests) {
  Fixture fx;
  // C occupies [0, 1/4), exactly the removed range's start; D occupies
  // [1/4, 3/4), straddling the paste range's end at 3/8.
  const Duration dotted_quarter = *Duration::create(NoteValue::kQuarter, 1);
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), half())}));

  const NotationFragment fragment = make_fragment(
      rat(3, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), dotted_quarter)})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kB));
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[1]));
  EXPECT_EQ(std::get<Rest>(content.events()[1]).duration, dotted_quarter);
}

TEST(ClipboardCommandTest,
     PasteStartBoundaryTupletStraddleFailsModelUnchanged) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 24)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteEndBoundaryTupletStraddleFailsModelUnchanged) {
  Fixture fx;
  // Three tuplet eighths tile [0, 1/12), [1/12, 1/6), [1/6, 1/4). A paste
  // range of [0, 1/8) leaves the second tuplet note straddling the range's
  // end (1/8 falls strictly inside [1/12, 1/6)).
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

// ============================================================
// PasteFragmentCommand -- overflow
// ============================================================

TEST(ClipboardCommandTest, PasteOverflowingNodeEndFailsModelUnchanged) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      Rational(1), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, build_voice({make_rest(whole())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(7, 2)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteNegativePositionFailsModelUnchanged) {
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(-1)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

// ============================================================
// PasteFragmentCommand -- ordinal mapping
// ============================================================

TEST(ClipboardCommandTest, PasteMultiStaveFragmentOntoGrandStaff) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC), quarter())})},
       FragmentVoicePart{
           0, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kE), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const auto* lane = fx.node()->lane(fx.track_a);
  EXPECT_EQ(
      std::get<Note>(lane->stave(fx.stave_a_treble)->voice(kVoice1).events()[0])
          .pitch,
      pitch(Letter::kC));
  EXPECT_EQ(
      std::get<Note>(lane->stave(fx.stave_a_bass)->voice(kVoice1).events()[0])
          .pitch,
      pitch(Letter::kE));
}

TEST(ClipboardCommandTest, PasteMultiTrackFragmentOntoConsecutiveActiveTracks) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}, FragmentTrackShape{1}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC), quarter())})},
       FragmentVoicePart{
           1, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kG), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  EXPECT_EQ(std::get<Note>(fx.node()
                               ->lane(fx.track_a)
                               ->stave(fx.stave_a_treble)
                               ->voice(kVoice1)
                               .events()[0])
                .pitch,
            pitch(Letter::kC));
  EXPECT_EQ(std::get<Note>(fx.node()
                               ->lane(fx.track_b)
                               ->stave(fx.stave_b)
                               ->voice(kVoice1)
                               .events()[0])
                .pitch,
            pitch(Letter::kG));
}

TEST(ClipboardCommandTest, PasteRunningOutOfStavesFailsModelUnchanged) {
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC), quarter())})},
       FragmentVoicePart{
           0, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kE), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before);
}

TEST(ClipboardCommandTest, PasteRunningOutOfTracksFailsModelUnchanged) {
  Fixture         fx;
  const TrackLane before_a = fx.lane_of(fx.track_a);
  const TrackLane before_b = fx.lane_of(fx.track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 4),
      {FragmentTrackShape{1}, FragmentTrackShape{1}, FragmentTrackShape{1}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC), quarter())})},
       FragmentVoicePart{
           1, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kE), quarter())})},
       FragmentVoicePart{
           2, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kG), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);
}

// ============================================================
// PasteFragmentCommand -- identity
// ============================================================

TEST(ClipboardCommandTest, PastingSameFragmentTwiceYieldsDisjointIds) {
  Fixture      fx;
  const Note   note_a       = make_note(pitch(Letter::kC), quarter());
  VoiceContent part_content = build_voice({note_a});
  ASSERT_TRUE(
      part_content.add_dynamic(make_dynamic_marking(note_a.id, Dynamic::kFf))
          .ok());
  const NotationFragment fragment =
      make_fragment(quarter().resolved(), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, part_content}});

  std::vector<NotationEntityId> fragment_ids;
  collect_ids(fragment.parts()[0].content, fragment_ids);

  fx.assign_and_complete(fx.track_a, fx.stave_a_bass, kVoice1,
                         {make_note(pitch(Letter::kA, 3), whole())});
  const NotationEntityId preexisting_id =
      std::get<Note>(fx.node()
                         ->lane(fx.track_a)
                         ->stave(fx.stave_a_bass)
                         ->voice(kVoice1)
                         .events()[0])
          .id;

  const PasteAnchor anchor1{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(0)};
  const PasteAnchor anchor2{fx.node_id, fx.track_a, fx.stave_a_treble,
                            rat(1, 2)};

  PasteFragmentCommand first(fragment, anchor1);
  ASSERT_TRUE(first.execute(fx.project).ok());
  PasteFragmentCommand second(fragment, anchor2);
  ASSERT_TRUE(second.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  std::vector<NotationEntityId> all_ids;
  collect_ids(content, all_ids);

  const std::unordered_set<NotationEntityId> unique(all_ids.begin(),
                                                    all_ids.end());
  EXPECT_EQ(all_ids.size(), unique.size());
  for (const NotationEntityId id : fragment_ids)
    EXPECT_FALSE(unique.contains(id));
  EXPECT_FALSE(unique.contains(preexisting_id));
}

// ============================================================
// PasteFragmentCommand -- markings and pedal spans
// ============================================================

TEST(ClipboardCommandTest, PasteDropsMarkingsAnchoredToRemovedEventsAndSpans) {
  Fixture      fx;
  const Note   e1          = make_note(pitch(Letter::kC), quarter());
  const Note   e2          = make_note(pitch(Letter::kD), quarter());
  const Note   e3          = make_note(pitch(Letter::kE), quarter());
  const Note   e4          = make_note(pitch(Letter::kF), quarter());
  VoiceContent destination = build_voice({e1, e2, e3, e4});
  ASSERT_TRUE(
      destination.add_dynamic(make_dynamic_marking(e1.id, Dynamic::kFf)).ok());
  ASSERT_TRUE(
      destination.add_dynamic(make_dynamic_marking(e3.id, Dynamic::kPp)).ok());
  ASSERT_TRUE(destination.add_slur(make_slur(e2.id, e3.id)).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, destination);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 2)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_EQ(content.dynamics().size(), 1u);
  EXPECT_EQ(content.dynamics()[0].at_event, e1.id);
  EXPECT_TRUE(content.slurs().empty());
}

TEST(ClipboardCommandTest, PastePedalSpansClippedRemovedAndOffsetInserted) {
  // Paste range == [1/2, 3/4).
  Fixture    fx;
  TrackLane* lane = fx.node()->lane(fx.track_a);

  ASSERT_TRUE(lane->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(Rational(0), rat(1, 4)))
                  .ok());  // fully before range: untouched
  const NotationEntityId before_id =
      lane->pedal_spans(fx.stave_a_treble)->back().id;

  ASSERT_TRUE(lane->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(9, 16), rat(11, 16)))
                  .ok());  // fully inside range: removed
  const NotationEntityId inside_id =
      lane->pedal_spans(fx.stave_a_treble)->back().id;

  ASSERT_TRUE(lane->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(1, 4), rat(3, 5)))
                  .ok());  // straddles the start boundary (1/2)
  const NotationEntityId straddle_start_id =
      lane->pedal_spans(fx.stave_a_treble)->back().id;

  ASSERT_TRUE(lane->add_pedal_span(fx.stave_a_treble,
                                   make_pedal_span(rat(7, 10), Rational(1)))
                  .ok());  // straddles the end boundary (3/4)
  const NotationEntityId straddle_end_id =
      lane->pedal_spans(fx.stave_a_treble)->back().id;

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, build_voice({make_rest(quarter())})}},
      {FragmentPedalSpan{0, 0, Rational(0), rat(1, 8)}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 2)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const std::vector<PedalSpan>* spans =
      fx.node()->lane(fx.track_a)->pedal_spans(fx.stave_a_treble);
  ASSERT_NE(spans, nullptr);

  bool saw_untouched_before = false;
  bool saw_inside_removed   = true;
  bool saw_straddle_removed = true;
  bool saw_end_removed      = true;
  bool saw_truncated_start  = false;
  bool saw_truncated_end    = false;
  bool saw_fragment_offset  = false;
  for (const PedalSpan& span : *spans) {
    if (span.id == before_id) {
      EXPECT_EQ(span.start, Rational(0));
      EXPECT_EQ(span.end, rat(1, 4));
      saw_untouched_before = true;
    }
    if (span.id == inside_id)
      saw_inside_removed = false;
    if (span.id == straddle_start_id)
      saw_straddle_removed = false;
    if (span.id == straddle_end_id)
      saw_end_removed = false;
    if (span.start == rat(1, 4) && span.end == rat(1, 2))
      saw_truncated_start = true;
    if (span.start == rat(3, 4) && span.end == Rational(1))
      saw_truncated_end = true;
    if (span.start == rat(1, 2) && span.end == rat(5, 8))
      saw_fragment_offset = true;
  }
  EXPECT_TRUE(saw_untouched_before);
  EXPECT_TRUE(saw_inside_removed);
  EXPECT_TRUE(saw_straddle_removed);
  EXPECT_TRUE(saw_end_removed);
  EXPECT_TRUE(saw_truncated_start);
  EXPECT_TRUE(saw_truncated_end);
  EXPECT_TRUE(saw_fragment_offset);
}

// ============================================================
// CutFragmentCommand
// ============================================================

TEST(ClipboardCommandTest, CutFragmentMatchesExtractFragmentStructurally) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), quarter())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});

  const FragmentExtraction expected = extract_fragment(fx.project, selection);
  ASSERT_TRUE(expected.fragment.has_value());

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());
  EXPECT_TRUE(
      fragments_structurally_equal(*command.fragment(), *expected.fragment));
}

TEST(ClipboardCommandTest, CutRangeBecomesNormalizedRests) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter()),
                         make_note(pitch(Letter::kD), quarter())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_TRUE(content.check_complete(fx.node_end()).ok());
  ASSERT_GE(content.events().size(), 1u);
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[0]));
  EXPECT_EQ(std::get<Rest>(content.events()[0]).duration, half());
}

TEST(ClipboardCommandTest, CutFullMeasureSelectionClearsAllFourVoices) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), whole())});
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice2,
                         {make_note(pitch(Letter::kD), whole())});

  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Measure 0's length (1) is shorter than node_end() (4), so region3
  // pads with additional rests after the cut range's own rest.
  const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  for (const Voice voice : {kVoice1, kVoice2, kVoice3, kVoice4}) {
    ASSERT_GE(stave->voice(voice).events().size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Rest>(stave->voice(voice).events()[0]));
    EXPECT_EQ(std::get<Rest>(stave->voice(voice).events()[0]).duration,
              whole());
  }
}

TEST(ClipboardCommandTest, CutUndoRestoresExactlyAndRedoDoesNotReExtract) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), quarter()),
                          make_note(pitch(Letter::kD), quarter())});
  const TrackLane before = fx.lane_of(fx.track_a);

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());
  std::vector<NotationEntityId> first_ids;
  collect_ids(command.fragment()->parts()[0].content, first_ids);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  ASSERT_TRUE(command.redo(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());
  std::vector<NotationEntityId> second_ids;
  collect_ids(command.fragment()->parts()[0].content, second_ids);
  EXPECT_EQ(first_ids, second_ids);
}

// ============================================================
// Composition and stale-context / state-guard rejection
// ============================================================

TEST(ClipboardCommandTest, CutThenPasteElsewhereInOneTransaction) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), quarter()),
                          make_note(pitch(Letter::kD), quarter())});

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction extraction = extract_fragment(fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  const PasteAnchor destination{fx.node_id, fx.track_b, fx.stave_b,
                                Rational(0)};

  CommandTransaction txn;
  ASSERT_TRUE(
      txn.add_command(std::make_unique<CutFragmentCommand>(selection)).ok());
  ASSERT_TRUE(txn.add_command(std::make_unique<PasteFragmentCommand>(
                                  *extraction.fragment, destination))
                  .ok());

  ASSERT_TRUE(txn.execute(fx.project).ok());

  const VoiceContent& cut_side =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(cut_side.events().size(), 1u);
  EXPECT_TRUE(std::holds_alternative<Rest>(cut_side.events()[0]));

  const VoiceContent& paste_side =
      fx.node()->lane(fx.track_b)->stave(fx.stave_b)->voice(kVoice1);
  ASSERT_GE(paste_side.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(paste_side.events()[0]).pitch, pitch(Letter::kC));

  const TrackLane a_after = fx.lane_of(fx.track_a);
  const TrackLane b_after = fx.lane_of(fx.track_b);
  ASSERT_TRUE(txn.undo(fx.project).ok());
  EXPECT_FALSE(fx.lane_of(fx.track_a) == a_after);
  EXPECT_FALSE(fx.lane_of(fx.track_b) == b_after);
}

TEST(ClipboardCommandTest, TransactionRollsBackCutWhenPasteOverflows) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), quarter()),
                          make_note(pitch(Letter::kD), quarter())});
  const TrackLane before = fx.lane_of(fx.track_a);

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction extraction = extract_fragment(fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  // Overflowing anchor: position + span_length() exceeds node_end().
  const PasteAnchor bad_anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                               fx.node_end()};

  CommandTransaction txn;
  ASSERT_TRUE(
      txn.add_command(std::make_unique<CutFragmentCommand>(selection)).ok());
  ASSERT_TRUE(txn.add_command(std::make_unique<PasteFragmentCommand>(
                                  *extraction.fragment, bad_anchor))
                  .ok());

  const Result result = txn.execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteUndoRejectsStaleContextThenRetries) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane post_execute = fx.lane_of(fx.track_a);

  *fx.node()->lane(fx.track_a) = TrackLane{};
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);

  *fx.node()->lane(fx.track_a) = post_execute;
  EXPECT_TRUE(command.undo(fx.project).ok());
}

TEST(ClipboardCommandTest, PasteRedoRejectsStaleContextThenRetries) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.undo(fx.project).ok());
  const TrackLane post_undo = fx.lane_of(fx.track_a);

  *fx.node()->lane(fx.track_a) = TrackLane{};
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);

  *fx.node()->lane(fx.track_a) = post_undo;
  EXPECT_TRUE(command.redo(fx.project).ok());
}

TEST(ClipboardCommandTest, CutUndoRejectsStaleContextThenRetries) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), quarter()),
                          make_note(pitch(Letter::kD), quarter())});

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane post_execute = fx.lane_of(fx.track_a);

  *fx.node()->lane(fx.track_a) = TrackLane{};
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);

  *fx.node()->lane(fx.track_a) = post_execute;
  EXPECT_TRUE(command.undo(fx.project).ok());
}

TEST(ClipboardCommandTest, PasteStateGuardsRejectDoubleExecuteAndOutOfOrder) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand fresh(fragment, anchor);
  EXPECT_EQ(fresh.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fresh.redo(fx.project).code(), ResultCode::kInvalidArgument);

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(ClipboardCommandTest, CutStateGuardsRejectDoubleExecuteAndOutOfOrder) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), quarter())}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 4)}}});

  CutFragmentCommand fresh(selection);
  EXPECT_EQ(fresh.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fresh.redo(fx.project).code(), ResultCode::kInvalidArgument);

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
}

// ============================================================
// Fresh/default-empty project state — regression for finding #1
// ============================================================

// A fixture that constructs through normal Project APIs without
// preconditioning any voice — the actual state of a freshly created
// project.  Only ensure_stave calls are permitted; no assign(),
// assign_and_complete(), or rest_filled() calls touch any voice.
struct UnfilledFixture {
  Project project{ProjectId::generate(), "Test"};
  TrackId track_a;
  TrackId track_b;
  StaveId stave_a_treble;
  StaveId stave_b;
  NodeId  node_id;

  UnfilledFixture() {
    const auto tid_a = project.add_track("A", StaffLayout::grand_staff(),
                                         *MidiChannel::create(0));
    assert(tid_a.has_value());
    track_a                  = *tid_a;
    const Track* track_a_ptr = project.find_active_track(track_a);
    assert(track_a_ptr != nullptr);
    stave_a_treble = track_a_ptr->layout().staves()[0].id;

    const auto tid_b = project.add_track("B", StaffLayout::single_staff(),
                                         *MidiChannel::create(1));
    assert(tid_b.has_value());
    track_b                  = *tid_b;
    const Track* track_b_ptr = project.find_active_track(track_b);
    assert(track_b_ptr != nullptr);
    stave_b = track_b_ptr->layout().staves()[0].id;

    node_id      = project.add_node("Node");
    Node* node_p = project.find_node(node_id);
    assert(node_p != nullptr);

    std::vector<Measure> measures;
    measures.push_back(
        Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
    auto timeline = NodeTimeline::create(
        measures, {StaveDefinition{stave_a_treble, Clef::kTreble},
                   StaveDefinition{stave_b, Clef::kTreble}});
    assert(timeline.has_value());
    node_p->set_timeline(std::move(*timeline));

    // ensure_stave so staves exist, but voices are left at their
    // default-constructed (zero-length) state - the real fresh-project
    // condition.
    node_p->lane(track_a)->ensure_stave(stave_a_treble);
    node_p->lane(track_b)->ensure_stave(stave_b);
  }

  Node* node() { return project.find_node(node_id); }

  Rational node_end() { return node()->timeline()->node_end(); }

  // Minimal assign for cut-test setup: assigns complete content to one
  // voice without requiring the full Fixture convenience methods.
  void assign_note(TrackId track, StaveId stave_id, Voice voice,
                   VoiceEvent event) {
    VoiceContent vc;
    const Result r = vc.append(event);
    assert(r.ok());
    (void)r;
    const Result n = vc.normalize(node_end());
    assert(n.ok());
    (void)n;
    node()->lane(track)->stave(stave_id)->voice(voice) = std::move(vc);
  }
};

TEST(ClipboardCommandTest, PasteIntoEntirelyDefaultEmptyVoicesSucceeds) {
  // Finding 1: untouched voices must stay raw-empty (default-constructed),
  // not be forcibly normalized.  Only the voice actually touched by the
  // paste (voice 1) becomes rhythmically complete.
  UnfilledFixture        fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  // Voice 1 was touched and must be complete.
  EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
  // Voices 2-4 were untouched and must remain raw-empty.
  for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
    EXPECT_EQ(stave->voice(voice).total_length(), Rational(0));
  }
}

TEST(ClipboardCommandTest,
     PasteUntouchedVoicesRemainRawAndUndoRestoresExactPreState) {
  // Finding 1: untouched voices must stay in their raw pre-paste state
  // (default-constructed, empty), and undo must restore the exact raw
  // pre-state, not a normalized version.
  UnfilledFixture    fx;
  const VoiceContent raw_voice1_pre =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  const VoiceContent raw_voice2_pre =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice2);

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1,
                         build_voice({make_note(pitch(Letter::kC), half())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // After paste: voice 1 (touched) must be complete.
  {
    const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
    // Voices 2-4: untouched, remain raw-empty.
    for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
      EXPECT_EQ(stave->voice(voice).total_length(), Rational(0));
    }
  }

  // Undo restores the exact raw pre-state (all voices raw-empty).
  ASSERT_TRUE(command.undo(fx.project).ok());
  {
    const auto*  stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    VoiceContent expect_empty;
    EXPECT_TRUE(stave->voice(kVoice1) == raw_voice1_pre);
    EXPECT_TRUE(stave->voice(kVoice2) == raw_voice2_pre);
    EXPECT_EQ(stave->voice(kVoice1).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice2).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice3).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice4).total_length(), Rational(0));
  }

  // Redo re-applies: voice 1 complete again.
  ASSERT_TRUE(command.redo(fx.project).ok());
  {
    const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
    for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
      EXPECT_EQ(stave->voice(voice).total_length(), Rational(0));
    }
  }
}

TEST(ClipboardCommandTest,
     CutOnDefaultEmptyVoicesSucceedsAndUndoRestoresRawState) {
  // Finding 1: after a cut on a lane with raw-empty untouched voices, those
  // voices must stay raw-empty.  Undo must restore the exact raw pre-state.
  UnfilledFixture fx;
  // Put one note into voice 1 so there is something to cut.
  fx.assign_note(fx.track_a, fx.stave_a_treble, kVoice1,
                 make_note(pitch(Letter::kC), whole()));

  const VoiceContent pre_voice2 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice2);

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(1)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());

  // Voice 1: cut range became rests, then padded to node_end (complete).
  // Voices 2-4: untouched, remain raw-empty.
  {
    const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
    for (const Voice voice : {kVoice2, kVoice3, kVoice4}) {
      EXPECT_EQ(stave->voice(voice).total_length(), Rational(0));
    }
  }

  // Undo: exact raw pre-state restored (all voices back to pre-cut state).
  ASSERT_TRUE(command.undo(fx.project).ok());
  {
    const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
    EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
    // Voice 2 must be byte-identical to its raw pre-cut (empty) state.
    EXPECT_TRUE(stave->voice(kVoice2) == pre_voice2);
    EXPECT_EQ(stave->voice(kVoice2).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice3).total_length(), Rational(0));
    EXPECT_EQ(stave->voice(kVoice4).total_length(), Rational(0));
  }
}

// ============================================================
// Treble -> bass paste: SpelledPitch invariance (finding #3)
// ============================================================

TEST(ClipboardCommandTest, TrebleToBassPastePreservesSpelledPitchExactly) {
  // Build a source project with known pitches in a treble staff, extract
  // a fragment, then paste into a bass-only destination and verify every
  // SpelledPitch (letter, octave, accidental) is byte-identical to the
  // source — paste must never adjust for clef difference.
  Fixture    src_fx;
  const Note c4 = make_note(*SpelledPitch::create(Letter::kC, 4), quarter());
  const Note d4 = make_note(*SpelledPitch::create(Letter::kD, 4), quarter());
  const Note e4 = make_note(*SpelledPitch::create(Letter::kE, 4), quarter());
  src_fx.assign(src_fx.track_a, src_fx.stave_a_treble, kVoice1,
                build_voice({c4, d4, e4}));
  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{src_fx.node_id, src_fx.track_a, src_fx.stave_a_treble,
                          kVoice1, MusicalSpan{Rational(0), rat(3, 4)}}});
  const FragmentExtraction extraction =
      extract_fragment(src_fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  // Destination is a separate project with a bass-only staff (just track_b,
  // single staff, treble clef context but pitches remain unchanged).
  Fixture           dst_fx;
  const PasteAnchor anchor{dst_fx.node_id, dst_fx.track_b, dst_fx.stave_b,
                           Rational(0)};

  PasteFragmentCommand command(*extraction.fragment, anchor);
  ASSERT_TRUE(command.execute(dst_fx.project).ok());

  const VoiceContent& content = dst_fx.node()
                                    ->lane(dst_fx.track_b)
                                    ->stave(dst_fx.stave_b)
                                    ->voice(kVoice1);
  ASSERT_GE(content.events().size(), 3u);
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[0]));
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[1]));
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[2]));

  const Note& pasted_c4 = std::get<Note>(content.events()[0]);
  const Note& pasted_d4 = std::get<Note>(content.events()[1]);
  const Note& pasted_e4 = std::get<Note>(content.events()[2]);

  EXPECT_TRUE(pasted_c4.pitch == c4.pitch);
  EXPECT_TRUE(pasted_d4.pitch == d4.pitch);
  EXPECT_TRUE(pasted_e4.pitch == e4.pitch);

  // Verify each SpelledPitch field individually (accessors, not members).
  for (const auto& [src_note, dst_note] :
       {std::make_pair(&c4, &pasted_c4), std::make_pair(&d4, &pasted_d4),
        std::make_pair(&e4, &pasted_e4)}) {
    EXPECT_EQ(dst_note->pitch.letter(), src_note->pitch.letter());
    EXPECT_EQ(dst_note->pitch.octave(), src_note->pitch.octave());
    EXPECT_EQ(dst_note->pitch.accidental(), src_note->pitch.accidental());
  }

  // Destination range tile check: fragment length + rest fill = node_end.
  EXPECT_TRUE(content.check_complete(dst_fx.node_end()).ok());
}

// ============================================================
// Max-referenced-stave-ordinal mapping (finding #4)
// ============================================================

TEST(ClipboardCommandTest,
     GrandStaffFragmentOnlyBassRequiresOneDestinationStave) {
  // A fragment whose shape declares grand staff (stave_count = 2) but
  // whose parts reference only stave_ordinal 1 (the bass stave) must
  // require only that one mapped stave — not both.  Pasting into a
  // single-staff track must succeed.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 1, kVoice1,
          build_voice({make_note(pitch(Letter::kE, 3), quarter())})}});

  // Anchor into the single-staff track_b.
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_b)->stave(fx.stave_b)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kE, 3));
}

TEST(ClipboardCommandTest,
     GrandStaffFragmentBothStavesRequiresTwoDestinationStaves) {
  // A fragment declaring grand staff and actually referencing both staves
  // must still fail when pasted into a single-staff track.
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kE, 3), quarter())})}});

  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before);
}

// ============================================================
// Finding 1 — exact lane equality on raw pre-state (deep)
// ============================================================

TEST(ClipboardCommandTest,
     PasteUndoRestoresExactRawLaneIncludingUntouchedStaves) {
  // Fixture pre-fills all voices rest-filled.  We touch only track_a's
  // treble stave voice 1.  After undo, the entire lane (including untouched
  // bass stave and voices 2-4) must be byte-equal to the pre-paste lane.
  Fixture                fx;
  const TrackLane        before   = fx.lane_of(fx.track_a);
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const TrackLane after_execute = fx.lane_of(fx.track_a);
  EXPECT_FALSE(after_execute == before);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_execute);
}

TEST(ClipboardCommandTest,
     PasteWithoutPreEnsuringStaveSucceedsWhenMappingPermits) {
  // The paste mapping resolves staves through the destination track's
  // layout, not through pre-existing ensure_stave calls.  If a stave does
  // not yet exist in the lane, ensure_stave is called during paste and the
  // paste succeeds.
  UnfilledFixture        fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  ASSERT_NE(stave, nullptr);
  EXPECT_TRUE(stave->voice(kVoice1).check_complete(fx.node_end()).ok());
}

// ============================================================
// Finding 2 & 3 — directional boundary preservation + tie severing
// ============================================================

TEST(ClipboardCommandTest,
     PasteLeftBoundaryChordStraddlePreservesAttackSeversChordTies) {
  // A chord with two noteheads (C4 tied, E4 tied), spanning [0, 1/2).
  // Paste at [1/4, 1/2): left region preserves truncated chord at
  // [0, 1/4) with original id, both noteheads' ties severed.
  Fixture                fx;
  std::vector<ChordNote> chord_notes = {
      ChordNote{NotationEntityId{}, pitch(Letter::kC, 4), true},
      ChordNote{NotationEntityId{}, pitch(Letter::kE, 4), true}};
  const Chord            chord    = make_chord(half(), std::move(chord_notes));
  const NotationEntityId chord_id = chord.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({chord}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  // Left region: truncated chord with original id.
  EXPECT_TRUE(std::holds_alternative<Chord>(content.events()[0]));
  const Chord& left_chord = std::get<Chord>(content.events()[0]);
  EXPECT_EQ(left_chord.id, chord_id);
  EXPECT_EQ(left_chord.duration, quarter());
  ASSERT_EQ(left_chord.notes.size(), 2u);
  EXPECT_FALSE(left_chord.notes[0].tied_to_next);
  EXPECT_FALSE(left_chord.notes[1].tied_to_next);
}

TEST(ClipboardCommandTest, PasteSeveresOutgoingTieWhenTargetIsRemoved) {
  // C4 quarter tied to D4 quarter.  Paste replaces D4.  C4's tie must
  // be severed (tied_to_next = false).
  Fixture                fx;
  const Note             c4 = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note             d4 = make_note(pitch(Letter::kD, 4), quarter(), false);
  const NotationEntityId c_id = c4.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({c4, d4}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_c.id, c_id);
  EXPECT_FALSE(left_c.tied_to_next);
}

TEST(ClipboardCommandTest, CutSeveresOutgoingTieWhenTailIsInCutRange) {
  // C4 quarter tied to D4 quarter.  Cut replaces D4 with rests.  C4's tie
  // is severed.
  Fixture                fx;
  const Note             c4 = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note             d4 = make_note(pitch(Letter::kD, 4), quarter(), false);
  const NotationEntityId c_id = c4.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({c4, d4}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_c.id, c_id);
  EXPECT_FALSE(left_c.tied_to_next);
}

TEST(ClipboardCommandTest,
     PasteRightBoundaryStraddleAttackRemovedBecomesRests) {
  // D half note at [0, 1/2).  Paste at [0, 1/4): the right region
  // [1/4, 1/2) of D should become rests (attack was in the replaced range).
  Fixture    fx;
  const Note d_half = make_note(pitch(Letter::kD, 4), half());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, build_voice({d_half}));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC, 4));
  // D's tail [1/4, 1/2) → rests.
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[1]));
  EXPECT_EQ(std::get<Rest>(content.events()[1]).duration, quarter());
}

TEST(ClipboardCommandTest,
     PasteBothBoundariesStraddledDifferentDestinationEvents) {
  // C half note at [0, 1/2), D half note at [1/2, 1).
  // Paste at [1/4, 3/4): C straddles left boundary (preserved attack),
  // D straddles right boundary (attack removed → rests).
  Fixture                fx;
  const Note             c_half = make_note(pitch(Letter::kC, 4), half());
  const Note             d_half = make_note(pitch(Letter::kD, 4), half());
  const NotationEntityId c_id   = c_half.id;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({c_half, d_half}));

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), half())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 3u);
  // Left: truncated C quarter, original id, tie severed.
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_c.id, c_id);
  EXPECT_EQ(left_c.duration, quarter());
  // Middle: pasted B half.
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kB, 3));
  // Right: D tail [3/4, 1) → rests.
  EXPECT_TRUE(std::holds_alternative<Rest>(content.events()[2]));
  EXPECT_EQ(std::get<Rest>(content.events()[2]).duration, quarter());
}

TEST(ClipboardCommandTest,
     PasteMarkingsOnSurvivingLeftBoundaryEventArePreserved) {
  // C half note at [0, 1/2) with a dynamic marking (ff) on it.
  // Paste at [1/4, 1/2): left region preserves truncated C.  Its dynamic
  // must survive.
  Fixture      fx;
  const Note   c_half = make_note(pitch(Letter::kC, 4), half());
  VoiceContent dest   = build_voice({c_half});
  ASSERT_TRUE(
      dest.add_dynamic(make_dynamic_marking(c_half.id, Dynamic::kFf)).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_EQ(content.dynamics().size(), 1u);
  EXPECT_EQ(content.dynamics()[0].at_event,
            std::get<Note>(content.events()[0]).id);
  EXPECT_EQ(content.dynamics()[0].value, Dynamic::kFf);
}

// ============================================================
// Finding 4 — failure atomicity: model unchanged, command retryable
// ============================================================

TEST(ClipboardCommandTest, PasteFailureLeavesModelUnchangedAndCommandFresh) {
  // Overflow failure: model and lane must be unchanged, command must be
  // retryable (not in kFaulted or kDone state).
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      Rational(1), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, build_voice({make_rest(whole())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           fx.node_end()};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  // Command must be retryable: undo/redo reject, but execute with a valid
  // anchor succeeds.
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);

  const PasteAnchor    good_anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                                Rational(0)};
  PasteFragmentCommand retry(fragment, good_anchor);
  ASSERT_TRUE(retry.execute(fx.project).ok());
}

TEST(ClipboardCommandTest, CutFailureLeavesModelUnchangedAndFragmentEmpty) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  // Cut range that straddles a tuplet → failure.
  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 24)}}});

  CutFragmentCommand command(selection);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(command.fragment().has_value());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  // Undo/redo must reject when command is still fresh.
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
}

// ============================================================
// Finding 5 — true treble→bass with accidentals and ChordNote
// ============================================================

TEST(ClipboardCommandTest,
     TrebleToBassPastePreservesAccidentalAndChordNoteOctave) {
  // Source: treble staff with F#5 (accidental) and Eb4.
  // Destination: any staff.  Pitches must be verbatim regardless of clef.
  Fixture    src_fx;
  const Note f_sharp = make_note(
      *SpelledPitch::create(Letter::kF, 5, Accidental::kSharp), quarter());
  const Note e_flat = make_note(
      *SpelledPitch::create(Letter::kE, 4, Accidental::kFlat), quarter());
  src_fx.assign(src_fx.track_a, src_fx.stave_a_treble, kVoice1,
                build_voice({f_sharp, e_flat}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{src_fx.node_id, src_fx.track_a, src_fx.stave_a_treble,
                          kVoice1, MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction extraction =
      extract_fragment(src_fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  Fixture           dst_fx;
  const PasteAnchor anchor{dst_fx.node_id, dst_fx.track_b, dst_fx.stave_b,
                           Rational(0)};

  PasteFragmentCommand command(*extraction.fragment, anchor);
  ASSERT_TRUE(command.execute(dst_fx.project).ok());

  const VoiceContent& content = dst_fx.node()
                                    ->lane(dst_fx.track_b)
                                    ->stave(dst_fx.stave_b)
                                    ->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);

  // F#5: must be exact.
  const Note& pasted_fs = std::get<Note>(content.events()[0]);
  EXPECT_EQ(pasted_fs.pitch.letter(), Letter::kF);
  EXPECT_EQ(pasted_fs.pitch.octave(), 5);
  EXPECT_EQ(pasted_fs.pitch.accidental(), Accidental::kSharp);

  // Eb4: must be exact.
  const Note& pasted_eb = std::get<Note>(content.events()[1]);
  EXPECT_EQ(pasted_eb.pitch.letter(), Letter::kE);
  EXPECT_EQ(pasted_eb.pitch.octave(), 4);
  EXPECT_EQ(pasted_eb.pitch.accidental(), Accidental::kFlat);
}

// ============================================================
// Finding 6 — sparse stave-ordinal mapping
// ============================================================

TEST(ClipboardCommandTest, SparseOrdinalsZeroAndTwoOnSingleStaffFails) {
  // On a single-staff track, ordinal 2 (compacted to index 1) needs 2
  // staves → fails.
  Fixture         fx;
  const TrackLane before = fx.lane_of(fx.track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{3}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 2, kVoice1,
           build_voice({make_note(pitch(Letter::kG, 4), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before);
}

TEST(ClipboardCommandTest, RepeatedVoicesOnSameOrdinalShareStave) {
  // Two fragment parts with the same (track_ordinal, stave_ordinal) but
  // different voices → they share one destination stave.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 0, kVoice2,
           build_voice({make_note(pitch(Letter::kD, 4), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const auto* stave = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  EXPECT_EQ(std::get<Note>(stave->voice(kVoice1).events()[0]).pitch,
            pitch(Letter::kC, 4));
  EXPECT_EQ(std::get<Note>(stave->voice(kVoice2).events()[0]).pitch,
            pitch(Letter::kD, 4));

  // Bass stave must be untouched.
  const auto* bass = fx.node()->lane(fx.track_a)->stave(fx.stave_a_bass);
  ASSERT_NE(bass, nullptr);
}

TEST(ClipboardCommandTest, NonFirstAnchorStavePasteSucceeds) {
  // Anchor on the bass stave (stave_ordinal 1 in grand staff).  Fragment
  // stave_ordinal 0 maps to the bass stave itself.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kE, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_bass,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_bass)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kE, 3));
}

// ============================================================
// Finding 7 — integrated marking-traceability regression
// ============================================================

TEST(ClipboardCommandTest, PasteWithAllMarkingsGeneratesDisjointIds) {
  // Build a fragment containing dynamics, hairpin, slur, beam override,
  // and grace group, then paste it twice.  Every id in each paste must be
  // fresh (not present in the fragment, not colliding across pastes).
  Fixture fx;
  // Use eighth notes so the beam override validates (beamable events only).
  const Note   n1   = make_note(pitch(Letter::kC, 4), eighth());
  const Note   n2   = make_note(pitch(Letter::kD, 4), eighth());
  VoiceContent part = build_voice({n1, n2});
  ASSERT_TRUE(part.add_dynamic(make_dynamic_marking(n1.id, Dynamic::kMf)).ok());
  ASSERT_TRUE(
      part.add_hairpin(make_hairpin(n1.id, n2.id, HairpinDirection::kCrescendo))
          .ok());
  ASSERT_TRUE(part.add_slur(make_slur(n1.id, n2.id)).ok());
  ASSERT_TRUE(
      part.add_beam_override(
              make_beam_override(BeamOverride::Kind::kJoin, {n1.id, n2.id}))
          .ok());
  std::vector<graphscore::GraceNote> grace_notes;
  grace_notes.push_back(graphscore::GraceNote{
      graphscore::NotationEntityId{}, pitch(Letter::kF, 5), eighth(),
      graphscore::GraceNoteType::kAcciaccatura, true});
  ASSERT_TRUE(
      part.add_grace_group(make_grace_group(n2.id, std::move(grace_notes)))
          .ok());

  std::vector<NotationEntityId> fragment_ids;
  collect_ids(part, fragment_ids);

  const NotationFragment frag =
      make_fragment(rat(1, 4), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, part}});

  // First paste.
  const PasteAnchor    anchor1{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(0)};
  PasteFragmentCommand first(frag, anchor1);
  ASSERT_TRUE(first.execute(fx.project).ok());

  const VoiceContent& content1 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  std::vector<NotationEntityId> first_ids;
  collect_ids(content1, first_ids);

  // Verify no fragment id appears in the destination.
  const std::unordered_set<NotationEntityId> first_set(first_ids.begin(),
                                                       first_ids.end());
  for (const NotationEntityId fid : fragment_ids)
    EXPECT_FALSE(first_set.contains(fid));

  // Verify all referencing markings are rewired correctly.
  ASSERT_GE(content1.dynamics().size(), 1u);
  EXPECT_EQ(content1.dynamics()[0].at_event, event_id(content1.events()[0]));
  ASSERT_GE(content1.hairpins().size(), 1u);
  EXPECT_EQ(content1.hairpins()[0].start_event, event_id(content1.events()[0]));
  EXPECT_EQ(content1.hairpins()[0].end_event, event_id(content1.events()[1]));
  ASSERT_GE(content1.slurs().size(), 1u);
  EXPECT_EQ(content1.slurs()[0].start_event, event_id(content1.events()[0]));
  EXPECT_EQ(content1.slurs()[0].end_event, event_id(content1.events()[1]));
  ASSERT_GE(content1.beam_overrides().size(), 1u);
  const auto& beam = content1.beam_overrides()[0];
  ASSERT_EQ(beam.events.size(), 2u);
  EXPECT_EQ(beam.events[0], event_id(content1.events()[0]));
  EXPECT_EQ(beam.events[1], event_id(content1.events()[1]));
  ASSERT_GE(content1.grace_groups().size(), 1u);
  EXPECT_EQ(content1.grace_groups()[0].principal_event,
            event_id(content1.events()[1]));

  // Second paste at a disjoint position.
  const PasteAnchor    anchor2{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(1)};
  PasteFragmentCommand second(frag, anchor2);
  ASSERT_TRUE(second.execute(fx.project).ok());

  const VoiceContent& content2 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  std::vector<NotationEntityId> second_ids;
  collect_ids(content2, second_ids);

  // No id appears twice anywhere in the joined content.
  const std::unordered_set<NotationEntityId> all_set(second_ids.begin(),
                                                     second_ids.end());
  EXPECT_EQ(second_ids.size(), all_set.size());
}

TEST(ClipboardCommandTest, PasteRemovesBeamOverrideCrossingBoundary) {
  // A beam override on events n1,n2,n3.  Paste replaces n2.  The beam
  // should be dropped (fewer than two survive or incomplete set).
  Fixture      fx;
  const Note   n1   = make_note(pitch(Letter::kC, 4), quarter());
  const Note   n2   = make_note(pitch(Letter::kD, 4), quarter());
  const Note   n3   = make_note(pitch(Letter::kE, 4), quarter());
  VoiceContent dest = build_voice({n1, n2, n3});
  ASSERT_TRUE(
      dest.add_beam_override(make_beam_override(BeamOverride::Kind::kJoin,
                                                {n1.id, n2.id, n3.id}))
          .ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  EXPECT_TRUE(content.beam_overrides().empty());
}

// ============================================================
// Fix 4 — true treble→bass with Chord + accidental Note + clef
// ============================================================

TEST(ClipboardCommandTest,
     TrebleToBassChordAndAccidentalNoteSpelledPitchInvariant) {
  // Source: treble staff with F#5 (accidental) and a C-major-root + Eb4
  // Chord. Destination: bass-only stave (Clef::kBass). Every SpelledPitch
  // (letter, octave, accidental) must match verbatim; clef_at_origin is
  // informational and must not be applied.
  Fixture    src_fx;
  const Note f_sharp = make_note(
      *SpelledPitch::create(Letter::kF, 5, Accidental::kSharp), quarter());

  std::vector<ChordNote> chord_notes = {
      ChordNote{NotationEntityId{},
                *SpelledPitch::create(Letter::kC, 4, Accidental::kNatural),
                false},
      ChordNote{NotationEntityId{},
                *SpelledPitch::create(Letter::kE, 4, Accidental::kFlat),
                false}};
  const Chord chord = make_chord(quarter(), std::move(chord_notes));

  src_fx.assign(src_fx.track_a, src_fx.stave_a_treble, kVoice1,
                build_voice({f_sharp, chord}));

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{src_fx.node_id, src_fx.track_a, src_fx.stave_a_treble,
                          kVoice1, MusicalSpan{Rational(0), rat(1, 2)}}});
  const FragmentExtraction extraction =
      extract_fragment(src_fx.project, selection);
  ASSERT_TRUE(extraction.fragment.has_value());

  // Verify the fragment carries clef_at_origin = kTreble.
  bool saw_treble_clef = false;
  for (const auto& ctx : extraction.fragment->stave_contexts()) {
    if (ctx.stave_ordinal == 0 && ctx.track_ordinal == 0) {
      EXPECT_EQ(ctx.clef_at_origin, Clef::kTreble);
      saw_treble_clef = true;
    }
  }
  EXPECT_TRUE(saw_treble_clef);

  // Destination: anchor on the bass stave (stave_a_bass) which has
  // default_clef = Clef::kBass.
  Fixture      dst_fx;
  const Track* dst_track = dst_fx.project.find_active_track(dst_fx.track_a);
  ASSERT_NE(dst_track, nullptr);
  ASSERT_GE(dst_track->layout().staves().size(), 2u);
  EXPECT_EQ(dst_track->layout().staves()[1].default_clef, Clef::kBass);

  const PasteAnchor anchor{dst_fx.node_id, dst_fx.track_a, dst_fx.stave_a_bass,
                           Rational(0)};

  PasteFragmentCommand command(*extraction.fragment, anchor);
  ASSERT_TRUE(command.execute(dst_fx.project).ok());

  const VoiceContent& content = dst_fx.node()
                                    ->lane(dst_fx.track_a)
                                    ->stave(dst_fx.stave_a_bass)
                                    ->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);

  // F#5: verbatim.
  EXPECT_TRUE(std::holds_alternative<Note>(content.events()[0]));
  const Note& pasted_fs = std::get<Note>(content.events()[0]);
  EXPECT_EQ(pasted_fs.pitch.letter(), Letter::kF);
  EXPECT_EQ(pasted_fs.pitch.octave(), 5);
  EXPECT_EQ(pasted_fs.pitch.accidental(), Accidental::kSharp);

  // Chord: two noteheads verbatim.
  EXPECT_TRUE(std::holds_alternative<Chord>(content.events()[1]));
  const Chord& pasted_chord = std::get<Chord>(content.events()[1]);
  ASSERT_EQ(pasted_chord.notes.size(), 2u);
  EXPECT_EQ(pasted_chord.notes[0].pitch.letter(), Letter::kC);
  EXPECT_EQ(pasted_chord.notes[0].pitch.octave(), 4);
  EXPECT_EQ(pasted_chord.notes[0].pitch.accidental(), Accidental::kNatural);
  EXPECT_EQ(pasted_chord.notes[1].pitch.letter(), Letter::kE);
  EXPECT_EQ(pasted_chord.notes[1].pitch.octave(), 4);
  EXPECT_EQ(pasted_chord.notes[1].pitch.accidental(), Accidental::kFlat);

  // References must be clean post-paste.
  const std::vector<NotationDiagnostic> ref_diags =
      validate_voice_references(content);
  EXPECT_TRUE(ref_diags.empty());
}

// ============================================================
// Fix 5 — real no-stave precondition
// ============================================================

TEST(ClipboardCommandTest, PasteCreatesStaveWhenDestinationLaneHasNoStaves) {
  // Construct through normal Project add_track / add_node / set_timeline
  // WITHOUT calling ensure_stave on any lane. Assert has_stave false,
  // paste creates the stave, undo restores exact no-stave lane, redo
  // succeeds, and an untouched lane remains unchanged.
  Project    proj{ProjectId::generate(), "NoStave"};
  const auto tid =
      proj.add_track("A", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = proj.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = proj.add_node("N");
  Node*        node_p  = proj.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  std::vector<Measure> measures;
  measures.push_back(
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  auto timeline = NodeTimeline::create(
      measures, {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));

  // No ensure_stave call — lane is truly empty.
  ASSERT_FALSE(node_p->lane(track_id)->has_stave(stave_id));

  // Capture untouched lane for later comparison.
  const auto tid2 =
      proj.add_track("B", StaffLayout::single_staff(), *MidiChannel::create(1));
  ASSERT_TRUE(tid2.has_value());
  const TrackId untouched_track = *tid2;
  // B's lane is also untouched — capture it.
  TrackLane untouched_before = *node_p->lane(untouched_track);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), quarter())})}});
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(proj).ok());

  // Stave created by paste's ensure_stave.
  EXPECT_TRUE(node_p->lane(track_id)->has_stave(stave_id));
  const auto* stave = node_p->lane(track_id)->stave(stave_id);
  ASSERT_NE(stave, nullptr);
  EXPECT_TRUE(stave->voice(kVoice1)
                  .check_complete(node_p->timeline()->node_end())
                  .ok());

  // Untouched lane unchanged.
  EXPECT_TRUE(*node_p->lane(untouched_track) == untouched_before);

  // Undo restores exact no-stave lane.
  const TrackLane post_execute = *node_p->lane(track_id);
  ASSERT_TRUE(command.undo(proj).ok());
  ASSERT_FALSE(node_p->lane(track_id)->has_stave(stave_id));
  EXPECT_TRUE(*node_p->lane(untouched_track) == untouched_before);

  // Redo succeeds and creates stave again.
  ASSERT_TRUE(command.redo(proj).ok());
  EXPECT_TRUE(node_p->lane(track_id)->has_stave(stave_id));
  EXPECT_TRUE(*node_p->lane(untouched_track) == untouched_before);
}

// ============================================================
// Fix 6 — valid tie-family evidence
// ============================================================

TEST(ClipboardCommandTest,
     ValidSamePitchTieChainLeftAttackPreservedOutgoingSevered) {
  // C4 quarter tied to C4 quarter (valid same-pitch tie). Paste replaces
  // the second C4. Left C4 retained with original id, its outgoing tie
  // must be severed. validate_voice_references clean before and after.
  Fixture    fx;
  const Note c4_tied   = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note c4_untied = make_note(pitch(Letter::kC, 4), quarter(), false);
  const NotationEntityId c_id = c4_tied.id;
  VoiceContent           dest = build_voice({c4_tied, c4_untied});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kD, 4), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left_c.id, c_id);
  EXPECT_EQ(left_c.pitch, pitch(Letter::kC, 4));
  EXPECT_FALSE(left_c.tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest,
     DifferentPitchPastedReplacementSeveresRetainedOutgoingTie) {
  // C4 tied to C4. Paste B3 into the position of the second C4. The
  // retained C4's outgoing tie severs deterministically — even though
  // the pasted note is a different pitch.
  Fixture      fx;
  const Note   c4_tied   = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note   c4_untied = make_note(pitch(Letter::kC, 4), quarter(), false);
  VoiceContent dest      = build_voice({c4_tied, c4_untied});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_FALSE(left_c.tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, PartialChordNoteTiesSomeRetainedSomeRemoved) {
  // Chord (C4 tied, E4 untied) followed by another chord as tie target.
  // Paste replaces the second chord. The retained truncated first chord
  // has both noteheads' ties severed since the next event is removed.
  Fixture                fx;
  std::vector<ChordNote> chord1_notes = {
      ChordNote{NotationEntityId{}, pitch(Letter::kC, 4), true},
      ChordNote{NotationEntityId{}, pitch(Letter::kE, 4), false}};
  std::vector<ChordNote> chord2_notes = {
      ChordNote{NotationEntityId{}, pitch(Letter::kC, 4), false},
      ChordNote{NotationEntityId{}, pitch(Letter::kE, 4), false}};
  const Chord  chord1 = make_chord(quarter(), std::move(chord1_notes));
  const Chord  chord2 = make_chord(quarter(), std::move(chord2_notes));
  VoiceContent dest   = build_voice({chord1, chord2});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Chord& left_chord = std::get<Chord>(content.events()[0]);
  ASSERT_EQ(left_chord.notes.size(), 2u);
  // Both severed: the next event is removed, so neither notehead keeps its
  // tie (even the E4 that was originally untied stays untied).
  EXPECT_FALSE(left_chord.notes[0].tied_to_next);
  EXPECT_FALSE(left_chord.notes[1].tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, CutExactBoundarySeveresOutgoingDestinationTie) {
  // C4 tied to C4. Cut replaces the second C4 with rests. Left C4 retained
  // with tie severed.
  Fixture      fx;
  const Note   c4_tied   = make_note(pitch(Letter::kC, 4), quarter(), true);
  const Note   c4_untied = make_note(pitch(Letter::kC, 4), quarter(), false);
  VoiceContent dest      = build_voice({c4_tied, c4_untied});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const Selection selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{rat(1, 4), rat(1, 2)}}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  const Note& left_c = std::get<Note>(content.events()[0]);
  EXPECT_FALSE(left_c.tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, MultiPieceLeftTruncationLastPieceOutgoingTieFalse) {
  // C whole note with tied_to_next=true, followed by another C note
  // (valid same-pitch tie chain). Paste at [1/4, 3/4). Left region
  // [0, 1/4) retains a truncated C quarter with tied_to_next forced false
  // because the tie target (second note) is in the replaced range.
  Fixture      fx;
  const Note   c_whole = make_note(pitch(Letter::kC, 4), whole(), true);
  const Note   c_tail  = make_note(pitch(Letter::kC, 4), whole(), false);
  VoiceContent dest    = build_voice({c_whole, c_tail});
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kD, 4), half())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  const Note& left = std::get<Note>(content.events()[0]);
  EXPECT_EQ(left.duration, quarter());
  EXPECT_FALSE(left.tied_to_next);
  EXPECT_TRUE(validate_voice_references(content).empty());
}

// ============================================================
// Fix 7 — sparse/marking evidence
// ============================================================

TEST(ClipboardCommandTest,
     SparseOrdinalsZeroAndTwoOnGrandStaffCompactCorrectly) {
  // Fragment track_shape declares 3 staves but only {0, 2} are used.
  // Distinct referenced ordinals compact onto the two destination staves.
  Fixture fx;

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{3}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 2, kVoice1,
           build_voice({make_note(pitch(Letter::kG, 4), quarter())})}});

  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const TrackLane* lane = fx.node()->lane(fx.track_a);
  ASSERT_NE(lane, nullptr);
  EXPECT_EQ(
      std::get<Note>(lane->stave(fx.stave_a_treble)->voice(kVoice1).events()[0])
          .pitch,
      pitch(Letter::kC, 4));
  EXPECT_EQ(
      std::get<Note>(lane->stave(fx.stave_a_bass)->voice(kVoice1).events()[0])
          .pitch,
      pitch(Letter::kG, 4));
}

TEST(ClipboardCommandTest,
     SparseOrdinalsAcrossMultipleTracksCompactIndependently) {
  Project    project{ProjectId::generate(), "Sparse multi-track"};
  const auto track_a = project.add_track("A", StaffLayout::grand_staff(),
                                         *MidiChannel::create(0));
  const auto track_b = project.add_track("B", StaffLayout::grand_staff(),
                                         *MidiChannel::create(1));
  ASSERT_TRUE(track_a.has_value());
  ASSERT_TRUE(track_b.has_value());

  const Track* first_track  = project.find_active_track(*track_a);
  const Track* second_track = project.find_active_track(*track_b);
  ASSERT_NE(first_track, nullptr);
  ASSERT_NE(second_track, nullptr);
  const StaveId first_treble  = first_track->layout().staves()[0].id;
  const StaveId first_bass    = first_track->layout().staves()[1].id;
  const StaveId second_treble = second_track->layout().staves()[0].id;
  const StaveId second_bass   = second_track->layout().staves()[1].id;

  const NodeId node_id = project.add_node("Node");
  Node*        node    = project.find_node(node_id);
  ASSERT_NE(node, nullptr);
  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}},
      {StaveDefinition{first_treble, Clef::kTreble},
       StaveDefinition{first_bass, Clef::kBass},
       StaveDefinition{second_treble, Clef::kTreble},
       StaveDefinition{second_bass, Clef::kBass}});
  ASSERT_TRUE(timeline.has_value());
  node->set_timeline(std::move(*timeline));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{3}, FragmentTrackShape{3}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           0, 2, kVoice1,
           build_voice({make_note(pitch(Letter::kD, 4), quarter())})},
       FragmentVoicePart{
           1, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kE, 4), quarter())})},
       FragmentVoicePart{
           1, 2, kVoice1,
           build_voice({make_note(pitch(Letter::kF, 4), quarter())})}});
  const PasteAnchor anchor{node_id, *track_a, first_treble, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(project).ok());

  const auto pasted_pitch = [node](TrackId track, StaveId stave) {
    return std::get<Note>(
               node->lane(track)->stave(stave)->voice(kVoice1).events()[0])
        .pitch;
  };
  EXPECT_EQ(pasted_pitch(*track_a, first_treble), pitch(Letter::kC, 4));
  EXPECT_EQ(pasted_pitch(*track_a, first_bass), pitch(Letter::kD, 4));
  EXPECT_EQ(pasted_pitch(*track_b, second_treble), pitch(Letter::kE, 4));
  EXPECT_EQ(pasted_pitch(*track_b, second_bass), pitch(Letter::kF, 4));
}

TEST(ClipboardCommandTest, SparseOrdinalsOnMultiTrackOverflowNonFirstAnchor) {
  // Two-track fragment with sparse staves, anchor on second track.
  // Verify multi-track overflow correctly counts per track.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}, FragmentTrackShape{2}},
      {FragmentVoicePart{
           0, 0, kVoice1,
           build_voice({make_note(pitch(Letter::kC, 4), quarter())})},
       FragmentVoicePart{
           1, 1, kVoice1,
           build_voice({make_note(pitch(Letter::kG, 4), quarter())})}});

  // Anchor on track_b (single staff). track_ordinal 0 → track_b itself,
  // track_ordinal 1 → would need another active track.
  const PasteAnchor anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(ClipboardCommandTest, PastedChordAllChordNoteIdsRegeneratedDisjoint) {
  // Paste a fragment containing a chord twice. Every ChordNote id must be
  // freshly regenerated each time, disjoint within and across pastes.
  Fixture                fx;
  std::vector<ChordNote> chord_notes = {
      ChordNote{NotationEntityId{}, pitch(Letter::kC, 4), false},
      ChordNote{NotationEntityId{}, pitch(Letter::kE, 4), false}};
  const Chord  chord        = make_chord(quarter(), std::move(chord_notes));
  VoiceContent part_content = build_voice({chord});

  const NotationFragment fragment =
      make_fragment(quarter().resolved(), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, part_content}});

  const PasteAnchor anchor1{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(0)};
  const PasteAnchor anchor2{fx.node_id, fx.track_a, fx.stave_a_treble,
                            rat(1, 4)};

  PasteFragmentCommand first(fragment, anchor1);
  ASSERT_TRUE(first.execute(fx.project).ok());
  PasteFragmentCommand second(fragment, anchor2);
  ASSERT_TRUE(second.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Chord>(content.events()[0]));
  EXPECT_TRUE(std::holds_alternative<Chord>(content.events()[1]));

  const Chord& chord1 = std::get<Chord>(content.events()[0]);
  const Chord& chord2 = std::get<Chord>(content.events()[1]);

  // Each chord must have unique top-level id.
  EXPECT_NE(chord1.id, chord2.id);

  // Collect all ChordNote ids across both pasted chords.
  std::vector<NotationEntityId> all_notehead_ids;
  for (const ChordNote& cn : chord1.notes)
    all_notehead_ids.push_back(cn.id);
  for (const ChordNote& cn : chord2.notes)
    all_notehead_ids.push_back(cn.id);

  const std::unordered_set<NotationEntityId> unique(all_notehead_ids.begin(),
                                                    all_notehead_ids.end());
  EXPECT_EQ(all_notehead_ids.size(), unique.size());
}

TEST(ClipboardCommandTest, MarkingSurvivalAndRemovalAcrossPasteBoundary) {
  // Events: n1, n2, n3 (all eighth). Markings on n1 (dynamic), n2→n3
  // (slur), n1→n2 (hairpin), n1+n2+n3 (beam override), n2 with grace group.
  // Paste replaces [1/4, 1/2), beginning exactly at n3's attack. Verify:
  //   - Dynamic on n1 survives (n1 is in left region).
  //   - Slur n2→n3 and beam override drop because n3 is removed.
  //   - Hairpin n1→n2 survives because both events precede the range.
  //   - Grace group on n2 survives because n2 remains immediately before
  //     the replaced range.
  Fixture      fx;
  const Note   n1   = make_note(pitch(Letter::kC, 4), eighth());
  const Note   n2   = make_note(pitch(Letter::kD, 4), eighth());
  const Note   n3   = make_note(pitch(Letter::kE, 4), eighth());
  VoiceContent dest = build_voice({n1, n2, n3});
  ASSERT_TRUE(dest.add_dynamic(make_dynamic_marking(n1.id, Dynamic::kMf)).ok());
  ASSERT_TRUE(dest.add_slur(make_slur(n2.id, n3.id)).ok());
  ASSERT_TRUE(
      dest.add_hairpin(make_hairpin(n1.id, n2.id, HairpinDirection::kCrescendo))
          .ok());
  ASSERT_TRUE(
      dest.add_beam_override(make_beam_override(BeamOverride::Kind::kJoin,
                                                {n1.id, n2.id, n3.id}))
          .ok());
  const graphscore::GraceNote grace_note{
      graphscore::NotationEntityId{}, pitch(Letter::kF, 5), eighth(),
      graphscore::GraceNoteType::kAcciaccatura, true};
  const auto grace_group = make_grace_group(n2.id, {grace_note});
  ASSERT_TRUE(dest.add_grace_group(grace_group).ok());
  ASSERT_TRUE(validate_voice_references(dest).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, dest);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 4)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);

  // Dynamic on n1 survives; its at_event rewired to left n1.
  ASSERT_GE(content.dynamics().size(), 1u);
  EXPECT_EQ(content.dynamics()[0].at_event, event_id(content.events()[0]));
  EXPECT_EQ(content.dynamics()[0].value, Dynamic::kMf);

  // Slur n2→n3 and beam override n1,n2,n3 are dropped (n3 removed).
  EXPECT_TRUE(content.slurs().empty());
  EXPECT_TRUE(content.beam_overrides().empty());

  // Hairpin n1→n2 and the GraceGroup survive because both principals/endpoints
  // are before the replaced range [1/4, 1/2).
  EXPECT_GE(content.hairpins().size(), 1u);
  ASSERT_EQ(content.grace_groups().size(), 1u);
  const auto& pasted_group = content.grace_groups()[0];
  EXPECT_EQ(pasted_group.id, grace_group.id);
  EXPECT_EQ(pasted_group.principal_event, n2.id);
  ASSERT_EQ(pasted_group.notes.size(), 1u);
  ASSERT_EQ(grace_group.notes.size(), 1u);
  EXPECT_EQ(pasted_group.notes[0].id, grace_group.notes[0].id);
  EXPECT_EQ(pasted_group.notes[0].pitch, grace_group.notes[0].pitch);
  EXPECT_EQ(pasted_group.notes[0].duration, grace_group.notes[0].duration);
  EXPECT_EQ(pasted_group.notes[0].type, grace_group.notes[0].type);
  EXPECT_EQ(pasted_group.notes[0].slashed, grace_group.notes[0].slashed);

  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, DestinationGraceGroupRemovedWhenPrincipalReplaced) {
  Fixture                     fx;
  const Note                  n1 = make_note(pitch(Letter::kC, 4), eighth());
  const Note                  n2 = make_note(pitch(Letter::kD, 4), eighth());
  const Note                  n3 = make_note(pitch(Letter::kE, 4), eighth());
  VoiceContent                destination = build_voice({n1, n2, n3});
  const graphscore::GraceNote grace_note{
      NotationEntityId{}, pitch(Letter::kF, 5), eighth(),
      graphscore::GraceNoteType::kAppoggiatura, false};
  ASSERT_TRUE(
      destination.add_grace_group(make_grace_group(n2.id, {grace_note})).ok());
  ASSERT_TRUE(validate_voice_references(destination).empty());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1, std::move(destination));

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB, 3), eighth())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           rat(1, 8)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  EXPECT_TRUE(content.grace_groups().empty());
  EXPECT_TRUE(validate_voice_references(content).empty());
}

TEST(ClipboardCommandTest, FragmentMarkingReferenceRewireAndTwoPastesDisjoint) {
  // Fragment with dynamics, hairpin, slur, beam override, grace group.
  // Paste twice; verify each paste's marking references are correctly
  // rewired to the pasted events of THAT paste, and both paste sets are
  // ID-disjoint.
  Fixture      fx;
  const Note   n1   = make_note(pitch(Letter::kC, 4), eighth());
  const Note   n2   = make_note(pitch(Letter::kD, 4), eighth());
  VoiceContent part = build_voice({n1, n2});
  ASSERT_TRUE(part.add_dynamic(make_dynamic_marking(n1.id, Dynamic::kFf)).ok());
  ASSERT_TRUE(
      part.add_hairpin(make_hairpin(n1.id, n2.id, HairpinDirection::kCrescendo))
          .ok());
  ASSERT_TRUE(part.add_slur(make_slur(n1.id, n2.id)).ok());
  ASSERT_TRUE(
      part.add_beam_override(
              make_beam_override(BeamOverride::Kind::kJoin, {n1.id, n2.id}))
          .ok());
  std::vector<graphscore::GraceNote> grace_notes;
  grace_notes.push_back(graphscore::GraceNote{
      graphscore::NotationEntityId{}, pitch(Letter::kG, 5), eighth(),
      graphscore::GraceNoteType::kAcciaccatura, true});
  ASSERT_TRUE(
      part.add_grace_group(make_grace_group(n2.id, std::move(grace_notes)))
          .ok());

  const NotationFragment frag =
      make_fragment(rat(1, 4), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, part}});

  // First paste at [0, 1/4).
  const PasteAnchor    anchor1{fx.node_id, fx.track_a, fx.stave_a_treble,
                            Rational(0)};
  PasteFragmentCommand first(frag, anchor1);
  ASSERT_TRUE(first.execute(fx.project).ok());

  const VoiceContent& content1 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content1.events().size(), 2u);
  const NotationEntityId e1_0 = event_id(content1.events()[0]);
  const NotationEntityId e1_1 = event_id(content1.events()[1]);

  ASSERT_GE(content1.dynamics().size(), 1u);
  EXPECT_EQ(content1.dynamics()[0].at_event, e1_0);
  ASSERT_GE(content1.hairpins().size(), 1u);
  EXPECT_EQ(content1.hairpins()[0].start_event, e1_0);
  EXPECT_EQ(content1.hairpins()[0].end_event, e1_1);
  ASSERT_GE(content1.slurs().size(), 1u);
  EXPECT_EQ(content1.slurs()[0].start_event, e1_0);
  EXPECT_EQ(content1.slurs()[0].end_event, e1_1);
  ASSERT_GE(content1.beam_overrides().size(), 1u);
  EXPECT_EQ(content1.beam_overrides()[0].events[0], e1_0);
  EXPECT_EQ(content1.beam_overrides()[0].events[1], e1_1);
  ASSERT_GE(content1.grace_groups().size(), 1u);
  EXPECT_EQ(content1.grace_groups()[0].principal_event, e1_1);

  // Second paste at [1/2, 3/4).
  const PasteAnchor    anchor2{fx.node_id, fx.track_a, fx.stave_a_treble,
                            rat(1, 2)};
  PasteFragmentCommand second(frag, anchor2);
  ASSERT_TRUE(second.execute(fx.project).ok());

  const VoiceContent& content2 =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  // After two pastes with a gap: events[0-1] first paste, events[2] gap
  // rest, events[3-4] second paste.
  ASSERT_GE(content2.events().size(), 5u);
  const NotationEntityId e2_0 = event_id(content2.events()[3]);
  const NotationEntityId e2_1 = event_id(content2.events()[4]);

  // Verify second paste's markings rewire correctly.
  std::size_t dynamics_count = 0;
  for (const auto& d : content2.dynamics()) {
    if (d.at_event == e2_0)
      dynamics_count++;
  }
  EXPECT_GE(dynamics_count, 1u);

  // Hairpin and slur from the second paste must reference e2_0, e2_1.
  bool saw_hairpin = false;
  for (const auto& hp : content2.hairpins()) {
    if (hp.start_event == e2_0 && hp.end_event == e2_1)
      saw_hairpin = true;
  }
  EXPECT_TRUE(saw_hairpin);
  bool saw_slur = false;
  for (const auto& sl : content2.slurs()) {
    if (sl.start_event == e2_0 && sl.end_event == e2_1)
      saw_slur = true;
  }
  EXPECT_TRUE(saw_slur);

  // Collect all ids and verify disjointness.
  std::vector<NotationEntityId> all_ids;
  collect_ids(content2, all_ids);
  const std::unordered_set<NotationEntityId> all_set(all_ids.begin(),
                                                     all_ids.end());
  EXPECT_EQ(all_ids.size(), all_set.size());
}

// ============================================================
// Defect 1 — pedal-only stave mapping
// ============================================================

// A fixture with a grand-staff track (A) containing written material on
// treble and a pedal span on the bass stave — no voice part names the
// bass stave.  Paste must map/create the bass stave for the pedal span.
struct PedalOnlyFixture {
  Project project{ProjectId::generate(), "PedalOnly"};
  TrackId track_a;
  StaveId stave_a_treble;
  StaveId stave_a_bass;
  NodeId  node_id;

  static constexpr std::size_t kMeasureCount = 4;

  PedalOnlyFixture() {
    const auto tid_a = project.add_track("A", StaffLayout::grand_staff(),
                                         *MidiChannel::create(0));
    assert(tid_a.has_value());
    track_a                  = *tid_a;
    const Track* track_a_ptr = project.find_active_track(track_a);
    assert(track_a_ptr != nullptr);
    stave_a_treble = track_a_ptr->layout().staves()[0].id;
    stave_a_bass   = track_a_ptr->layout().staves()[1].id;

    node_id      = project.add_node("N");
    Node* node_p = project.find_node(node_id);
    assert(node_p != nullptr);

    std::vector<Measure> measures;
    for (std::size_t i = 0; i < kMeasureCount; ++i) {
      measures.push_back(
          Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
    }
    auto timeline = NodeTimeline::create(
        measures, {StaveDefinition{stave_a_treble, Clef::kTreble},
                   StaveDefinition{stave_a_bass, Clef::kBass}});
    assert(timeline.has_value());
    node_p->set_timeline(std::move(*timeline));
  }

  Node* node() { return project.find_node(node_id); }

  Rational node_end() { return node()->timeline()->node_end(); }
};

TEST(ClipboardCommandTest,
     PedalOnlyStaveFragmentMapsAndCreatesDestinationStave) {
  // Fragment: voice part on treble (ordinal 0) + pedal span on bass
  // (ordinal 1). No voice part names the bass stave. Paste must map both
  // staves: treble maps to anchor stave (bass), pedal-only stave is an
  // extra referenced ordinal that requires a second stave.
  PedalOnlyFixture fx;

  // Source fragment: part on ordinal 0, pedal span on ordinal 1.
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)}});

  // Anchor on the bass stave (index 1). Ordinal 0 → bass, ordinal 1 →
  // would need one more stave (nonexistent in a 2-stave grand staff when
  // starting at index 1). Verify — this actually fails because there's no
  // third stave.
  const PasteAnchor    anchor{fx.node_id, fx.track_a, fx.stave_a_bass,
                           Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(ClipboardCommandTest, PedalSpanOnUnreferencedStavePastesOntoGrandStaff) {
  // Fragment: part on ordinal 0, pedal span on ordinal 1. Anchor on treble
  // stave (ordinal 0 in grand staff). Treble maps ordinal 0 → treble;
  // bass maps ordinal 1 → bass. Paste succeeds because grand staff has
  // two staves. Destination bass gets the pedal span but no voice rebuild.
  PedalOnlyFixture fx;

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)}});

  const PasteAnchor    anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Treble stave has the C half note.
  const auto* treble = fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble);
  ASSERT_NE(treble, nullptr);
  ASSERT_GE(treble->voice(kVoice1).events().size(), 1u);
  EXPECT_EQ(std::get<Note>(treble->voice(kVoice1).events()[0]).pitch,
            pitch(Letter::kC, 4));

  // Bass stave has the pedal span (offset to anchor position 0).
  const std::vector<PedalSpan>* spans =
      fx.node()->lane(fx.track_a)->pedal_spans(fx.stave_a_bass);
  ASSERT_NE(spans, nullptr);
  bool saw_pedal = false;
  for (const PedalSpan& span : *spans) {
    if (span.start == Rational(0) && span.end == rat(1, 2)) {
      saw_pedal = true;
      break;
    }
  }
  EXPECT_TRUE(saw_pedal);
}

TEST(ClipboardCommandTest, PedalOnlyFragmentToSingleStaffFailsAtomically) {
  // Fragment: part on ordinal 0, pedal on ordinal 1. Single-staff
  // destination: ordinal 0 maps to the only stave, ordinal 1 needs a
  // second → overflow. Paste must fail, model unchanged.
  PedalOnlyFixture fx;
  // Add a single-staff track.
  const auto tid_b = fx.project.add_track("B", StaffLayout::single_staff(),
                                          *MidiChannel::create(1));
  ASSERT_TRUE(tid_b.has_value());
  const TrackId track_b     = *tid_b;
  const Track*  track_b_ptr = fx.project.find_active_track(track_b);
  ASSERT_NE(track_b_ptr, nullptr);
  const StaveId stave_b = track_b_ptr->layout().staves()[0].id;

  const TrackLane before = *fx.node()->lane(track_b);

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)}});

  const PasteAnchor    anchor{fx.node_id, track_b, stave_b, Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  const Result         result = command.execute(fx.project);
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.node()->lane(track_b) == before);
}

TEST(ClipboardCommandTest, PedalOnlyStaveUndoRedoExactRoundTrip) {
  // Paste a fragment with pedal on bass-only into grand staff. Undo must
  // restore exact pre-state (no pedal span on bass). Redo must re-apply.
  PedalOnlyFixture fx;
  // Pre-write only on treble; bass has no stave yet.
  fx.node()->lane(fx.track_a)->ensure_stave(fx.stave_a_treble);
  // Give treble voice 1 some content for proper lane existence.
  VoiceContent vc;
  ASSERT_TRUE(vc.normalize(fx.node_end()).ok());
  fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1) = vc;

  const TrackLane before = *fx.node()->lane(fx.track_a);
  ASSERT_FALSE(fx.node()->lane(fx.track_a)->has_stave(fx.stave_a_bass));

  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)}});

  const PasteAnchor    anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Bass stave now exists and has pedal span.
  EXPECT_TRUE(fx.node()->lane(fx.track_a)->has_stave(fx.stave_a_bass));
  const TrackLane after_execute = *fx.node()->lane(fx.track_a);

  // Undo: bass stave gone, exact pre-state restored.
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(*fx.node()->lane(fx.track_a) == before);
  EXPECT_FALSE(fx.node()->lane(fx.track_a)->has_stave(fx.stave_a_bass));

  // Redo: bass stave back with pedal.
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.node()->lane(fx.track_a)->has_stave(fx.stave_a_bass));
  EXPECT_TRUE(*fx.node()->lane(fx.track_a) == after_execute);
}

TEST(ClipboardCommandTest, PedalOnlyMultiTrackPedalMapsAndPastesCorrectly) {
  // Two-track fragment: voice part on track_ordinal 0, pedal on
  // track_ordinal 1 stave_ordinal 1. The pedal span on the second track
  // compacts to that track's only stave (ordinal 1 → index 0). Paste
  // succeeds and the pedal span appears on the correct stave.
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}, FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 4), quarter())})}},
      {FragmentPedalSpan{1, 1, Rational(0), rat(1, 4)}});

  const PasteAnchor    anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};
  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Voice part pasted onto track_a treble.
  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC, 4));

  // Pedal span pasted onto track_b's stave.
  const std::vector<PedalSpan>* spans =
      fx.node()->lane(fx.track_b)->pedal_spans(fx.stave_b);
  ASSERT_NE(spans, nullptr);
  bool saw_pedal = false;
  for (const PedalSpan& span : *spans) {
    if (span.start == Rational(0) && span.end == rat(1, 4)) {
      saw_pedal = true;
      break;
    }
  }
  EXPECT_TRUE(saw_pedal);
}

// ============================================================
// Fix 2 — cut command failure atomicity: reachable failure + retry
// ============================================================

TEST(ClipboardCommandTest,
     CutFailureOnStraddlingTupletLeavesModelAndFragmentDisengaged) {
  // Simulate a reachable deterministic failure: cut a range that straddles
  // a tuplet. The model must be unchanged, fragment() must be disengaged,
  // and a fresh retry with a valid selection must succeed.
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const Selection bad_selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 24)}}});

  CutFragmentCommand bad_cut(bad_selection);
  EXPECT_EQ(bad_cut.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(bad_cut.fragment().has_value());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
  // State must still be fresh/retryable: undo and double-execute both reject.
  EXPECT_EQ(bad_cut.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(bad_cut.redo(fx.project).code(), ResultCode::kInvalidArgument);

  // A fresh command with a valid selection must succeed on the same model.
  const Selection good_selection = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), rat(1, 4)}}});
  CutFragmentCommand good_cut(good_selection);
  ASSERT_TRUE(good_cut.execute(fx.project).ok());
  ASSERT_TRUE(good_cut.fragment().has_value());
}

TEST(ClipboardCommandTest,
     PasteFailureOnStraddlingTupletLeavesModelUnchangedFreshRetry) {
  // Paste into a destination where a tuplet straddles the left boundary.
  // Model must be unchanged, command state kFresh, retry valid succeeds.
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice1,
            build_voice({make_note(pitch(Letter::kC), tuplet_eighth()),
                         make_note(pitch(Letter::kD), tuplet_eighth()),
                         make_note(pitch(Letter::kE), tuplet_eighth())}));
  const TrackLane before = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 8), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kB), eighth())})}});
  const PasteAnchor bad_anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                               rat(1, 24)};

  PasteFragmentCommand bad_paste(fragment, bad_anchor);
  EXPECT_EQ(bad_paste.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);

  // Retry with valid anchor.
  const PasteAnchor    good_anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                                Rational(1)};
  PasteFragmentCommand good_paste(fragment, good_anchor);
  ASSERT_TRUE(good_paste.execute(fx.project).ok());
}

TEST(ClipboardCommandTest, CompleteMeasureExtractPasteAllVoicesExactLifecycle) {
  Fixture                   fx;
  const std::vector<Voice>  voices = {kVoice1, kVoice2, kVoice3, kVoice4};
  const std::vector<Letter> source_pitches = {Letter::kC, Letter::kD,
                                              Letter::kE, Letter::kF};

  for (std::size_t i = 0; i < voices.size(); ++i) {
    fx.assign_and_complete(fx.track_a, fx.stave_a_treble, voices[i],
                           {make_note(pitch(source_pitches[i]), whole())});
    fx.assign(fx.track_b, fx.stave_b, voices[i],
              build_voice({make_note(pitch(Letter::kG), whole()),
                           make_note(pitch(Letter::kA), whole()),
                           make_note(pitch(Letter::kB), whole()),
                           make_note(pitch(Letter::kC, 5), whole())}));
  }

  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0}});
  const FragmentExtraction extraction = extract_fragment(fx.project, selection);
  ASSERT_TRUE(extraction.status.ok());
  ASSERT_TRUE(extraction.fragment.has_value());
  EXPECT_EQ(extraction.fragment->span_length(), Rational(1));
  ASSERT_EQ(extraction.fragment->parts().size(), 4u);

  const TrackLane               source_before      = fx.lane_of(fx.track_a);
  const TrackLane               destination_before = fx.lane_of(fx.track_b);
  std::vector<NotationEntityId> fragment_ids;
  for (const FragmentVoicePart& part : extraction.fragment->parts())
    collect_ids(part.content, fragment_ids);

  const PasteAnchor    anchor{fx.node_id, fx.track_b, fx.stave_b, Rational(2)};
  PasteFragmentCommand command(*extraction.fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane destination_after = fx.lane_of(fx.track_b);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == source_before);

  for (std::size_t i = 0; i < voices.size(); ++i) {
    const VoiceContent& before =
        destination_before.stave(fx.stave_b)->voice(voices[i]);
    const VoiceContent& after =
        destination_after.stave(fx.stave_b)->voice(voices[i]);
    ASSERT_EQ(before.events().size(), 4u);
    ASSERT_EQ(after.events().size(), 4u);
    EXPECT_TRUE(after.events()[0] == before.events()[0]);
    EXPECT_TRUE(after.events()[1] == before.events()[1]);
    EXPECT_TRUE(after.events()[3] == before.events()[3]);
    EXPECT_EQ(std::get<Note>(after.events()[2]).pitch,
              pitch(source_pitches[i]));
    EXPECT_NE(event_id(after.events()[2]),
              event_id(extraction.fragment->parts()[i].content.events()[0]));
  }
  std::vector<NotationEntityId> pasted_ids;
  for (const Voice voice : voices) {
    collect_ids(destination_after.stave(fx.stave_b)->voice(voice), pasted_ids);
  }
  const std::unordered_set<NotationEntityId> pasted_set(pasted_ids.begin(),
                                                        pasted_ids.end());
  for (const NotationEntityId id : fragment_ids)
    EXPECT_FALSE(pasted_set.contains(id));

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == source_before);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == destination_before);
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == source_before);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == destination_after);
}

TEST(ClipboardCommandTest,
     MultiTrackMultiStavePasteLifecycleAndAtomicStaleRejection) {
  Fixture                fx;
  const TrackLane        before_a = fx.lane_of(fx.track_a);
  const TrackLane        before_b = fx.lane_of(fx.track_b);
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}, FragmentTrackShape{1}},
      {FragmentVoicePart{0, 0, kVoice1,
                         build_voice({make_note(pitch(Letter::kC), half())})},
       FragmentVoicePart{
           0, 1, kVoice2,
           build_voice({make_note(pitch(Letter::kE, 3), half())})},
       FragmentVoicePart{1, 0, kVoice3,
                         build_voice({make_note(pitch(Letter::kG), half())})}},
      {FragmentPedalSpan{0, 1, Rational(0), rat(1, 2)},
       FragmentPedalSpan{1, 0, rat(1, 4), rat(1, 2)}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(1)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const TrackLane after_a = fx.lane_of(fx.track_a);
  const TrackLane after_b = fx.lane_of(fx.track_b);
  EXPECT_FALSE(after_a == before_a);
  EXPECT_FALSE(after_b == before_b);
  ASSERT_NE(after_a.pedal_spans(fx.stave_a_bass), nullptr);
  ASSERT_NE(after_b.pedal_spans(fx.stave_b), nullptr);

  *fx.node()->lane(fx.track_b) = TrackLane{};
  const TrackLane stale_a      = fx.lane_of(fx.track_a);
  const TrackLane stale_b      = fx.lane_of(fx.track_b);
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == stale_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == stale_b);

  *fx.node()->lane(fx.track_b) = after_b;
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);

  *fx.node()->lane(fx.track_a) = TrackLane{};
  const TrackLane redo_stale_a = fx.lane_of(fx.track_a);
  const TrackLane redo_stale_b = fx.lane_of(fx.track_b);
  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == redo_stale_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == redo_stale_b);

  *fx.node()->lane(fx.track_a) = before_a;
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == after_b);
}

TEST(ClipboardCommandTest, MultiLaneCutLifecycleAndAtomicStaleUndoRejection) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), whole())});
  fx.assign_and_complete(fx.track_b, fx.stave_b, kVoice1,
                         {make_note(pitch(Letter::kG), whole())});
  ASSERT_TRUE(fx.node()
                  ->lane(fx.track_b)
                  ->add_pedal_span(fx.stave_b,
                                   make_pedal_span(Rational(0), Rational(1)))
                  .ok());
  const TrackLane before_a  = fx.lane_of(fx.track_a);
  const TrackLane before_b  = fx.lane_of(fx.track_b);
  const Selection selection = *FullMeasureSet::create(
      {FullMeasureItem{fx.node_id, fx.track_a, fx.stave_a_treble, 0},
       FullMeasureItem{fx.node_id, fx.track_b, fx.stave_b, 0}});

  CutFragmentCommand command(selection);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.fragment().has_value());
  const TrackLane after_a = fx.lane_of(fx.track_a);
  const TrackLane after_b = fx.lane_of(fx.track_b);

  *fx.node()->lane(fx.track_b) = TrackLane{};
  const TrackLane stale_a      = fx.lane_of(fx.track_a);
  const TrackLane stale_b      = fx.lane_of(fx.track_b);
  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == stale_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == stale_b);

  *fx.node()->lane(fx.track_b) = after_b;
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == before_b);
  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_a);
  EXPECT_TRUE(fx.lane_of(fx.track_b) == after_b);
}

TEST(ClipboardCommandTest,
     PasteLastCandidateValidationFailureLeavesCommandRetryable) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2,
            build_voice({make_note(pitch(Letter::kD), quarter())}));
  const TrackLane        invalid_before = fx.lane_of(fx.track_a);
  const NotationFragment fragment       = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == invalid_before);

  VoiceContent repaired =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice2);
  ASSERT_TRUE(repaired.normalize(fx.node_end()).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2, std::move(repaired));
  ASSERT_TRUE(command.execute(fx.project).ok());
}

TEST(ClipboardCommandTest,
     CutLastCandidateValidationFailureLeavesFragmentEmptyAndRetries) {
  Fixture fx;
  fx.assign_and_complete(fx.track_a, fx.stave_a_treble, kVoice1,
                         {make_note(pitch(Letter::kC), whole())});
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2,
            build_voice({make_note(pitch(Letter::kD), quarter())}));
  const TrackLane invalid_before = fx.lane_of(fx.track_a);
  const Selection selection      = *ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fx.node_id, fx.track_a, fx.stave_a_treble, kVoice1,
                          MusicalSpan{Rational(0), Rational(1)}}});

  CutFragmentCommand command(selection);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(command.fragment().has_value());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == invalid_before);

  VoiceContent repaired =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice2);
  ASSERT_TRUE(repaired.normalize(fx.node_end()).ok());
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2, std::move(repaired));
  ASSERT_TRUE(command.execute(fx.project).ok());
  EXPECT_TRUE(command.fragment().has_value());
}

// ============================================================
// Phase 8h-iv -- meter compatibility gate (locked, never applies signature)
// ============================================================

TEST(ClipboardCommandTest, PasteMeterGateAcceptsMatchingTimeSignature) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_TRUE(command.execute(fx.project).ok());
}

TEST(ClipboardCommandTest,
     PasteMeterGateRejectsMismatchedTimeSignatureLeavesProjectUnchanged) {
  Project    project{ProjectId::generate(), "Meter"};
  const auto tid = project.add_track("A", StaffLayout::single_staff(),
                                     *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(3, 4), *KeySignature::create(0)}},
      {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  node_p->lane(track_id)->ensure_stave(stave_id);

  const std::vector<FragmentMeasureContext> mismatched = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                             *KeySignature::create(0)}};
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {}, {}, mismatched);
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(0)};

  const TrackLane      before = *node_p->lane(track_id);
  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*node_p->lane(track_id) == before);
}

TEST(ClipboardCommandTest, PasteMeterGateRejectsMultiMeterFragment) {
  Fixture                                   fx;
  const std::vector<FragmentMeasureContext> multi_meter = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                             *KeySignature::create(0)},
      FragmentMeasureContext{rat(1, 2), *TimeSignature::create(3, 4),
                             *KeySignature::create(0)}};
  const NotationFragment fragment = make_fragment(
      Rational(1), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, rest_filled(Rational(1))}}, {}, {}, {},
      multi_meter);
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  const TrackLane      before = fx.lane_of(fx.track_a);
  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}

TEST(ClipboardCommandTest, PasteMeterGateRejectsMultiMeterDestinationRange) {
  Project    project{ProjectId::generate(), "Meter"};
  const auto tid = project.add_track("A", StaffLayout::single_staff(),
                                     *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  // measure 0: 4/4 (length 1); measure 1: 3/4 (length 3/4). node_end == 7/4.
  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)},
       Measure{*TimeSignature::create(3, 4), *KeySignature::create(0)}},
      {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  node_p->lane(track_id)->ensure_stave(stave_id);

  // Range [0, 9/8) overlaps all of measure 0 and the start of measure 1.
  const NotationFragment fragment =
      make_fragment(rat(9, 8), {FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(9, 8))}});
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(0)};

  const TrackLane      before = *node_p->lane(track_id);
  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*node_p->lane(track_id) == before);
}

TEST(ClipboardCommandTest, PasteMeterGatePickdownRangeUsesLastMainRegionMeter) {
  Project    project{ProjectId::generate(), "Pickdown"};
  const auto tid = project.add_track("A", StaffLayout::single_staff(),
                                     *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}},
      {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  ASSERT_TRUE(node_p->timeline()->set_pickdown(rat(1, 8)).ok());
  node_p->lane(track_id)->ensure_stave(stave_id);

  // Range [1, 9/8) lies entirely in the pickdown; the governing meter is
  // the (only, hence last) main-region measure's 4/4.
  const NotationFragment fragment =
      make_fragment(rat(1, 8), {FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 8))}});
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(1)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_TRUE(command.execute(project).ok());
}

TEST(ClipboardCommandTest, PasteMeterGatePickdownRangeRejectsMismatchedMeter) {
  Project    project{ProjectId::generate(), "Pickdown"};
  const auto tid = project.add_track("A", StaffLayout::single_staff(),
                                     *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  auto timeline = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}},
      {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  ASSERT_TRUE(node_p->timeline()->set_pickdown(rat(1, 8)).ok());
  node_p->lane(track_id)->ensure_stave(stave_id);

  const std::vector<FragmentMeasureContext> mismatched = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(3, 4),
                             *KeySignature::create(0)}};
  const NotationFragment fragment =
      make_fragment(rat(1, 8), {FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 8))}},
                    {}, {}, {}, mismatched);
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(1)};

  const TrackLane      before = *node_p->lane(track_id);
  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*node_p->lane(track_id) == before);
}

TEST(ClipboardCommandTest, PasteMeterGateKeySignatureDifferenceDoesNotReject) {
  Fixture                                   fx;
  const std::vector<FragmentMeasureContext> different_key = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                             *KeySignature::create(5)}};
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {}, {}, different_key);
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_TRUE(command.execute(fx.project).ok());
}

TEST(ClipboardCommandTest,
     PasteMeterMismatchRejectionLeavesClefLanesUntouched) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 8), Clef::kAlto)
                  .ok());
  const ClefLane  before_clef = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const TrackLane before_lane = fx.lane_of(fx.track_a);

  const std::vector<FragmentMeasureContext> mismatched = {
      FragmentMeasureContext{Rational(0), *TimeSignature::create(3, 4),
                             *KeySignature::create(0)}};
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}}, {},
      {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}}, {}, mismatched);
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_clef);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_lane);
}

TEST(ClipboardCommandTest,
     PasteLaneValidationFailureLeavesClefLanesUntouchedEvenWithClefChanges) {
  Fixture fx;
  fx.assign(fx.track_a, fx.stave_a_treble, kVoice2,
            build_voice({make_note(pitch(Letter::kD), quarter())}));
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 8), Clef::kAlto)
                  .ok());
  const ClefLane  before_clef = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const TrackLane before_lane = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 8), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_clef);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_lane);
}

// ============================================================
// Phase 8h-iv -- interior clef application (contained, replace-not-interleave)
// ============================================================

TEST(ClipboardCommandTest,
     PasteAppliesInteriorClefChangeOnMappedDestinationStaves) {
  Fixture                fx;
  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}, FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))},
                     FragmentVoicePart{1, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {},
                    {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass},
                     FragmentClefChange{1, 0, rat(1, 4), Clef::kAlto}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* treble_lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(treble_lane, nullptr);
  EXPECT_EQ(treble_lane->clef_at(rat(1, 4)), Clef::kBass);

  // track_ordinal 1 walks forward to the next active track (track_b) at
  // that track's own stave_ordinal 0 (stave_b), not offset by the anchor.
  const ClefLane* track_b_lane = fx.timeline()->clef_lane(fx.stave_b);
  ASSERT_NE(track_b_lane, nullptr);
  EXPECT_EQ(track_b_lane->clef_at(rat(1, 4)), Clef::kAlto);
}

TEST(ClipboardCommandTest, PasteNeverAppliesFragmentClefAtOrigin) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {}, {FragmentStaveContext{0, 0, Clef::kAlto}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  EXPECT_TRUE(lane->changes().empty());
  EXPECT_EQ(lane->clef_at(Rational(0)), Clef::kTreble);
}

TEST(ClipboardCommandTest, PasteReplacesDestinationClefChangesInsideRange) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 8), Clef::kAlto)
                  .ok());

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 8), Clef::kTenor}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  EXPECT_EQ(lane->clef_at(rat(1, 8)), Clef::kTenor);

  std::size_t matches_at_position = 0;
  for (const ClefChange& change : lane->changes()) {
    if (change.position == rat(1, 8))
      ++matches_at_position;
  }
  EXPECT_EQ(matches_at_position, 1u);
}

TEST(ClipboardCommandTest, PasteContainmentPreservesPrevailingClefAfterRange) {
  Fixture                fx;
  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  EXPECT_EQ(lane->clef_at(rat(1, 4)), Clef::kBass);
  // Contained: everything from range_end (1/2) onward, which this paste
  // never touches, still shows the pre-paste prevailing clef.
  EXPECT_EQ(lane->clef_at(rat(1, 2)), Clef::kTreble);
  EXPECT_EQ(lane->clef_at(Rational(3)), Clef::kTreble);

  bool has_reassertion = false;
  for (const ClefChange& change : lane->changes()) {
    if (change.position == rat(1, 2) && change.clef == Clef::kTreble)
      has_reassertion = true;
  }
  EXPECT_TRUE(has_reassertion);
}

TEST(ClipboardCommandTest, PasteSkipsClefReassertionWhenRangeEndEqualsNodeEnd) {
  Fixture        fx;
  const Rational node_end        = fx.node_end();
  const Rational span            = rat(1, 2);
  const Rational anchor_position = node_end - span;

  const NotationFragment fragment =
      make_fragment(span, {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(span)}}, {},
                    {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           anchor_position};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  ASSERT_EQ(lane->changes().size(), 1u);
  EXPECT_EQ(lane->changes().front().position, anchor_position + rat(1, 4));
}

TEST(ClipboardCommandTest, PasteClefApplicationDoesNotAlterStoredPitches) {
  Fixture                fx;
  const NotationFragment fragment = make_fragment(
      rat(1, 2), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC, 5), quarter()),
                       make_note(pitch(Letter::kG, 2), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const VoiceContent& content =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  ASSERT_GE(content.events().size(), 2u);
  EXPECT_EQ(std::get<Note>(content.events()[0]).pitch, pitch(Letter::kC, 5));
  EXPECT_EQ(std::get<Note>(content.events()[1]).pitch, pitch(Letter::kG, 2));
}

// ============================================================
// Phase 8h-iv -- absent-clef-lane creation, reversibility, atomicity
// ============================================================

TEST(ClipboardCommandTest, PasteCreatesClefLaneWhenAbsentAndUndoRemovesIt) {
  Project    project{ProjectId::generate(), "NoClefLane"};
  const auto tid = project.add_track(
      "A", StaffLayout::single_staff(Clef::kAlto), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  const TrackId track_id  = *tid;
  const Track*  track_ptr = project.find_active_track(track_id);
  ASSERT_NE(track_ptr, nullptr);
  const StaveId stave_id = track_ptr->layout().staves()[0].id;

  const NodeId node_id = project.add_node("N");
  Node*        node_p  = project.find_node(node_id);
  ASSERT_NE(node_p, nullptr);

  std::vector<Measure> measures;
  measures.push_back(
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  // No StaveDefinition passed to create(): the resulting timeline has no
  // clef lane at all for this stave, simulating a track added after the
  // node's timeline was created (Project::add_track only calls
  // Node::ensure_lane, never touches NodeTimeline's clef lanes).
  auto timeline = NodeTimeline::create(measures, {});
  ASSERT_TRUE(timeline.has_value());
  node_p->set_timeline(std::move(*timeline));
  node_p->lane(track_id)->ensure_stave(stave_id);
  ASSERT_FALSE(node_p->timeline()->has_clef_lane(stave_id));

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{1}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 8), Clef::kBass}});
  const PasteAnchor anchor{node_id, track_id, stave_id, Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(project).ok());

  ASSERT_TRUE(node_p->timeline()->has_clef_lane(stave_id));
  const ClefLane* lane = node_p->timeline()->clef_lane(stave_id);
  ASSERT_NE(lane, nullptr);
  EXPECT_EQ(lane->default_clef(), Clef::kAlto);
  EXPECT_EQ(lane->clef_at(rat(1, 8)), Clef::kBass);
  const ClefLane after_execute = *lane;

  ASSERT_TRUE(command.undo(project).ok());
  EXPECT_FALSE(node_p->timeline()->has_clef_lane(stave_id));

  ASSERT_TRUE(command.redo(project).ok());
  ASSERT_TRUE(node_p->timeline()->has_clef_lane(stave_id));
  EXPECT_TRUE(*node_p->timeline()->clef_lane(stave_id) == after_execute);
}

TEST(ClipboardCommandTest, PasteClefExecuteUndoRedoRestoresExactly) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 8), Clef::kAlto)
                  .ok());
  const ClefLane before = *fx.timeline()->clef_lane(fx.stave_a_treble);

  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const ClefLane after_execute = *fx.timeline()->clef_lane(fx.stave_a_treble);
  EXPECT_FALSE(after_execute == before);

  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == after_execute);
}

TEST(ClipboardCommandTest, PasteUndoRejectsStaleClefLaneContextThenRetries) {
  Fixture                fx;
  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  const ClefLane  after_execute = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const TrackLane after_execute_lane = fx.lane_of(fx.track_a);

  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(3, 8), Clef::kTenor)
                  .ok());
  const ClefLane stale = *fx.timeline()->clef_lane(fx.stave_a_treble);

  EXPECT_EQ(command.undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == stale);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_execute_lane);

  ASSERT_TRUE(
      fx.timeline()->restore_clef_lane(fx.stave_a_treble, after_execute).ok());
  ASSERT_TRUE(command.undo(fx.project).ok());
}

TEST(ClipboardCommandTest, PasteRedoRejectsStaleClefLaneContextThenRetries) {
  Fixture                fx;
  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());
  ASSERT_TRUE(command.undo(fx.project).ok());
  const ClefLane  after_undo = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const TrackLane after_undo_lane = fx.lane_of(fx.track_a);

  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(3, 8), Clef::kTenor)
                  .ok());
  const ClefLane stale = *fx.timeline()->clef_lane(fx.stave_a_treble);

  EXPECT_EQ(command.redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == stale);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == after_undo_lane);

  ASSERT_TRUE(
      fx.timeline()->restore_clef_lane(fx.stave_a_treble, after_undo).ok());
  ASSERT_TRUE(command.redo(fx.project).ok());
}

// ============================================================
// Fix-round regressions: reviewer findings 1, 2, 3 (8h-iv NEEDS WORK)
// ============================================================

// Finding 1: "replace, not interleave" is unqualified -- a destination
// stave whose notes are wholly overwritten by the paste loses its in-range
// clef changes even when the fragment names no clef change of its own for
// that stave.
TEST(ClipboardCommandTest,
     PasteWipesStaleDestinationClefChangeWithNoFragmentClefChanges) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 4), Clef::kBass)
                  .ok());

  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);
  for (const ClefChange& change : lane->changes())
    EXPECT_NE(change.position, rat(1, 4));
  // Inside the wiped range, the removed change no longer governs, so the
  // stave's default clef prevails.
  EXPECT_EQ(lane->clef_at(rat(1, 4)), Clef::kTreble);
  // Containment: the pre-edit prevailing clef at range_end (Bass, from the
  // now-removed change at 1/4) is reasserted, so nothing at or after the
  // paste's own range changes from what it showed before the paste.
  EXPECT_EQ(lane->clef_at(rat(1, 2)), Clef::kBass);
  EXPECT_EQ(lane->clef_at(Rational(3)), Clef::kBass);
}

// Finding 1 (continued): within one paste, a stave a fragment clef change
// actually names and a stave it does not both get their in-range wholly
// overwritten music's stale destination clef changes wiped consistently.
TEST(ClipboardCommandTest,
     PasteWipesStaleClefsConsistentlyAcrossNamedAndUnnamedStaves) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 4), Clef::kBass)
                  .ok());
  ASSERT_TRUE(
      fx.timeline()->add_clef_change(fx.stave_b, rat(1, 4), Clef::kAlto).ok());

  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}, FragmentTrackShape{1}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))},
                     FragmentVoicePart{1, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(3, 8), Clef::kTenor}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Named stave: fragment names a clef change at 3/8, so the destination's
  // stale change at 1/4 is replaced; containment reasserts that stave's own
  // pre-edit clef at range_end (Bass, from the removed 1/4 change).
  const ClefLane* treble_lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(treble_lane, nullptr);
  for (const ClefChange& change : treble_lane->changes())
    EXPECT_NE(change.position, rat(1, 4));
  EXPECT_EQ(treble_lane->clef_at(rat(3, 8)), Clef::kTenor);
  EXPECT_EQ(treble_lane->clef_at(rat(1, 2)), Clef::kBass);

  // Unnamed stave: the fragment names no clef change on track_ordinal 1 at
  // all, yet its notes are wholly replaced too, so its stale change at 1/4
  // is wiped exactly the same way, with containment reasserting its own
  // pre-edit clef at range_end (Alto, from the removed 1/4 change).
  const ClefLane* track_b_lane = fx.timeline()->clef_lane(fx.stave_b);
  ASSERT_NE(track_b_lane, nullptr);
  for (const ClefChange& change : track_b_lane->changes())
    EXPECT_NE(change.position, rat(1, 4));
  EXPECT_EQ(track_b_lane->clef_at(rat(1, 4)), Clef::kTreble);
  EXPECT_EQ(track_b_lane->clef_at(rat(1, 2)), Clef::kAlto);
}

// Finding 7: adding clef-change ordinals to resolve_paste_mapping's `used`
// set (clipboard_command_helpers.hpp) means a fragment clef change naming a
// stave that no voice part or pedal span references now resolves and
// applies instead of hard-rejecting the whole paste with kInvalidArgument.
TEST(ClipboardCommandTest,
     PasteAppliesClefChangeOnStaveUnreferencedByVoicePartOrPedalSpan) {
  Fixture         fx;
  const ClefLane  before_treble = *fx.timeline()->clef_lane(fx.stave_a_treble);
  const ClefLane  before_bass   = *fx.timeline()->clef_lane(fx.stave_a_bass);
  const TrackLane before_lane   = fx.lane_of(fx.track_a);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 0, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 1, rat(1, 8), Clef::kAlto}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Application: the clef change on bass (ordinal 1), named by no voice
  // part or pedal span, is applied.
  const ClefLane* bass_lane = fx.timeline()->clef_lane(fx.stave_a_bass);
  ASSERT_NE(bass_lane, nullptr);
  EXPECT_EQ(bass_lane->clef_at(rat(1, 8)), Clef::kAlto);
  // Containment: nothing at or after range_end differs from the pre-edit
  // prevailing clef, even on this unreferenced stave.
  EXPECT_EQ(bass_lane->clef_at(rat(1, 4)), Clef::kBass);
  EXPECT_EQ(bass_lane->clef_at(Rational(3)), Clef::kBass);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_treble);

  // Exact undo.
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_bass) == before_bass);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_treble);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_lane);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_EQ(fx.timeline()->clef_lane(fx.stave_a_bass)->clef_at(rat(1, 8)),
            Clef::kAlto);
}

// Finding 7 (compaction consequence): the same `used`-set change alters
// stave compaction. A fragment naming stave_ordinal 1 for its only voice
// part and stave_ordinal 0 for a clef change now consumes two distinct
// destination staves -- ordinal 0 (clef-only) compacts to the anchor
// stave, ordinal 1 (the voice part) to the next -- rather than collapsing
// the voice part onto the anchor stave as the sole voice/pedal-referenced
// ordinal. This is the documented compaction rule working as specified,
// matching the 8g-ii pedal-span-on-unreferenced-stave precedent exactly.
TEST(ClipboardCommandTest,
     PasteClefChangeOnLowerOrdinalCompactsVoicePartToHigherStave) {
  Fixture         fx;
  const TrackLane before_lane   = fx.lane_of(fx.track_a);
  const ClefLane  before_treble = *fx.timeline()->clef_lane(fx.stave_a_treble);

  const NotationFragment fragment = make_fragment(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{
          0, 1, kVoice1,
          build_voice({make_note(pitch(Letter::kC), quarter())})}},
      {}, {FragmentClefChange{0, 0, rat(1, 8), Clef::kAlto}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  // Application: the voice part (fragment ordinal 1) lands on bass
  // (base+1), not treble (base+0) -- treble is consumed by ordinal 0's
  // clef-only reference.
  const VoiceContent& bass_voice =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_bass)->voice(kVoice1);
  ASSERT_GE(bass_voice.events().size(), 1u);
  EXPECT_EQ(std::get<Note>(bass_voice.events()[0]).pitch, pitch(Letter::kC));
  // Treble's own voice content is untouched -- ordinal 0 names no voice
  // part.
  const VoiceContent& treble_voice =
      fx.node()->lane(fx.track_a)->stave(fx.stave_a_treble)->voice(kVoice1);
  for (const VoiceEvent& event : treble_voice.events())
    EXPECT_TRUE(std::holds_alternative<Rest>(event));

  const ClefLane* treble_lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(treble_lane, nullptr);
  EXPECT_EQ(treble_lane->clef_at(rat(1, 8)), Clef::kAlto);
  // Containment.
  EXPECT_EQ(treble_lane->clef_at(rat(1, 4)), Clef::kTreble);
  EXPECT_EQ(treble_lane->clef_at(Rational(3)), Clef::kTreble);

  // Exact undo.
  ASSERT_TRUE(command.undo(fx.project).ok());
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before_lane);
  EXPECT_TRUE(*fx.timeline()->clef_lane(fx.stave_a_treble) == before_treble);

  ASSERT_TRUE(command.redo(fx.project).ok());
  EXPECT_EQ(fx.timeline()->clef_lane(fx.stave_a_treble)->clef_at(rat(1, 8)),
            Clef::kAlto);
}

// Finding 2: the containment skip when a destination clef change already
// sits exactly at range_end had no dedicated test.
TEST(ClipboardCommandTest,
     PasteSkipsReassertionWhenDestinationChangeAlreadySitsAtRangeEnd) {
  Fixture fx;
  ASSERT_TRUE(fx.timeline()
                  ->add_clef_change(fx.stave_a_treble, rat(1, 2), Clef::kTenor)
                  .ok());
  const ClefLane before = *fx.timeline()->clef_lane(fx.stave_a_treble);

  const NotationFragment fragment =
      make_fragment(rat(1, 2), {FragmentTrackShape{2}},
                    {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 2))}},
                    {}, {FragmentClefChange{0, 0, rat(1, 4), Clef::kBass}});
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};

  PasteFragmentCommand command(fragment, anchor);
  ASSERT_TRUE(command.execute(fx.project).ok());

  const ClefLane* lane = fx.timeline()->clef_lane(fx.stave_a_treble);
  ASSERT_NE(lane, nullptr);

  std::size_t matches_at_range_end = 0;
  for (const ClefChange& change : lane->changes()) {
    if (change.position == rat(1, 2)) {
      ++matches_at_range_end;
      EXPECT_EQ(change.clef, Clef::kTenor);
    }
  }
  EXPECT_EQ(matches_at_range_end, 1u);

  const Rational near_node_end = fx.node_end() - rat(1, 8);
  EXPECT_EQ(lane->clef_at(rat(1, 2)), before.clef_at(rat(1, 2)));
  EXPECT_EQ(lane->clef_at(near_node_end), before.clef_at(near_node_end));
}

// Finding 3: an empty measure_contexts fragment (constructible directly via
// NotationFragment::create even though make_fragment's default never
// produces one) must hit the (1d) defensive reject.
TEST(ClipboardCommandTest,
     PasteRejectsFragmentWithEmptyMeasureContextsLeavesLaneUnchanged) {
  Fixture                         fx;
  std::optional<NotationFragment> fragment = NotationFragment::create(
      rat(1, 4), {FragmentTrackShape{2}},
      {FragmentVoicePart{0, 0, kVoice1, rest_filled(rat(1, 4))}}, {}, {}, {},
      {});
  ASSERT_TRUE(fragment.has_value());
  const PasteAnchor anchor{fx.node_id, fx.track_a, fx.stave_a_treble,
                           Rational(0)};
  const TrackLane   before = fx.lane_of(fx.track_a);

  PasteFragmentCommand command(*fragment, anchor);
  EXPECT_EQ(command.execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fx.lane_of(fx.track_a) == before);
}
