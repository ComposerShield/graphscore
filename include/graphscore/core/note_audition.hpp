// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>

#include <graphscore/core/midi_pitch.hpp>
#include <graphscore/core/midi_velocity.hpp>
#include <graphscore/core/strong_id.hpp>

namespace graphscore {

// A request to briefly sound the pitches a score edit just made newly
// audible: the "short preview request" of docs/plan/05-notation-editor.md's
// note-entry deliverable ("Newly inserted or pitch-edited notes issue a
// short preview request; actual plugin audition is connected in Milestone
// 08"). This milestone produces the request as a value and nothing plays
// it; docs/plan/08-audio-vst.md's counterpart deliverable ("Newly inserted
// or pitch-edited notes produce a short fixed preview through the relevant
// track chain") is what eventually consumes one.
//
// -- Why this lives in graphscore_core --
// The eventual consumer is graphscore_writer_audio, which ADR 0003 assigns
// "note-preview insertion audition" to. Its permitted edges
// (cmake/architecture_contract.cmake) are graphscore_core, graphscore_domain,
// graphscore_scheduler, graphscore_audio_device, graphscore_vst3_host and
// graphscore_compiler -- notably NOT graphscore_notation, the target that
// produces this request from a pointer click
// (audition_for_note_entry, notation/graphscore_notation.hpp). A type
// declared in graphscore_notation would therefore be unreachable by the one
// target that must consume it. Every field below is already a
// graphscore_core type, and graphscore_core is a permitted edge of both the
// producer and the future consumer, so declaring the value type here adds no
// new architecture edge anywhere. This is the same reasoning that put
// playback_mapping.hpp, midi_pitch.hpp and midi_velocity.hpp in core rather
// than domain: playback concepts two layers must agree on belong in the
// layer both can see.
//
// -- Why there is no duration field --
// Milestone 08 specifies a short *fixed* preview. The audition's length is
// therefore a constant of the audio layer that renders it, not data this
// layer chooses per request: a notation-layer producer has no view of the
// device buffer size, the plugin's own release tail, or how long a preview
// may occupy the track chain before the next click arrives, and encoding a
// duration here would invite two layers to disagree about it. Nor is the
// audition's length the edited note's notated duration -- clicking a whole
// note is not a request to hold the preview for a whole note.
//
// -- Ordering and deduplication of `pitches` --
// `pitches` is the complete set of sounding pitches the audition should
// play together, in ascending MidiPitch order with no duplicates. Both
// properties are the producer's responsibility, not the consumer's.
// Deduplication is load-bearing rather than defensive: distinct
// SpelledPitch spellings deliberately collapse to the same MidiPitch
// (spelled_pitch.hpp), so adding C-sharp 4 to a chord that already contains
// D-flat 4 is a genuinely new *notated* pitch that is not a new *sounding*
// one, and sounding MidiPitch 61 twice in one audition would double its
// perceived level for no musical reason. An empty `pitches` is not a
// meaningful request; a producer returns no request at all rather than an
// empty one (see audition_for_note_entry).
struct NoteAuditionRequest {
  // The track whose chain the audition plays through -- Milestone 08 routes
  // the preview through "the relevant track chain", so the request names the
  // track rather than leaving the consumer to guess a monitoring default.
  TrackId track_id;

  // Ascending MidiPitch order, deduplicated. See "Ordering and
  // deduplication of `pitches`" above.
  std::vector<MidiPitch> pitches;

  // Applies uniformly to every pitch in `pitches`: the audition sounds them
  // together as one chord at this single velocity, not per-pitch. The
  // producer derives it (see audition_for_note_entry).
  MidiVelocity velocity;

  [[nodiscard]] bool operator==(const NoteAuditionRequest&) const = default;
};

}  // namespace graphscore
