// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/notation_event.hpp>

namespace graphscore {

class VoiceContent;

// One addressable note/chordnote notehead in a tie chain: the event it
// lives in, the notehead index within that event, and its
// (chain-invariant) pitch.
struct ChainNotehead {
  std::size_t  event_index = 0;
  std::size_t  note_index  = 0;
  SpelledPitch pitch;

  [[nodiscard]] bool operator==(const ChainNotehead&) const = default;
};

// The number of addressable noteheads in `event`: 1 for a Note, the note
// count for a Chord, 0 for a Rest.  This is the exact index space
// build_tie_chain and notehead_id_at address, so a caller enumerating a
// chord column's noteheads never has to inspect the variant itself.
[[nodiscard]] std::size_t notehead_count(const VoiceEvent& event);

// The persistent id of the notehead at `note_index` in `event` (a Note's
// own id, or the addressed ChordNote's id), or std::nullopt when the
// address is out of range or the event is a Rest.
[[nodiscard]] std::optional<NotationEntityId> notehead_id_at(
    const VoiceEvent& event, std::size_t note_index);

// Builds the complete tie chain through the notehead addressed by
// (`event_index`, `note_index`) in `voice`.  That address must name a Note
// or ChordNote notehead -- i.e. `note_index < notehead_count(events[
// event_index])` -- never a GraceNote, which has no tied_to_next field and
// so never participates in a chain.
//
// A tie edge connects notehead (e, n) whose tied_to_next is set to every
// notehead (e+1, m) in the immediately following event at the same pitch;
// the same edge is traversed backward from (e+1, m) to (e, n). The chain is
// the connected component of the addressed notehead under these edges,
// visited with exact-address deduplication so duplicate-pitch chords never
// re-visit a member or stop at the first equal pitch instead of the one that
// carries the tie. Returns the chain in ascending (event, notehead) order.
// A dangling tie (a tied_to_next flag with no matching following pitch)
// yields only the members a valid walk reaches; VoiceContent::validate later
// rejects that state.  An out-of-range address yields an empty chain.
//
// A notehead that ties to nothing is a chain of exactly one member (itself),
// so callers can treat "chain size == 1" as "not tied" without a separate
// query.
//
// This walk is public domain surface (a cross-target header under
// include/) rather than a src/domain detail because both the notehead-editing
// commands (MoveNoteheadCommand, StepAccidentalCommand -- a pitch edit must
// carry the whole chain with it) and the notation layer's own keyboard staff
// step (selection_after_staff_step, M5-phase-24 -- which resolves a landing
// notehead visually along a chain) need exactly one implementation of it.
[[nodiscard]] std::vector<ChainNotehead> build_tie_chain(
    const VoiceContent& voice, std::size_t event_index, std::size_t note_index);

}  // namespace graphscore
