// SPDX-License-Identifier: Apache-2.0

#include "clipboard_test_support.hpp"

#include <cassert>
#include <utility>
#include <vector>

namespace clipboard_test {

SpelledPitch pitch(Letter letter, std::int8_t octave) {
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

VoiceContent build_voice(std::vector<VoiceEvent> events) {
  VoiceContent voice;
  for (VoiceEvent& event : events) {
    const Result result = voice.append(std::move(event));
    assert(result.ok());
    (void)result;
  }
  return voice;
}

VoiceContent rest_filled(Rational length) {
  VoiceContent voice;
  const Result result = voice.normalize(length);
  assert(result.ok());
  (void)result;
  return voice;
}

std::vector<FragmentMeasureContext> default_measure_contexts() {
  return {FragmentMeasureContext{Rational(0), *TimeSignature::create(4, 4),
                                 *KeySignature::create(0)}};
}

NotationFragment make_fragment(
    Rational span, std::vector<FragmentTrackShape> tracks,
    std::vector<FragmentVoicePart>      parts,
    std::vector<FragmentPedalSpan>      pedal_spans,
    std::vector<FragmentClefChange>     clef_changes,
    std::vector<FragmentStaveContext>   stave_contexts,
    std::vector<FragmentMeasureContext> measure_contexts) {
  std::optional<NotationFragment> fragment = NotationFragment::create(
      span, std::move(tracks), std::move(parts), std::move(pedal_spans),
      std::move(clef_changes), std::move(stave_contexts),
      std::move(measure_contexts));
  assert(fragment.has_value());
  return std::move(*fragment);
}

namespace {

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

bool same_content_structure(const VoiceContent& a, const VoiceContent& b) {
  if (a.events().size() != b.events().size())
    return false;
  for (std::size_t i = 0; i < a.events().size(); ++i) {
    if (!same_event_structure(a.events()[i], b.events()[i]))
      return false;
  }
  return true;
}

}  // namespace

bool fragments_structurally_equal(const NotationFragment& a,
                                  const NotationFragment& b) {
  if (!(a.span_length() == b.span_length()) || a.tracks() != b.tracks() ||
      a.parts().size() != b.parts().size())
    return false;
  for (std::size_t i = 0; i < a.parts().size(); ++i) {
    const FragmentVoicePart& pa = a.parts()[i];
    const FragmentVoicePart& pb = b.parts()[i];
    if (pa.track_ordinal != pb.track_ordinal ||
        pa.stave_ordinal != pb.stave_ordinal || !(pa.voice == pb.voice) ||
        !same_content_structure(pa.content, pb.content))
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

Fixture::Fixture() {
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
  auto timeline_value = NodeTimeline::create(
      measures, {StaveDefinition{stave_a_treble, Clef::kTreble},
                 StaveDefinition{stave_a_bass, Clef::kBass},
                 StaveDefinition{stave_b, Clef::kTreble}});
  assert(timeline_value.has_value());
  node_p->set_timeline(std::move(*timeline_value));
  node_p->lane(track_a)->ensure_stave(stave_a_treble);
  node_p->lane(track_a)->ensure_stave(stave_a_bass);
  node_p->lane(track_b)->ensure_stave(stave_b);

  const Rational end = node_p->timeline()->node_end();
  for (const StaveId stave_id : {stave_a_treble, stave_a_bass}) {
    for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v)
      assign(track_a, stave_id, *Voice::create(v), rest_filled(end));
  }
  for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v)
    assign(track_b, stave_b, *Voice::create(v), rest_filled(end));
}

Node* Fixture::node() {
  return project.find_node(node_id);
}

NodeTimeline* Fixture::timeline() {
  return node()->timeline();
}

Rational Fixture::node_end() {
  return timeline()->node_end();
}

void Fixture::assign(TrackId track, StaveId stave_id, Voice voice,
                     VoiceContent content) {
  node()->lane(track)->stave(stave_id)->voice(voice) = std::move(content);
}

void Fixture::assign_and_complete(TrackId track, StaveId stave_id, Voice voice,
                                  std::vector<VoiceEvent> events) {
  VoiceContent content = build_voice(std::move(events));
  const Result result  = content.normalize(node_end());
  assert(result.ok());
  (void)result;
  assign(track, stave_id, voice, std::move(content));
}

TrackLane Fixture::lane_of(TrackId track) {
  return *node()->lane(track);
}

UnfilledFixture::UnfilledFixture() {
  const auto tid_a = project.add_track("A", StaffLayout::grand_staff(),
                                       *MidiChannel::create(0));
  assert(tid_a.has_value());
  track_a                  = *tid_a;
  const Track* track_a_ptr = project.find_active_track(track_a);
  assert(track_a_ptr != nullptr);
  stave_a_treble   = track_a_ptr->layout().staves()[0].id;
  const auto tid_b = project.add_track("B", StaffLayout::single_staff(),
                                       *MidiChannel::create(1));
  assert(tid_b.has_value());
  track_b                  = *tid_b;
  const Track* track_b_ptr = project.find_active_track(track_b);
  assert(track_b_ptr != nullptr);
  stave_b      = track_b_ptr->layout().staves()[0].id;
  node_id      = project.add_node("Node");
  Node* node_p = project.find_node(node_id);
  assert(node_p != nullptr);
  auto timeline_value = NodeTimeline::create(
      {Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}},
      {StaveDefinition{stave_a_treble, Clef::kTreble},
       StaveDefinition{stave_b, Clef::kTreble}});
  assert(timeline_value.has_value());
  node_p->set_timeline(std::move(*timeline_value));
  node_p->lane(track_a)->ensure_stave(stave_a_treble);
  node_p->lane(track_b)->ensure_stave(stave_b);
}

Node* UnfilledFixture::node() {
  return project.find_node(node_id);
}

Rational UnfilledFixture::node_end() {
  return node()->timeline()->node_end();
}

void UnfilledFixture::assign_note(TrackId track, StaveId stave_id, Voice voice,
                                  VoiceEvent event) {
  VoiceContent content;
  const Result append_result = content.append(event);
  assert(append_result.ok());
  (void)append_result;
  const Result normalize_result = content.normalize(node_end());
  assert(normalize_result.ok());
  (void)normalize_result;
  node()->lane(track)->stave(stave_id)->voice(voice) = std::move(content);
}

PedalOnlyFixture::PedalOnlyFixture() {
  const auto tid_a = project.add_track("A", StaffLayout::grand_staff(),
                                       *MidiChannel::create(0));
  assert(tid_a.has_value());
  track_a                  = *tid_a;
  const Track* track_a_ptr = project.find_active_track(track_a);
  assert(track_a_ptr != nullptr);
  stave_a_treble = track_a_ptr->layout().staves()[0].id;
  stave_a_bass   = track_a_ptr->layout().staves()[1].id;
  node_id        = project.add_node("N");
  Node* node_p   = project.find_node(node_id);
  assert(node_p != nullptr);
  std::vector<Measure> measures;
  for (std::size_t i = 0; i < kMeasureCount; ++i) {
    measures.push_back(
        Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  }
  auto timeline_value = NodeTimeline::create(
      measures, {StaveDefinition{stave_a_treble, Clef::kTreble},
                 StaveDefinition{stave_a_bass, Clef::kBass}});
  assert(timeline_value.has_value());
  node_p->set_timeline(std::move(*timeline_value));
}

Node* PedalOnlyFixture::node() {
  return project.find_node(node_id);
}

Rational PedalOnlyFixture::node_end() {
  return node()->timeline()->node_end();
}

}  // namespace clipboard_test
