// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// Materializes a currently-empty voice as a measure-aligned stream of
// normalized rests (decompose_measure_aligned_rests, voice_content.hpp),
// so a composer can start an entirely new rhythmic line in a voice that
// has never held anything. This is the "explicit voice-stream workflow":
// a stave always carries four structural VoiceContent slots
// (StaveVoices::voice), but an untouched slot is empty until something
// fills it, and pointer/keyboard note entry only ever operates on an
// existing event boundary -- an empty voice has none. This command is
// that missing boundary-creation step, applied in the same undoable
// action as the composer's first click (see make_note_entry_command,
// graphscore_notation).
//
// Fails, leaving the project unchanged, if:
//   - the node/track/stave named by the constructor is not owned by
//     `project`, or the node has no NodeTimeline;
//   - the target voice is not empty (events() is non-empty) -- this
//     command creates a stream, it never replaces or extends one;
//   - decompose_measure_aligned_rests cannot express the timeline's
//     [0, node_end()) span exactly (never a partial fill);
//   - the resulting candidate fails VoiceContent::validate() or
//     validate_voice_references() (notation_validation.hpp) -- both run on
//     every execute(), the same as redo() below, even though a candidate
//     built purely from append(Rest) carries no markings today and so
//     trivially passes validate_voice_references(); the check stays in
//     place so a future richer fill can never silently bypass it.
//
// Reversibility: execute() snapshots the voice's pre-edit (empty) and
// post-edit (fully rest-filled) states, exactly like SetEventCommand and
// ConvertEventToRestCommand. undo() restores the voice to completely
// empty -- zero events, not rests -- so the voice returns to the same
// "never touched" state a composer who never clicked would have seen.
// redo() reproduces the exact same Rest ids as the original execute() by
// replaying the captured post-edit snapshot rather than recomputing the
// fill, so a redo after an intervening undo can never mint different ids
// than the ones any other command (e.g. a SetEventCommand chained after
// this one in a CommandTransaction) already captured a reference to.
// execute() asserts candidate.check_complete(timeline->node_end()) before
// publishing, symmetric with redo()'s own assertion of the same fact on
// the replayed snapshot -- so a future bug in
// decompose_measure_aligned_rests that produced a fill not exactly tiling
// node_end() would be caught atomically on the very first execute(),
// never only surfacing later on a redo.
class CreateVoiceStreamCommand : public Command {
 public:
  CreateVoiceStreamCommand(NodeId node_id, TrackId track_id, StaveId stave_id,
                           Voice voice)
      : node_id_(node_id),
        track_id_(track_id),
        stave_id_(stave_id),
        voice_(voice) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId  node_id_;
  TrackId track_id_;
  StaveId stave_id_;
  Voice   voice_;

  // Saved pre-edit state for undo restoration: always an empty VoiceContent,
  // captured rather than default-constructed so undo can stale-context-check
  // it the same way every sibling command does.
  std::optional<VoiceContent> pre_snapshot_;
  // Saved post-edit state (the exact rest fill), verified before undo to
  // reject stale context and replayed verbatim by redo().
  std::optional<VoiceContent> post_snapshot_;
  State                       state_ = State::kFresh;
};

}  // namespace graphscore
