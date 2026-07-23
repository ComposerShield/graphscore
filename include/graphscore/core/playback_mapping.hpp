// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <graphscore/core/articulation.hpp>
#include <graphscore/core/dynamic.hpp>
#include <graphscore/core/grace_note_type.hpp>
#include <graphscore/core/midi_velocity.hpp>
#include <graphscore/core/rational.hpp>

namespace graphscore {

// This header's file-level overview -- the Phase 7 "normative playback
// specification" deliverable for articulation/dynamic/grace mapping
// (docs/plan/02-domain-model.md: "Enumerate articulation mappings and
// precedence with slurs, ties, dynamics, and hairpins" and "Specify
// grace-group steal fraction/limits, multi-note division, behavior without
// a preceding sounded note, and interaction with articulations") -- spans
// every declaration below, not only the one immediately following it.
//
// -- Why this lives in graphscore_core --
// Placed in core rather than domain for the same reason DeterministicPrng
// (core/deterministic_prng.hpp) and tempo_curve.hpp are: the ADR 0003
// runtime closure (graphscore_runtime and everything it depends on) can see
// graphscore_core but never graphscore_domain, and the writer and runtime
// must be able to reproduce identical velocity/duration mappings from the
// same Dynamic/Articulation/GraceNoteType data at playback time. This is
// also why Articulation, Dynamic, and GraceNoteType themselves were
// relocated into graphscore_core alongside this header (from
// graphscore/domain/articulation.hpp, graphscore/domain/dynamic.hpp, and a
// nested enum in graphscore/domain/notation_markings.hpp respectively) --
// this module needs to take them as parameters directly, and core cannot
// depend on domain (ADR 0003), mirroring exactly how TempoPoint/
// TempoSegmentKind moved into core in Phase 7a.
//
// -- Exact Rational only: no floating point --
// Unlike tempo_curve.hpp, this module has no product-approved exception to
// this project's "never floating point for score structure" rule
// (AGENTS.md). MIDI velocities are small bounded integers (MidiVelocity,
// [0, 127]) and every duration this module touches is an exact whole-note
// Rational; every function below stays exact on Rational/MidiVelocity
// boundaries end to end, including the hairpin interpolation's rounding
// step (see "Rounding a Rational velocity" below).
//
// -- Scope: math only, not context resolution --
// Every function below is a pure function of already-resolved inputs: a
// governing Dynamic, a pair of already-known hairpin endpoint velocities,
// an already-known gap to the next note-on, and so on. Deciding WHICH
// Dynamic/Hairpin/Slur governs an arbitrary point in a real timeline --
// walking a TrackLane's voices to find the nearest preceding
// DynamicMarking, or the Hairpin/Slur whose [start_event, end_event] span
// contains a given event -- is domain/runtime wiring, out of scope here,
// exactly the same "spec the math now, wire the traversal later" boundary
// Adam drew for Phase 7c's simultaneous-MIDI-ordering deliverable, applied
// narrowly to this one resolution step. graphscore/domain/
// notation_playback.hpp is the thin domain-layer wiring that applies this
// module's functions to already-loaded Note/Chord/GraceGroup values; it
// does not walk TrackLane/VoiceContent either -- resolving governing
// context across a timeline remains deferred to a future runtime/scheduler
// phase in both layers.
//
// == Dynamics and hairpins: base velocity ==
//
// -- The eight-level dynamic table --
// velocity_for_dynamic() maps each of the eight standard Dynamic levels
// (core/dynamic.hpp: ppp through fff) to a MidiVelocity via
// `kDynamicVelocityTable`, indexed by the enum's own underlying value. The
// table is a roughly even linear spread across the legal [0, 127] range,
// deliberately not spanning the full range at either end: ppp (16) is kept
// clearly audible rather than at the silent floor (0), since a "very soft"
// marking still specifies a sounding note, not a rest, while fff (127)
// sits at the legal ceiling, the natural choice for "as loud as the format
// allows." The seven inter-level steps (16,17,16,15,16,16,16,15 as
// consecutive differences) are chosen to be as close to a single constant
// step (127-16)/7 ~ 15.86 as integer values allow, so no dynamic level is
// disproportionately emphasized relative to its neighbors -- a deliberately
// conservative, unsurprising choice given Adam has final numerical
// sign-off later and nothing downstream consumes these numbers yet.
//
// -- Hairpin interpolation and its rounding rule --
// interpolate_hairpin_velocity() linearly interpolates between two already
// -resolved endpoint velocities `from`/`to` (the hairpin's start/end
// Dynamic, already mapped through velocity_for_dynamic() by the caller, or
// any other already-known pair of endpoint velocities) at exact fraction
// `position` through the span: `from + (to - from) * position`, computed
// entirely in exact Rational arithmetic, `position` clamped to [0, 1]
// first (see "Clamping, not failing" below). The Rational result is then
// rounded to the nearest legal MidiVelocity integer with round-half-to-even
// tie-breaking -- the same IEEE-754-default convention
// tempo_curve.hpp's round_to_sample_count() documents and uses, chosen for
// the identical reason: it is the standard, unbiased tie-break, and unlike
// round_to_sample_count()'s `double` input, exact Rational arithmetic makes
// "exactly half" a well-defined, exactly representable case (e.g.
// interpolating from velocity 60 to 61 at position 1/2 lands on the exact
// value 121/2, not a `double` approximation of it), so this module can and
// does perform the tie-break exactly rather than merely approximately, by
// comparing `2 * remainder` against the denominator with plain integer
// arithmetic (see playback_mapping.cpp's round_velocity()).
//
// -- Clamping, not failing --
// `position` outside [0, 1] is clamped to the nearest bound rather than
// producing an error or an unspecified result: this function's whole
// contract is "answer a velocity for this position along the span",
// and extrapolating past a hairpin's own notated endpoints is not a
// meaningful request for this module to refuse -- the boundary velocity is
// always a well-defined, safe answer. This mirrors round_to_sample_count()
// and invert_elapsed_seconds()'s own preference for a well-defined
// clamped/saturated answer over an error path for out-of-domain input.
//
// -- Accent/marcato precedence: apply_emphasis() --
// A note/chord may carry accent, marcato, both, or neither
// (core/articulation.hpp's Articulation set is per-event, not mutually
// exclusive the way the three duration articulations are). This module's
// rule: marcato and accent do NOT stack -- marcato, the stronger emphasis,
// takes precedence outright whenever both are present, rather than
// summing both boosts. `kMarcatoVelocityBoost` (24) is chosen larger than
// `kAccentVelocityBoost` (16) specifically so the two remain
// distinguishable when compared against `kDynamicVelocityTable`'s own
// ~16-unit inter-level step: accent reads as "about one dynamic level
// louder", marcato as "louder still, roughly one and a half levels", a
// legible, proportioned pair of boosts rather than two arbitrary numbers.
// The combined result saturates at MidiVelocity::kMax (127) rather than
// wrapping or asserting, the same saturate-not-overflow convention
// round_to_sample_count() uses for its own out-of-range input.
//
// == Articulation, slur, and tie: sounded duration ==
//
// -- Duration-articulation ratios --
// sounded_duration_for_articulation() maps a notated Rational duration and
// an optional duration-articulation (std::nullopt, or one of the three
// is_duration_articulation() (core/articulation.hpp) markings -- kStaccato,
// kStaccatissimo, kTenuto; kAccent/kMarcato never reach this function,
// since they affect only velocity via apply_emphasis()) to the note's
// actual sounded (audible, MIDI note-on-to-note-off) duration, via
// `kDefaultSoundedDurationRatio`/`kStaccatoSoundedDurationRatio`/
// `kStaccatissimoSoundedDurationRatio`/`kTenutoSoundedDurationRatio`:
//
//   no articulation (std::nullopt): 7/8  -- a conventional "detache"
//     default. Two structurally adjacent, unarticulated notes in this
//     model's VoiceContent occupy back-to-back notated time with no
//     rest between them; sounding a plain note for its FULL notated
//     value would leave zero gap before the next note's onset, audibly
//     indistinguishable from a tie even though none is notated. A
//     conventional playback default therefore holds a plain note for a
//     high fraction of its notated value, not all of it, to keep
//     consecutive notes audibly separated -- the same default-gap
//     convention common notation-to-MIDI playback engines use.
//   kStaccato: 1/2 -- the textbook "half value" staccato convention.
//   kStaccatissimo: 1/4 -- shorter than staccato ("as short as possible"),
//     a plain halving of staccato's own ratio for a legible, proportioned
//     pair rather than two unrelated numbers.
//   kTenuto: 1/1 (the full notated value, no shortening) -- tenuto means
//     "held": distinct from the no-articulation default specifically by
//     NOT leaving the default's small separation gap, giving tenuto a
//     real, audible effect versus a plain unarticulated note rather than
//     being indistinguishable from it.
//
// -- Tie-boundary suppression (Adam's ruling: 2f) --
// `is_tied` (true when this event ties INTO a following event -- a Note's
// own `tied_to_next` flag, or a Chord's per-notehead ChordNote::
// tied_to_next, per notation_event.hpp) suppresses duration-articulation
// shortening entirely: whenever `is_tied` is true, this function returns
// `notated_duration` unchanged, ignoring `duration_articulation` outright,
// regardless of which of the three shortening markings (if any) is
// present. Rationale: a tied note's notated duration is not its whole
// audible span -- the tie continues sounding into the following event, so
// shortening THIS event's own notated slice would carve an audible gap
// mid-note, defeating the tie entirely. The articulation's effect belongs
// at the LAST event of the tie chain (`is_tied == false` there, since
// nothing ties out of it), where the shortening finally applies to
// whatever notated slice actually precedes the note's real note-off.
// Resolving tie-CHAIN membership (which event is genuinely "the last" of a
// chain spanning several tied events) is the caller's job -- see
// graphscore/domain/pickdown_ownership.hpp's tied_note_spans() for an
// existing example of exactly this kind of tie-chain resolution elsewhere
// in the domain layer, referenced here for context only; this module does
// not depend on it and a caller is not required to use it. A simple
// per-event boolean is sufficient for this function's own contract.
// Tenuto interacts with this rule inertly: since kTenuto already returns
// the full notated value even when NOT tied, tie-suppression never changes
// a tenuto note's own result, only a staccato-family one's.
//
// -- Legato (slur) overlap and its precedence over shortening --
// legato_sounded_duration() extends a note's sounded duration fully into
// the gap before the next event's onset: `articulated_duration +
// gap_to_next_onset`, eliminating the gap entirely so the note sounds
// continuously up to (never past) the following onset -- the real overlap
// a slur denotes, as distinct from a mere absence of a rest between
// notated events. `gap_to_next_onset` is caller-resolved (see "Scope: math
// only" above); this function's own precondition is `gap_to_next_onset >=
// Rational(0)` (a negative gap describes overlapping notated events, not a
// meaningful request this function is asked to interpret).
//
// Precedence versus duration-articulation shortening AND tie-boundary
// suppression (the plan's "precedence" deliverable, stated explicitly): a
// slur governing a note's outgoing edge overrides BOTH duration-
// articulation shortening AND tie-suppression ENTIRELY, not partially.
// graphscore/domain/notation_playback.hpp's wiring enforces this by NOT
// calling sounded_duration_for_articulation() at all for a slurred note --
// it passes the note's raw notated duration directly as
// `articulated_duration` here, bypassing the three shortening ratios
// above, the no-articulation 7/8 default, AND the `is_tied` flag alike (a
// slurred note has no reason to carry the ordinary detache gap either,
// since the slur already guarantees legato continuity). Tenuto is
// unaffected either way, since tenuto's own ratio is already 1/1 --
// identical to the raw notated value this bypass supplies.
//
// This IS a real conflict, resolved deliberately, not a case that "never
// conflicts in practice": a note that is BOTH tied AND slurred AND marked
// staccato sounds for `notated_duration + gap_to_next_onset` (the full
// legato-overlap result computed by legato_sounded_duration()) -- its
// `is_tied` flag and its staccato marking are BOTH silently ignored for
// that note's own sounded-duration computation. Slur wins outright because
// it is treated as the "loudest" playback instruction of the three: an
// audibly continuous legato connection -- the entire point of notating a
// slur -- must not be interrupted by a shortening articulation, nor
// deferred to tie-suppression logic that was designed for a different
// case (a tied note's audible span continuing into a SEPARATE following
// event via a real MIDI tie, not a slur's own overlap into the next
// onset's attack). A caller passing `is_tied` alongside a
// `slurred_gap_to_next_onset` must not expect `is_tied` to matter --
// graphscore/domain/notation_playback.hpp's event_sounded_duration() doc
// comment states this explicitly, since `is_tied` is a named parameter of
// that function and a caller could otherwise be surprised it is silently
// discarded in this combination.
//
// == Grace-note steal ==
//
// -- Acciaccatura vs appoggiatura, and the steal cap --
// grace_steal_durations() returns, in the group's own note order, the
// stolen Rational duration for each of `note_count` grace notes preceding
// `principal_event`, stolen from `available_duration` (the sounded
// duration of the note that precedes the grace group, BEFORE any of that
// note's own duration-articulation is applied -- see "Composition order
// with articulation" below). Each grace note's kind sets a fixed
// per-note steal fraction of `available_duration`:
//
//   kAcciaccatura: `kAcciaccaturaPerNoteStealFraction` = 1/16 per note --
//     "crushed", played as close to instantaneous as this model allows.
//     A single acciaccatura therefore steals a small sliver (1/16) of the
//     preceding note; see "Multi-note division" below for how additional
//     notes in one group scale this.
//   kAppoggiatura: `kAppoggiaturaPerNoteStealFraction` = 1/6 per note -- a
//     more substantial, deliberately audible share, three times an
//     acciaccatura's per-note fraction, without approaching the cap on
//     its own for an ordinary (note_count == 1) appoggiatura.
//
// The total steal (summed across all `note_count` notes) is
// `per_note_fraction * note_count`, of `available_duration`, capped at
// `kMaxGraceStealFraction` = 1/2: `total = min(per_note_fraction *
// note_count, kMaxGraceStealFraction) * available_duration`. This
// guarantees the preceding note always keeps at least half its own
// duration (see grace_steal_remaining_duration() below), satisfying the
// "must never consume the entire preceding note" requirement with an
// explicit, generous minimum remainder rather than an infinitesimal one.
// When the proposed (uncapped) total would exceed the cap, EVERY note's
// share is scaled down proportionally by dividing the same capped total
// among the group using its own division rule (below) rather than only
// trimming the last note or leaving the excess on one note -- keeping the
// group's own relative shape (front-loaded or even) intact under the cap,
// just smaller overall.
//
// -- Multi-note division --
// Acciaccatura groups front-load: note i (0-indexed) of an n-note group
// receives geometric weight `1/2^(i+1)` for i in [0, n-2], and the LAST
// note (index n-1) receives the remaining `1/2^(n-1)`, so weights sum to
// exactly 1 (a standard finite-geometric-series identity) for any n from 1
// up to `kMaxGraceNotesPerGroup` -- see "Grace-group size bound"
// immediately below for the exact, still-exactly-summing-to-1 rule this
// module uses past that bound, and why an unqualified "for any n >= 1"
// claim here would be false. This mirrors real acciaccatura-group
// performance practice: the whole crushed run is played as one quick
// gesture immediately before the beat, with each successive note claiming
// half of what remains of that gesture's own brief allotment, not a share
// that grows with the group's note count. Appoggiatura groups instead
// divide evenly (`1/n` each) for any n >= 1 -- unlike acciaccatura's
// geometric halving, even division constructs no bit-shift and needs no
// analogous bound -- reflecting their more deliberate, individually
// weighted character versus acciaccatura's front-loaded crush: an n-note
// appoggiatura group is n roughly equally-weighted notes sharing the
// group's total steal, not one dominant note and several vanishingly
// brief ones.
//
// -- Grace-group size bound --
// `kMaxGraceNotesPerGroup` (16) bounds the DEPTH of acciaccatura's
// geometric halving, not the legal value of `note_count` itself (this
// module accepts any `note_count`, including values past the bound).
// Constructing weight `1/2^(i+1)` for an unbounded `i` shifts a 64-bit
// integer by an unbounded exponent -- undefined behavior once the exponent
// reaches 64, reproduced by a 70-note acciaccatura group under UBSan
// before this bound existed. For `note_count <= kMaxGraceNotesPerGroup`,
// acciaccatura weighting is exactly the halving rule described above. For
// `note_count > kMaxGraceNotesPerGroup`, the halving rule applies only
// through depth `kMaxGraceNotesPerGroup` -- weights `1/2, 1/4, ...,
// 1/2^(kMaxGraceNotesPerGroup - 1)` for the first `kMaxGraceNotesPerGroup -
// 1` notes -- and EVERY remaining note beyond that depth shares the
// smallest slot, `1/2^(kMaxGraceNotesPerGroup - 1)`, EQUALLY among
// themselves, rather than continuing to halve without limit. This keeps
// the largest shift exponent this module ever constructs fixed at
// `kMaxGraceNotesPerGroup - 1` (15) regardless of how large `note_count`
// actually is, and the weights still sum to exactly 1 in both regimes --
// the shape degenerates past the bound, but the total never does. 16 was
// chosen because acciaccatura groups notating double digits of
// simultaneous grace notes are already musically unusual (single digits
// are the overwhelming norm); low tens comfortably covers every genuinely
// notated case this format needs to represent, well clear of a magnitude
// where the geometric shape -- each note claiming half of what's left of
// an already-brief gesture -- would stop being musically meaningful
// anyway.
//
// Every weight in both regimes above is strictly positive by construction,
// so no grace note in a group with `available_duration > 0` (see "No
// preceding sounded note" immediately below for the other case) is ever
// assigned exactly zero duration: a zero-duration grace note would have no
// meaningful playback distinction from the note simply not existing, which
// this construction never produces.
//
// -- No preceding sounded note --
// `available_duration <= Rational(0)` signals there is nothing to steal
// from (no sounded note precedes the grace group, e.g. it opens a voice).
// This function's contract for that case is a fixed, absolute fallback,
// NOT a steal from the following principal event's own duration: every one
// of the `note_count` grace notes receives the same fixed
// `kGraceFallbackNoteDuration` (1/32 of a whole note), with no acciaccatura
// -vs-appoggiatura distinction and no cap logic (there is no
// "available_duration" quantity to divide or cap against). Stealing from
// the FOLLOWING principal event instead was considered and rejected for
// this module's signature: it would require this function to also accept
// the principal event's own notated duration as a second budget, which
// this specification's chosen shape (matching the plan's suggested
// signature) does not carry, and would entangle this one function with
// two independent budgets and two independent cap rules instead of one.
// The fixed fallback keeps the function's contract self-contained: given
// only a type, a count, and a single duration budget (which may be
// non-positive to mean "no budget"), it always returns a well-defined,
// bounded answer.
//
// -- Composition order with articulation --
// grace_steal_remaining_duration() returns the preceding note's OWN
// remaining sounded duration after the theft: conceptually
// `available_duration - sum(grace_steal_durations(...))` when
// `available_duration > 0` (guaranteed strictly positive by the 1/2 cap
// above), or `available_duration` unchanged when it was already <= 0
// (nothing was stolen, so nothing changes). The IMPLEMENTATION computes
// this in closed form -- `available_duration * (1 - total_fraction)`,
// where `total_fraction` is the same capped total fraction
// grace_steal_durations() distributes across the group -- rather than
// materializing the per-note vector and summing it, since the per-note
// weights are constructed to sum to exactly `total_fraction` by
// construction (see "Multi-note division" and "Grace-group size bound"
// above): this is a real optimization, not merely an equivalent
// rephrasing, since a summation loop over `note_count` `Rational` values
// risks overflowing `Rational`'s denominator arithmetic well before
// `note_count` reaches even `kMaxGraceNotesPerGroup` (reproduced by a
// 32-note acciaccatura group under UBSan before this closed form
// existed), while the closed form performs a fixed, small number of
// `Rational` operations regardless of `note_count`. The explicit
// composition order this module
// specifies: grace-note theft is applied to the PRECEDING note's own
// available-duration BUDGET first, and duration-articulation
// (sounded_duration_for_articulation()) is applied SECOND, to whatever
// budget theft left behind -- i.e. a caller computes the preceding note's
// final sounded duration as sounded_duration_for_articulation(
// grace_steal_remaining_duration(...), preceding_note's own articulation,
// preceding_note's own is_tied), not the other way around. Theft shrinks
// the notated TIME available before articulation ever asks "what fraction
// of this time should sound"; articulation then answers that question
// against whatever time remains, exactly as it would for any other
// notated duration. The grace notes' OWN durations
// (grace_steal_durations()'s return values) are never run back through
// sounded_duration_for_articulation() themselves -- this model has no
// duration-articulation field on GraceNote (core/grace_note_type.hpp only
// records acciaccatura/appoggiatura kind), only on the principal Note/
// Chord and on ordinary voice events. The PRINCIPAL event's own
// articulation, symmetrically, is entirely unaffected by any of this: it
// applies via sounded_duration_for_articulation() to the principal event's
// own notated duration exactly as if no grace group preceded it at all --
// grace-note theft only ever touches the note that comes BEFORE the grace
// group, never the principal event the group leads into.
inline constexpr std::array<std::uint8_t, 8> kDynamicVelocityTable = {
    16, 33, 49, 64, 80, 96, 112, 127};

inline constexpr std::uint8_t kAccentVelocityBoost  = 16;
inline constexpr std::uint8_t kMarcatoVelocityBoost = 24;

inline constexpr Rational kDefaultSoundedDurationRatio =
    Rational(7) / Rational(8);
inline constexpr Rational kStaccatoSoundedDurationRatio =
    Rational(1) / Rational(2);
inline constexpr Rational kStaccatissimoSoundedDurationRatio =
    Rational(1) / Rational(4);
inline constexpr Rational kTenutoSoundedDurationRatio = Rational(1);

inline constexpr Rational kAcciaccaturaPerNoteStealFraction =
    Rational(1) / Rational(16);
inline constexpr Rational kAppoggiaturaPerNoteStealFraction =
    Rational(1) / Rational(6);
inline constexpr Rational kMaxGraceStealFraction = Rational(1) / Rational(2);
inline constexpr Rational kGraceFallbackNoteDuration =
    Rational(1) / Rational(32);

// Bounds the DEPTH of acciaccatura's geometric halving in
// grace_steal_durations() -- not the legal value of note_count itself, see
// this header's overview, "Grace-group size bound", for the exact
// degenerate-shape rule this module uses for note_count past this bound
// and the rationale for 16.
inline constexpr std::size_t kMaxGraceNotesPerGroup = 16;

// The base MIDI note-on velocity for a governing Dynamic marking. See this
// header's overview, "The eight-level dynamic table", for the table's
// values and rationale. Total for all eight Dynamic values.
[[nodiscard]] MidiVelocity velocity_for_dynamic(Dynamic dynamic) noexcept;

// Exact-Rational linear interpolation between two already-resolved
// hairpin/dynamic-span endpoint velocities `from` (position 0) and `to`
// (position 1), at exact fraction `position`, rounded to the nearest legal
// MidiVelocity with round-half-to-even tie-breaking. `position` outside
// [0, 1] is clamped, not rejected. See this header's overview, "Hairpin
// interpolation and its rounding rule" and "Clamping, not failing", for
// the exact rounding rule and why out-of-range input is clamped rather
// than refused. Resolving WHICH Hairpin/DynamicMarking governs an
// arbitrary timeline point, and what `from`/`to`/`position` should
// therefore be, is caller/domain/runtime wiring, out of scope here (see
// this header's overview, "Scope: math only, not context resolution") --
// this function is deliberately agnostic about how its three arguments
// were determined; only the interpolation arithmetic itself is this
// function's concern.
[[nodiscard]] MidiVelocity interpolate_hairpin_velocity(
    MidiVelocity from, MidiVelocity to, Rational position) noexcept;

// Combines a base velocity (from velocity_for_dynamic()/
// interpolate_hairpin_velocity(), or any other already-resolved base) with
// accent and/or marcato emphasis, saturating at MidiVelocity::kMax. See
// this header's overview, "Accent/marcato precedence: apply_emphasis()",
// for the non-stacking precedence rule (marcato wins outright over accent
// when both are present) and the two boost constants' rationale.
[[nodiscard]] MidiVelocity apply_emphasis(MidiVelocity base, bool accent,
                                          bool marcato) noexcept;

// The actual sounded (audible) duration of a note/chord whose notated
// whole-note length is `notated_duration`, given at most one duration
// articulation (`duration_articulation`; kAccent/kMarcato never reach this
// function -- see core/articulation.hpp's is_duration_articulation()) and
// whether this event ties into a following event (`is_tied`). See this
// header's overview, "Duration-articulation ratios" and "Tie-boundary
// suppression", for the four ratios (no articulation, staccato,
// staccatissimo, tenuto) and the tie-suppression rule
// (`is_tied == true` returns `notated_duration` unchanged, ignoring
// `duration_articulation` entirely).
[[nodiscard]] Rational sounded_duration_for_articulation(
    Rational                    notated_duration,
    std::optional<Articulation> duration_articulation, bool is_tied) noexcept;

// Extends `articulated_duration` (the result of
// sounded_duration_for_articulation(), or a note's raw notated duration
// when a slur overrides shortening -- see this header's overview, "Legato
// (slur) overlap and its precedence over shortening") fully into the gap
// before the next event's onset: `articulated_duration +
// gap_to_next_onset`. Precondition: `gap_to_next_onset >= Rational(0)`.
[[nodiscard]] Rational legato_sounded_duration(
    Rational articulated_duration, Rational gap_to_next_onset) noexcept;

// The stolen Rational duration for each of `note_count` grace notes of
// kind `type`, in order, stolen from `available_duration` (the preceding
// sounded note's own available duration BEFORE that note's own
// articulation is applied -- see this header's overview, "Composition
// order with articulation"). `available_duration <= Rational(0)` signals
// no preceding sounded note; see this header's overview, "No preceding
// sounded note", for the fixed-fallback behavior that branch takes.
// `note_count == 0` returns an empty vector unconditionally. `note_count`
// itself is never rejected, including values past
// `kMaxGraceNotesPerGroup` -- see this header's overview, "Acciaccatura vs
// appoggiatura, and the steal cap", "Multi-note division", and
// "Grace-group size bound", for the per-note fractions, the 1/2 cap, the
// front-loaded (acciaccatura) versus even (appoggiatura) division rule,
// and the degenerate-but-still-safe shape acciaccatura's weighting takes
// past `kMaxGraceNotesPerGroup`.
[[nodiscard]] std::vector<Rational> grace_steal_durations(
    GraceNoteType type, std::size_t note_count, Rational available_duration);

// The preceding note's own remaining sounded duration after
// grace_steal_durations() steals from it: conceptually `available_duration
// - sum(grace_steal_durations(type, note_count, available_duration))` when
// `available_duration > Rational(0)` (always strictly positive by the 1/2
// cap), or `available_duration` unchanged otherwise (nothing was stolen).
// Computed in exact closed form with no summation loop over `note_count`
// values -- see this header's overview, "Composition order with
// articulation", for why this is a genuine overflow-avoidance measure, not
// just a rephrasing, and why this remaining duration -- not
// `available_duration` itself -- is what a caller should feed to
// sounded_duration_for_articulation() as the preceding note's own
// `notated_duration`.
[[nodiscard]] Rational grace_steal_remaining_duration(
    GraceNoteType type, std::size_t note_count, Rational available_duration);

}  // namespace graphscore
