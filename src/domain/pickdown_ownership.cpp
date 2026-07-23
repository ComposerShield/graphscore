// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/pickdown_ownership.hpp>

#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

namespace {

struct ActiveTie {
  SpelledPitch pitch;
  std::size_t  chain_index;
};

// Extends `active`'s chain for `pitch` if one exists (updating its span's
// end to `span.end` and returning its index), otherwise starts a fresh
// chain in `result` beginning at `event_index`/`span` and returns its
// new index.
std::size_t extend_or_start_chain(std::vector<TiedNoteSpan>&    result,
                                  const std::vector<ActiveTie>& active,
                                  const SpelledPitch&           pitch,
                                  std::size_t                   event_index,
                                  const MusicalSpan&            span) {
  for (const ActiveTie& tie : active) {
    if (tie.pitch == pitch) {
      result[tie.chain_index].span.end = span.end;
      return tie.chain_index;
    }
  }
  result.push_back(TiedNoteSpan{event_index, pitch, span});
  return result.size() - 1;
}

}  // namespace

std::vector<TiedNoteSpan> tied_note_spans(const std::vector<VoiceEvent>& events,
                                          Rational start) {
  std::vector<TiedNoteSpan> result;
  std::vector<ActiveTie>    active;

  Rational position = start;
  for (std::size_t index = 0; index < events.size(); ++index) {
    const VoiceEvent& event    = events[index];
    const Rational    duration = event_duration(event).resolved();
    const MusicalSpan span{position, position + duration};

    if (std::holds_alternative<Rest>(event)) {
      active.clear();
      position = span.end;
      continue;
    }

    std::vector<ActiveTie> next_active;

    if (const auto* note = std::get_if<Note>(&event)) {
      const std::size_t chain_index =
          extend_or_start_chain(result, active, note->pitch, index, span);
      if (note->tied_to_next)
        next_active.push_back(ActiveTie{note->pitch, chain_index});
    } else if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const ChordNote& notehead : chord->notes) {
        const std::size_t chain_index =
            extend_or_start_chain(result, active, notehead.pitch, index, span);
        if (notehead.tied_to_next)
          next_active.push_back(ActiveTie{notehead.pitch, chain_index});
      }
    }

    active   = std::move(next_active);
    position = span.end;
  }

  return result;
}

NoteOwnership classify_note_ownership(const NodeTimeline& timeline,
                                      const TiedNoteSpan& note) {
  const SpanClassification classification = timeline.classify(note.span);
  return NoteOwnership{note, classification.main_part,
                       classification.pickdown_part};
}

std::vector<NoteOwnership> classify_voice_ownership(
    const NodeTimeline& timeline, const std::vector<VoiceEvent>& events,
    Rational start) {
  std::vector<NoteOwnership> result;
  for (const TiedNoteSpan& note : tied_note_spans(events, start))
    result.push_back(classify_note_ownership(timeline, note));
  return result;
}

}  // namespace graphscore
