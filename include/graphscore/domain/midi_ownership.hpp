// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// This header's file-level overview -- same-pitch newest-owner retrigger,
// logical-OR CC64 pedal ownership, and the lifecycle rules that remove
// note/pedal ownership (docs/plan/README.md's locked decisions;
// docs/plan/02-domain-model.md's "Adaptive playback semantics" section,
// bullets 2 and 3) -- spanning every declaration below (MidiOwnerRegion
// through MidiOwnershipTracker), not only the one immediately following it:
//
//   "If overlapping material attacks the same track/channel/pitch, emit
//   note-off then the newer note-on; the newer attack owns the eventual
//   note-off and the older logical note's later release is suppressed."
//   "Overlapping CC64 pedal spans combine as logical OR per track/channel;
//   pedal-up emits only after the last active logical span releases."
//   "Source-main notes currently owning MIDI pitches receive note-offs at
//   the jump. Concurrent pickdown tails remain active."
//   "[Pickdown notes] emit MIDI only, and survive later active-node
//   transitions until naturally complete."
//   "Sustain pedal is the sole pre-general-CC exception and emits MIDI
//   CC64."
//   "Effect tails decay naturally; a panic action cuts notes and resets
//   processing immediately."
//
// This class models logical ownership STATE only -- a toolkit-independent
// domain model, not MIDI I/O, an audio device, or a plugin (docs/plan/
// 02-domain-model.md's own acceptance bar). It never emits, schedules, or
// times a MIDI message; it tells a caller (a future writer/runtime
// playback engine, wired up in a later phase) exactly which ownership
// transitions just happened, so that caller can emit the corresponding
// note-on/note-off/CC64 messages. Every mutating method's return value IS
// that authoritative transition record -- this class has no other output.
//
// -- Keying: per (track/channel, pitch), resolved explicitly by the caller --
// Note ownership is keyed on (MidiChannel, MidiPitch) (core/midi_channel.hpp,
// core/midi_pitch.hpp); pedal ownership on (MidiChannel, NotationEntityId)
// where the NotationEntityId is the PedalSpan's own `id`
// (notation_markings.hpp) -- reusing that existing stable identity rather
// than minting a second one. Neither key ever mentions TrackId or StaveId
// directly, even though the product prose says "per track/channel" and
// PedalSpan is stored stave-scoped (TrackLane::add_pedal_span,
// track.hpp): a Track's MidiChannel is shared by every stave in that
// track's StaffLayout (track.hpp's Track::channel() is fixed once for the
// whole track, independent of how many staves it has), so "per
// track/channel" and "per channel" are the identical partition once a
// caller has resolved Track::channel() for the stave/track a note or
// PedalSpan actually belongs to -- this class takes that already-resolved
// MidiChannel, never a TrackId/StaveId, so it never needs its own
// Project/Track lookup to do so. Keying purely on MidiChannel also means
// two different Tracks configured with the identical channel value (legal
// -- Track::channel() carries no project-wide uniqueness requirement)
// correctly compete for the same physical MIDI channel's pitch/pedal
// space, exactly as a real MIDI device would see them: this is intended,
// not an oversight.
//
// -- Same-pitch newest-owner retrigger --
// attack_note() records a fresh, uniquely identified MidiAttackId as the
// new owner of (channel, pitch). If that pitch already had an owner, the
// old owner's MidiAttackId is returned as `superseded` -- "emit note-off
// then the newer note-on" is the caller's job at that exact call site,
// immediately, using the two ids this method hands back (old: note-off;
// new: note-on) -- this class never defers or schedules that pair itself.
// From that instant, the OLD MidiAttackId is no longer the current owner
// of its pitch, so a later release_note() call against it (typically
// driven by the old logical note's own originally notated end, arriving
// after the retrigger already cut it short) finds the ownership entry
// already reassigned and reports MidiNoteReleaseOutcome::kSuppressed
// rather than kEmitsNoteOff -- exactly "the older logical note's later
// release is suppressed", modeled explicitly and queryably through this
// return value rather than as an implicit side effect a caller could get
// wrong. release_note() is idempotent and never fails: an attack id this
// class has never seen, or has already released (by a prior release_note()
// call or by any lifecycle method below), reports kSuppressed uniformly --
// there is exactly one reason release_note() ever emits a note-off
// (`attack_id` is still literally the current owner of its pitch) and
// every other case is, from the caller's perspective, identically "do not
// emit anything", whether the underlying reason was a newer attack or an
// intervening lifecycle clear.
//
// -- Ownership spans main material and pickdown tails simultaneously --
// Every owned pitch/pedal-span entry also carries a MidiOwnerScope: which
// region (kMain or kPickdownTail) attacked/depressed it, and which node
// (main) or which PickdownTailSnapshotId (tail) it belongs to. A tail and
// the node that becomes active at the same boundary can therefore hold
// concurrent ownership of DIFFERENT pitches on the same channel with no
// interaction at all -- this class does not special-case that; it is a
// direct consequence of keying by (channel, pitch), not by node/region --
// and if they ever DO attack the identical (channel, pitch), the
// same-pitch newest-owner rule above applies uniformly regardless of which
// side is older or newer, matching pickdown_ownership.hpp's "may contain
// new attacks" and this file's own governing prose above, which draws no
// region distinction either.
//
// -- Logical-OR CC64 pedal ownership --
// pedal_down()/pedal_up() key each logical pedal span (a PedalSpan's own
// `id`, notation_markings.hpp) into a per-channel active set. Because more
// than one span can be simultaneously active on one channel (an
// overlapping main-region span and a concurrent tail's span, or simply two
// overlapping spans within the same region), the channel is "down" -- in
// MIDI terms, warrants CC64 == 127 -- while that set is non-empty, and
// "up" -- CC64 == 0 -- only once it becomes empty again: this is the
// logical OR. pedal_down() reports MidiPedalTransition::kBecameActive only
// on the 0->1 transition (the first depress on that channel; a caller
// emits CC64 down exactly then, never on every depress) and pedal_up()
// reports kBecameInactive only on the N->0 transition (a caller emits
// CC64 up exactly then). Every other call reports kNoChange: the pedal
// was already down (another span keeps it down) or the released span was
// never tracked as active (already released, by pedal_up() or by any
// lifecycle method below) -- both idempotent, never an error.
//
// -- Boundary transfer: crossing notes/pedals move from main to tail --
// docs/plan/README.md: "Notes/ties crossing the transition boundary
// transfer their post-boundary logical ownership to the pickdown tail."
// pickdown_ownership.hpp's NoteOwnership{main_part, pickdown_tail_part}
// models exactly this at the notation level for one logical note that
// began before a boundary and continues past it (an already-crossing
// chain), explicitly deferring "the actual clearing rules that DO
// eventually end tail ownership" to this file (pickdown_ownership.hpp's
// own words, describing this exact increment).
// transfer_main_to_pickdown_tail(source_node, snapshot) below is that
// consequence: at a sequential boundary, every note/pedal span this
// tracker still records as MidiOwnerScope::main(source_node) is, by
// definition, still sounding past the boundary -- had it already ended,
// release_note()/pedal_up() would already have removed its entry -- so
// it IS the crossing material, with no need for a caller to separately
// enumerate which entries cross: a bulk re-scope of the whole set to
// MidiOwnerScope::pickdown_tail(snapshot), in one call, is exactly right.
// This is deliberately NOT a second attack: a tied note must never emit
// a second note-on at the boundary, so this method only rewrites which
// scope an already-current MidiAttackId/pedal span belongs to --
// attack_note()/pedal_down() are never called by it, it never appears in
// a MidiOwnershipRelease, and it emits no MIDI of its own. It exists
// because neither alternative a caller could otherwise choose at
// attack_note() time is correct for the note's whole life: attacking as
// main(source_node) makes a later vertical_jump(source_node) wrongly cut
// a note that has already survived the boundary -- notably including the
// cycle case (README.md: "[on a cycle] the pickdown continues sounding
// into the next iteration, overlapping the start of its own main
// region"), where source_node becoming active again and firing a
// vertical jump against itself is exactly the scenario this method
// exists to get right; attacking as pickdown_tail(snapshot) instead
// wrongly survives a vertical jump taken BEFORE the boundary, when the
// note was still ordinary main material. Calling this method once,
// immediately after begin_pickdown_tail() mints `snapshot` for
// `source_node`, is what makes both true at the times they must be true:
// main-scoped and vulnerable to vertical_jump(source_node) before the
// boundary, tail-scoped and immune to it after.
//
// -- Lifecycle: what removes ownership, and what does not --
// docs/plan/02-domain-model.md: "Lifecycle rules remove note and pedal
// ownership on vertical jump, stop, reset, node play, panic, and snapshot
// retirement; pause retains ownership." Four public methods below cover
// every listed trigger except pause, each returning a MidiOwnershipRelease
// recording exactly which notes/pedal channels the caller must now emit
// note-offs/CC64-ups for:
//
//   - vertical_jump(source_node): docs/plan/README.md's "Source-main notes
//     currently owning MIDI pitches receive note-offs at the jump.
//     Concurrent pickdown tails remain active." -- releases only
//     MidiOwnerScope::kMain entries whose node is `source_node`; every
//     MidiOwnerScope::kPickdownTail entry, regardless of which node
//     originated it, is left untouched. This class does not itself track
//     "the currently active node" -- it only ever sees whatever
//     MidiOwnerScope a caller supplied at attack_note()/pedal_down() time
//     -- so it is a caller obligation (mirroring several such obligations
//     already documented on EventStateMachine, event_state_machine.hpp)
//     that a MidiOwnerScope::main(node) is only ever attached while `node`
//     genuinely is the active main node; this class has no Project/Graph
//     access with which to verify that itself. The product prose is
//     explicit about notes; this class extends the identical main/tail
//     split to pedal ownership too, since docs/plan/02-domain-model.md's
//     lifecycle bullet lists "note and pedal ownership" together for every
//     trigger in this list, including this one, without distinguishing
//     them -- a source-main sustain pedal span is main-region material
//     exactly as a source-main note is, and a pedal span that has crossed
//     into a pickdown tail is exactly as much "survive[s] later
//     active-node transitions until naturally complete" material as a
//     tied note that has. The plan-bullet grouping is suggestive, not
//     decisive on its own; the decisive argument is musical: a
//     source-main pedal span left down across a jump has no reachable
//     release event -- its notated end lies in a node that is no longer
//     playing -- so under README.md's "pedal-up emits only after the
//     last active logical span releases", CC64 would stay at 127 for the
//     remainder of playback, blurring the destination node and
//     everything that follows it. Cutting it at the jump costs only a
//     pedal shorter than notated at the jump instant, exactly the cost
//     README.md already accepts for notes at a vertical jump ("Target
//     notes that began before the mapped position are not chased").
//     Leaving TAIL pedal spans active remains mandatory under README.md's
//     pickdown prose above ("[on a cycle] the pickdown continues
//     sounding into the next iteration" / "survive later active-node
//     transitions until naturally complete").
//   - clear_all(): stop, transport reset, and activating a node's play
//     button (node-play) all call this -- mirroring EventStateMachine::
//     clear_all()'s own identical three-way mapping and its documented
//     reasoning (event_state_machine.hpp's "Transport clearing" section):
//     each is, in effect, "start over from a defined point", including
//     "clears tails/queues" (docs/plan/README.md's node-play sentence,
//     verbatim) -- unlike vertical_jump() above, this releases EVERY
//     tracked note and pedal span, both regions, every node and snapshot,
//     unconditionally. It also forgets every open snapshot's source node:
//     after clear_all(), pickdown_tail_source_node() reports nullopt for
//     every PickdownTailSnapshotId ever minted by begin_pickdown_tail(),
//     matching that query's own "unknown or has already been retired"
//     contract below -- a snapshot's tail material no longer exists past
//     a clear_all(), so neither should the record of where it came from.
//   - panic(): Adam's ruling (product decision) -- "Since panic refers to
//     the audio processing itself, which is only in one place, and by
//     extension the transport, this is global. It clears audio processing,
//     stops it, stops the transport." At this class's scope, panic's
//     note/pedal-ownership effect is identical to clear_all()'s (every
//     tracked entry, both regions, and every open snapshot's source node
//     forgotten too) -- nothing here further distinguishes
//     them, since clear_all() already clears tails per node-play's own
//     "clears tails/queues" wording. panic() is exposed as its own,
//     separately named entry point (rather than being a mere alias a
//     caller invokes by calling clear_all()) because it is its own
//     distinct product-level transport action, strictly BROADER than
//     clear_all() once the concerns this class does not own are included:
//     stopping the transport itself and clearing EventStateMachine's own
//     queues/manual-queue/vertical-jump-guard state (event_state_machine.
//     hpp) are a caller's separate responsibility alongside calling this
//     method -- this class has no transport or EventStateMachine access
//     with which to do either itself. "Effect tails decay naturally; a
//     panic action cuts notes and resets processing immediately"
//     (docs/plan/README.md) is exactly the distinction motivating a
//     separate method at all: ordinary tail completion is
//     retire_pickdown_tail_snapshot() below, not panic().
//   - retire_pickdown_tail_snapshot(snapshot): Adam's ruling -- "snapshot
//     retirement" is the ordinary, non-error end of one pickdown tail's
//     life, when its material finishes sounding naturally (contrast with
//     panic() above, which cuts a tail short instead). begin_pickdown_tail
//     (below) mints the PickdownTailSnapshotId a caller attaches to every
//     MidiOwnerScope::pickdown_tail() attack/pedal-down belonging to that
//     one tail instance, at "a sequential destination starts when the
//     pickdown starts" (docs/plan/README.md); this method releases
//     "whatever note/pedal ownership that snapshot still holds" (this
//     phase's brief) -- typically little to nothing, since most of a
//     tail's material will already have released itself individually via
//     ordinary release_note()/pedal_up() calls as its own notated
//     durations elapse; this method is what cleans up whatever remains
//     once the tail, as a whole, is done. Idempotent and never fails: an
//     unknown or already-retired snapshot yields an empty
//     MidiOwnershipRelease, exactly like clearing an already-empty
//     EventQueue (event_queue.hpp).
//
// Pause has no corresponding method here, by the same design
// EventStateMachine documents for its own queues ("pause = no call"): no
// method on this class may ever be invoked for a pause action. Every
// attack/pedal-down this class currently tracks, and every
// PickdownTailSnapshotId still open, remains exactly as it was when
// playback resumes.
//
// -- Phase 7c normative same-sample resolution and serialization --
// This section specifies the future scheduler's MIDI 1.0 output at one
// sample. It is deliberately a contract, not an API on this class:
// MidiOwnershipTracker has no timestamp, TrackId, TrackIndex, stave, or
// notation traversal context. The scheduler owns that context, calls this
// tracker for physical ownership state, and attaches the ordering metadata
// described here to every pending attack, span, and generated release. The
// caller-owned runtime output retains its stable TrackId/TrackIndex, but
// neither UUID lexical order nor any unordered-container iteration is an
// ordering source.
//
// Only note-on, note-off, and CC64 are in this 0.1.0 contract. General MIDI
// CC is explicitly out of scope; Milestone 12 extends this order with its
// own message classes and must not silently treat another CC as CC64.
//
// A scheduled note attack or pedal-span operation has a strict source order.
// Its first component is playback origin: open pickdown tails come first, in
// increasing scheduler-owned tail-creation ordinal (older before newer),
// followed by the active main material. Tail snapshot IDs are opaque and
// MUST NOT be compared for this purpose. Within one origin, order by
// TrackIndex ascending and then by the position in that Track's
// StaffLayout::staves() vector.
//
// Note attacks next order by Voice 1 through 4 and VoiceContent::events()
// vector index. At an event with attached grace groups, all grace attacks sort
// before the principal attack. Grace attacks order by the matching group's
// VoiceContent::grace_groups() vector index and then by GraceGroup::notes
// vector index; sounding MIDI pitch is the final tie-breaker. A principal Note
// has notehead index zero; a principal Chord orders noteheads by sounding MIDI
// pitch ascending and then Chord::notes vector index. The cooker records this
// whole tuple as a unique source ordinal. Thus a grace attack precedes its
// principal attack even if both quantize to one sample, and repeated same-pitch
// grace notes still have a unique order.
//
// Pedal-span operations instead order by the span's exact `start`, then by its
// index in TrackLane::pedal_spans(stave). The per-stave span vector position is
// stable source order; the unordered map that locates a stave is not. These
// rules produce a unique final ordinal for every cooked note attack and pedal
// span. No source-order component is a UUID, random value, hash value, or
// incidental unordered-container position.
//
// A sequential boundary creates a new tail after any older open tails, so
// its newly scheduled tail material follows those tails in source order. Its
// destination is active main material and follows every tail. Likewise, a
// vertical target is active main material. Consequently, a destination
// attack wins a same-(channel,pitch) collision with tail material, while a
// newer tail wins one with an older tail. This source/ownership order is not
// the final byte-stream order below.
//
// The scheduler applies the M04 same-sample phase order before resolving
// MIDI ownership: it ingests input against the start-of-sample active node;
// a selected vertical jump releases source-main ownership, enters its target,
// and suppresses source-boundary work; otherwise it enqueues sequential
// occurrences, resolves a boundary, creates/transfers a tail, and enters the
// selected destination. A vertical source release is therefore resolved
// before target attacks. A sequential transfer calls
// transfer_main_to_pickdown_tail() without output: it is a pure scope change,
// never a note-off, note-on, or CC64 operation. Tail due events and
// destination due events at that same sample then use the origin order above.
// Stop, reset, node-play, panic, and tail retirement contribute lifecycle
// releases to this same sample-level resolution; they do not bypass it.
//
// Resolution is atomic for the sample. First collect all natural note/span
// ends, graph/lifecycle releases, and candidate attacks/span starts. Retain
// source metadata with an attack for its full lifetime, so a natural release,
// a vertical/lifecycle release, and the retrigger note-off of that attack
// inherit the original material order. Every operation also carries this
// scheduler phase rank, in ascending order:
//
//   0. stop/reset/panic/node-play clear_all() release;
//   1. vertical_jump() source-main release;
//   2. ordinary scheduled natural note or span end;
//   3. retire_pickdown_tail_snapshot() cleanup release;
//   4. retrigger release generated by a retained attack; and
//   5. retained scheduled note attack or pedal-span start.
//
// These ranks are established by phase traversal, not discovery order in a
// map. Stop, reset, and panic are quiescent lifecycle actions: rank 0 flushes
// ownership and no later rank occurs at that sample. Node-play performs its
// rank 0 flush and then admits rank 5 attacks at the new node's start.
// A process sample never performs two rank-0 actions. A selected vertical jump
// performs rank 1 before target rank-5 attacks and suppresses source-main
// rank-2 ends and all source-boundary work, while independent tail ranks 2-3
// remain eligible. Without a vertical jump, scheduled ends precede retirement
// cleanup and attacks.
//
// Before collision resolution, discard any candidate whose quantized sounded
// end sample is not later than its attack sample. Such a zero-sample attack is
// silent: it never calls attack_note(), emits neither note-on nor note-off, and
// cannot displace an existing owner. This is only an output-quantization rule;
// it does not alter the event's exact Rational score duration.
//
// For each physical (MidiChannel, MidiPitch), collapse the remaining
// same-sample attack candidates before calling attack_note(): the last
// candidate in source order is the sole winner, and every earlier candidate is
// discarded as an attack. A release belonging to a discarded, never-owned
// candidate is suppressed. Next apply natural and lifecycle releases of the
// start-of-sample owner in phase-rank order. A release emits only when that
// attack is still the current owner; duplicate, displaced, and otherwise stale
// releases are suppressed. Finally, if the retained winner replaces an owner
// that remains current, emit that old owner's rank-4 note-off and make the
// winner current. If the old owner already released at this sample, the winner
// attacks after that earlier note-off instead. Thus a key has at most one
// resolved note-off and one resolved note-on at a sample, and an attack's
// note-off can never be serialized before its own note-on. This is the required
// use of MidiOwnershipTracker's newest-owner/suppressed-release semantics, not
// a post-hoc sort of independently emitted messages.
//
// Resolve pedal spans as a per-channel set over the entire sample, including
// natural span ends, lifecycle clearing, tail transfer, and new depressions.
// For each channel compare the logical-OR state before this atomic resolution
// with the state after it. Emit no CC64 when they are equal, CC64 127 exactly
// on false->true, and CC64 0 exactly on true->false. There is at most one
// relevant CC64 message per channel per sample. In particular, releasing a
// transferred/source span while another tail or destination span depresses at
// the same sample must not produce an up/down glitch. Duplicate starts and
// stale ends retain the tracker's idempotent semantics. For ordering metadata,
// a down transition inherits the first retained activating span in source
// order. An up transition inherits the final effective removal when removals
// are ordered by source order and then the exact phase rank above. Those
// operations, including lifecycle removals, retain their spans' original
// source metadata and phase rank.
//
// Serialize only after this ownership resolution, in exactly three message
// classes: all resolved note-offs, then all resolved CC64 messages, then all
// resolved note-ons. Within each class, sort by retained source order, then by
// the exact phase rank above, and finally by MIDI channel and pitch (CC64 uses
// channel and controller 64). The unique source ordinal plus the final MIDI
// fields make this a strict total order, including lifecycle flushes spanning
// material from different nodes. Retrigger note-offs carry the displaced
// attack's metadata, lifecycle note-offs carry the released attack's metadata,
// and a CC64 transition carries the causal span metadata just defined.
// Therefore no generated message is left to an arbitrary traversal order, even
// when tracks legally share one physical MIDI channel. Ownership arbitration
// remains channel/pitch-only in MidiOwnershipTracker; the scheduler, not this
// class, supplies TrackId/TrackIndex and all ordering metadata.
enum class MidiOwnerRegion : std::uint8_t {
  kMain = 0,
  kPickdownTail,
};

namespace detail {
struct MidiAttackIdTag {};

struct PickdownTailSnapshotIdTag {};
}  // namespace detail

// An opaque, globally unique handle minted by attack_note() below,
// identifying exactly one note attack for the lifetime of the ownership it
// may or may not still hold. Carries no ordering meaning of its own (unlike
// EventOccurrence::arrival_ordinal(), event_occurrence.hpp, which recency
// arbitration depends on) -- current ownership is looked up by (channel,
// pitch), never by comparing two MidiAttackIds against each other.
using MidiAttackId = StrongId<detail::MidiAttackIdTag>;

// An opaque, globally unique handle minted by
// MidiOwnershipTracker::begin_pickdown_tail() below, identifying one
// pickdown tail's entire lifetime from the boundary where it starts
// sounding through retire_pickdown_tail_snapshot() (this header's overview
// above).
using PickdownTailSnapshotId = StrongId<detail::PickdownTailSnapshotIdTag>;

// Which node/tail an owned note or pedal span belongs to (this header's
// overview above, "Ownership spans main material and pickdown tails
// simultaneously"). Constructed only through main()/pickdown_tail() below.
// region() reports which alternative is actually held; exactly one of
// main_node()/tail_snapshot() is ever non-nullopt for a given instance.
class MidiOwnerScope {
 public:
  [[nodiscard]] static MidiOwnerScope main(NodeId node) noexcept {
    return MidiOwnerScope(node);
  }

  [[nodiscard]] static MidiOwnerScope pickdown_tail(
      PickdownTailSnapshotId snapshot) noexcept {
    return MidiOwnerScope(snapshot);
  }

  [[nodiscard]] MidiOwnerRegion region() const noexcept {
    return std::holds_alternative<NodeId>(value_)
               ? MidiOwnerRegion::kMain
               : MidiOwnerRegion::kPickdownTail;
  }

  [[nodiscard]] std::optional<NodeId> main_node() const noexcept {
    const auto* node = std::get_if<NodeId>(&value_);
    return node != nullptr ? std::optional(*node) : std::nullopt;
  }

  [[nodiscard]] std::optional<PickdownTailSnapshotId> tail_snapshot()
      const noexcept {
    const auto* snapshot = std::get_if<PickdownTailSnapshotId>(&value_);
    return snapshot != nullptr ? std::optional(*snapshot) : std::nullopt;
  }

  [[nodiscard]] bool operator==(const MidiOwnerScope&) const = default;

 private:
  explicit MidiOwnerScope(
      std::variant<NodeId, PickdownTailSnapshotId> value) noexcept
      : value_(value) {}

  std::variant<NodeId, PickdownTailSnapshotId> value_;
};

// One attack_note() call's result: the new MidiAttackId, now the current
// owner of (channel, pitch), and, if a different attack owned that pitch a
// moment before, its MidiAttackId as `superseded` -- see this header's
// overview above ("Same-pitch newest-owner retrigger") for exactly what a
// caller must do with each id.
struct MidiNoteAttack {
  MidiAttackId                attack_id;
  std::optional<MidiAttackId> superseded;

  [[nodiscard]] bool operator==(const MidiNoteAttack&) const = default;
};

// release_note()'s result -- see this header's overview above for exactly
// when each value is returned.
enum class MidiNoteReleaseOutcome : std::uint8_t {
  kEmitsNoteOff = 0,
  kSuppressed,
};

// pedal_down()/pedal_up()'s result -- see this header's overview above
// ("Logical-OR CC64 pedal ownership") for exactly when each value is
// returned.
enum class MidiPedalTransition : std::uint8_t {
  kNoChange = 0,
  kBecameActive,
  kBecameInactive,
};

// One note-off a lifecycle method's MidiOwnershipRelease says the caller
// must now emit.
struct MidiReleasedNote {
  MidiChannel  channel;
  MidiPitch    pitch;
  MidiAttackId attack_id;

  [[nodiscard]] bool operator==(const MidiReleasedNote&) const = default;
};

// One channel whose sustain pedal a lifecycle method's MidiOwnershipRelease
// says the caller must now emit CC64 == 0 for -- reported only on the
// genuine N->0 transition (this header's overview above), never once per
// released span.
struct MidiReleasedPedal {
  MidiChannel channel;

  [[nodiscard]] bool operator==(const MidiReleasedPedal&) const = default;
};

// The authoritative record of exactly what a lifecycle method just
// released -- see this header's overview above ("Lifecycle: what removes
// ownership, and what does not"). Either vector may be empty; both empty
// is a legal, idempotent "nothing was owned to release" result, not an
// error. `notes` is sorted by (channel, pitch) and `pedals` by channel
// before being returned, deterministically, regardless of the tracker's
// internal (hash-map) iteration order -- a caller comparing two
// MidiOwnershipRelease values, or simply replaying one deterministically,
// never observes run-to-run reordering of otherwise-identical releases.
// This ordering is additionally a strict total order over what is
// actually released, not merely a deterministic one, so there are no ties
// for std::sort to break arbitrarily: `notes` is keyed by (channel, pitch)
// in the tracker's own note_owners_ map, so no two released
// MidiReleasedNotes can ever compare equal by (channel, pitch); and a
// release pushes at most one MidiReleasedPedal per channel per call (only
// on the true N->0 transition), so no two released MidiReleasedPedals can
// ever compare equal by channel either. A future reader must not switch
// either sort to std::stable_sort under the impression that ties exist
// and need stabilizing -- none do.
struct MidiOwnershipRelease {
  std::vector<MidiReleasedNote>  notes;
  std::vector<MidiReleasedPedal> pedals;

  [[nodiscard]] bool operator==(const MidiOwnershipRelease&) const = default;
};

// The normative MIDI note/pedal ownership tracker itself; see this
// header's overview above for the full specification this class
// implements.
class MidiOwnershipTracker {
 public:
  MidiOwnershipTracker() = default;

  // Mints a fresh PickdownTailSnapshotId for a tail beginning on
  // `source_node`; see this header's overview above
  // (retire_pickdown_tail_snapshot()). Every attack_note()/pedal_down()
  // call belonging to this one tail instance should be scoped with
  // MidiOwnerScope::pickdown_tail(the returned id).
  [[nodiscard]] PickdownTailSnapshotId begin_pickdown_tail(NodeId source_node);

  // Re-scopes every MidiOwnerScope::main(source_node) note/pedal-span
  // entry this tracker still holds to MidiOwnerScope::pickdown_tail(
  // snapshot); see this header's overview above ("Boundary transfer:
  // crossing notes/pedals move from main to tail"). Call once,
  // immediately after begin_pickdown_tail() mints `snapshot` for
  // `source_node`. Emits nothing of its own -- ownership moves in place;
  // no note-on, note-off, or CC64 message is implied, and this method
  // never appears in a MidiOwnershipRelease.
  //
  // It is a caller obligation (mirroring vertical_jump()'s own above, and
  // several such obligations already documented on EventStateMachine,
  // event_state_machine.hpp) that `snapshot` is the live
  // PickdownTailSnapshotId begin_pickdown_tail(source_node) most recently
  // minted for `source_node`, and that this method is called at most once
  // per snapshot: this class has no Project/Graph access with which to
  // verify that pairing itself. A second call, or a `source_node` that
  // currently owns no main-scoped entry at all, is a harmless no-op --
  // there is simply nothing left to re-scope. But passing an unknown or
  // already-retired `snapshot` is not harmless: the re-scoped entries are
  // silently orphaned. They stop being reachable by a vertical_jump()
  // against `source_node` (they are tail-scoped now, not main-scoped), and
  // they were never reachable by retire_pickdown_tail_snapshot() for this
  // call either, since that call for this id already happened or never
  // will. From that point on the only way to ever release them is
  // retire_pickdown_tail_snapshot() called again with that exact stale id,
  // or clear_all()/panic() -- otherwise they are exactly the stuck note /
  // stuck CC64-at-127 outcome this header's overview above argues a
  // source-main pedal span left dangling at a vertical jump would cause.
  void transfer_main_to_pickdown_tail(NodeId                 source_node,
                                      PickdownTailSnapshotId snapshot);

  // The node begin_pickdown_tail() minted `snapshot` for, or nullopt if
  // `snapshot` is unknown or has already been retired. Intended for tests
  // and diagnostics.
  [[nodiscard]] std::optional<NodeId> pickdown_tail_source_node(
      PickdownTailSnapshotId snapshot) const;

  // Records a fresh attack on (channel, pitch); see this header's overview
  // above ("Same-pitch newest-owner retrigger") for the exact retrigger
  // semantics.
  [[nodiscard]] MidiNoteAttack attack_note(MidiChannel channel, MidiPitch pitch,
                                           MidiOwnerScope scope);

  // Releases `attack_id`'s ownership if it is still current; see this
  // header's overview above for exactly when kEmitsNoteOff vs kSuppressed
  // is returned. Never fails.
  [[nodiscard]] MidiNoteReleaseOutcome release_note(MidiAttackId attack_id);

  // The MidiAttackId currently owning (channel, pitch), or nullopt if
  // unowned. Intended for tests and diagnostics.
  [[nodiscard]] std::optional<MidiAttackId> current_note_owner(
      MidiChannel channel, MidiPitch pitch) const;

  // Marks pedal span `span_id` (a PedalSpan's own `id`,
  // notation_markings.hpp) active on `channel`; see this header's overview
  // above ("Logical-OR CC64 pedal ownership"). A span already recorded
  // active under the identical (channel, span_id) is a no-op, reported as
  // kNoChange -- calling this twice for the same span never double-counts
  // it into the logical-OR set. That no-op silently KEEPS the original
  // scope, even if `scope` here differs from the one first passed:
  // re-scoping an already-active span is transfer_main_to_pickdown_tail()
  // below's job, not a second pedal_down() call with a different scope --
  // calling pedal_down() again to "re-depress with tail scope" is a
  // silent no-op, not a transfer.
  [[nodiscard]] MidiPedalTransition pedal_down(MidiChannel      channel,
                                               NotationEntityId span_id,
                                               MidiOwnerScope   scope);

  // Marks pedal span `span_id` inactive on `channel`. A span not currently
  // recorded active under (channel, span_id) -- never depressed, already
  // released, or released via a lifecycle method below -- is a no-op,
  // reported as kNoChange.
  [[nodiscard]] MidiPedalTransition pedal_up(MidiChannel      channel,
                                             NotationEntityId span_id);

  // True while at least one logical pedal span keeps `channel`'s sustain
  // pedal down. Intended for tests and diagnostics.
  [[nodiscard]] bool pedal_active(MidiChannel channel) const noexcept;

  // Vertical-jump lifecycle clearing; see this header's overview above.
  [[nodiscard]] MidiOwnershipRelease vertical_jump(NodeId source_node);

  // Stop/reset/node-play lifecycle clearing; see this header's overview
  // above.
  [[nodiscard]] MidiOwnershipRelease clear_all();

  // Panic lifecycle clearing; see this header's overview above.
  [[nodiscard]] MidiOwnershipRelease panic();

  // Snapshot-retirement lifecycle clearing; see this header's overview
  // above.
  [[nodiscard]] MidiOwnershipRelease retire_pickdown_tail_snapshot(
      PickdownTailSnapshotId snapshot);

 private:
  struct NoteKey {
    MidiChannel channel;
    MidiPitch   pitch;

    [[nodiscard]] bool operator==(const NoteKey&) const = default;
  };

  // A perfect hash: MidiChannel is [0, 15] and MidiPitch is [0, 127], so
  // packing them into disjoint low/high byte ranges can never collide.
  struct NoteKeyHash {
    [[nodiscard]] std::size_t operator()(const NoteKey& key) const noexcept {
      return (static_cast<std::size_t>(key.channel.value()) << 8) |
             static_cast<std::size_t>(key.pitch.value());
    }
  };

  struct OwnedNote {
    MidiAttackId   attack_id;
    MidiOwnerScope scope;
  };

  struct PedalKey {
    MidiChannel      channel;
    NotationEntityId span_id;

    [[nodiscard]] bool operator==(const PedalKey&) const = default;
  };

  struct PedalKeyHash {
    [[nodiscard]] std::size_t operator()(const PedalKey& key) const noexcept {
      const std::size_t span_hash = std::hash<NotationEntityId>{}(key.span_id);
      return span_hash ^ (static_cast<std::size_t>(key.channel.value()) +
                          0x9e3779b9U + (span_hash << 6) + (span_hash >> 2));
    }
  };

  // Releases every owned note/pedal entry matching the filter this call
  // describes: `release_all` unconditionally matches everything
  // (clear_all()/panic()); otherwise exactly one of `main_node_filter`/
  // `tail_snapshot_filter` is set and matches only entries of the
  // corresponding MidiOwnerRegion carrying that exact node/snapshot
  // (vertical_jump()/retire_pickdown_tail_snapshot()).
  [[nodiscard]] MidiOwnershipRelease release_where(
      bool release_all, std::optional<NodeId> main_node_filter,
      std::optional<PickdownTailSnapshotId> tail_snapshot_filter);

  std::unordered_map<NoteKey, OwnedNote, NoteKeyHash> note_owners_;
  std::unordered_map<MidiAttackId, NoteKey>           attack_lookup_;
  std::unordered_map<PedalKey, MidiOwnerScope, PedalKeyHash>
      active_pedal_spans_;
  std::array<std::size_t, static_cast<std::size_t>(MidiChannel::kMax) + 1>
                                                     pedal_active_count_{};
  std::unordered_map<PickdownTailSnapshotId, NodeId> snapshot_source_node_;
};

}  // namespace graphscore
