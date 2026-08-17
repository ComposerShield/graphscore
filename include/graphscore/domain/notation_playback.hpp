// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/notation_markings.hpp>

namespace graphscore {

class VoiceContent;

// Thin domain-layer wiring over graphscore/core/playback_mapping.hpp: these
// functions extract already-attached articulation/duration/grace-note
// context from this domain's existing Note/Chord/GraceGroup structures and
// call straight into the core math. The overloads taking VoiceContent below
// resolve only the grace group's immediate preceding event; all other context
// (which Dynamic/Hairpin/Slur applies, the gap to the next onset, whether an
// event is tied) remains an already-resolved parameter, per
// playback_mapping.hpp's overview, "Scope: math only, not context
// resolution".

// The hairpin/dynamic-span endpoint context event_note_on_velocity()
// forwards to interpolate_hairpin_velocity() when a Hairpin governs an
// event; std::nullopt when only a plain DynamicMarking governs it.
struct HairpinVelocityContext {
  MidiVelocity from;
  MidiVelocity to;
  Rational     position;

  [[nodiscard]] bool operator==(const HairpinVelocityContext&) const = default;
};

// This event's final sounded (audible) duration. Reads the event's own
// notated Duration (event_duration()) and, when unslurred, its duration
// articulation (event_articulations(), via is_duration_articulation());
// `is_tied` and an active slur's `slurred_gap_to_next_onset` are supplied
// by the caller, which alone knows the event's tie/slur context.
//
// Calls, in this order: when `slurred_gap_to_next_onset` has a value, this
// event's notated duration is passed straight to legato_sounded_duration()
// UNMODIFIED by sounded_duration_for_articulation() at all -- a slur
// overrides BOTH duration-articulation shortening (and the plain
// no-articulation default gap) AND tie-boundary suppression entirely, per
// playback_mapping.hpp's overview, "Legato (slur) overlap and its
// precedence over shortening". Concretely: `is_tied` has NO EFFECT on this
// function's result whenever `slurred_gap_to_next_onset` has a value --
// slur wins outright over tie-suppression, not merely over articulation
// shortening, so a caller must not expect `is_tied` to matter for a
// slurred note even though it is a named parameter here. Otherwise (no
// slur governs this event), this event's notated duration, its own
// duration articulation (if any), and `is_tied` are passed to
// sounded_duration_for_articulation(), whose own overview documents the
// four ratios and the tie-boundary suppression rule.
[[nodiscard]] Rational event_sounded_duration(
    const VoiceEvent& event, bool is_tied,
    std::optional<Rational> slurred_gap_to_next_onset);

// This event's final MIDI note-on velocity. Calls, in order:
// interpolate_hairpin_velocity() when `hairpin` has a value, else
// velocity_for_dynamic(governing_dynamic), for the base velocity; then
// apply_emphasis() with accent/marcato flags read from the event's own
// articulations (event_articulations()). `governing_dynamic` and
// `hairpin` are caller-resolved, exactly as playback_mapping.hpp's
// interpolate_hairpin_velocity() documents.
[[nodiscard]] MidiVelocity event_note_on_velocity(
    const VoiceEvent& event, Dynamic governing_dynamic,
    std::optional<HairpinVelocityContext> hairpin);

// The stolen duration for each grace note in `group`, in `group.notes`
// order, stolen from `available_duration` (the preceding sounded note's
// own available duration, before that note's own articulation is
// applied -- see grace_steal_durations()'s and
// grace_group_remaining_preceding_duration()'s own overviews for the
// composition order with articulation). Calls grace_steal_durations()
// with `group.notes.size()` and the GROUP's governing GraceNoteType: this
// wiring simplifies a GraceGroup to one uniform kind per group, taken from
// `group.notes.front().type` (GraceNoteType::kAppoggiatura, an arbitrary
// but harmless default, when `group.notes` is empty). A GraceGroup with a
// uniform GraceNoteType across all its notes -- the ordinary case -- is
// unaffected by this simplification; a caller that notates mixed kinds
// within one group gets the whole group's steal shaped by its first
// note's kind, while each GraceNote's own `type` field remains recorded
// and unaffected for notation/engraving purposes.
[[nodiscard]] std::vector<Rational> grace_group_steal_durations(
    const GraceGroup& group, Rational available_duration);

// The preceding note's own remaining sounded duration after `group`'s
// grace notes steal from it -- see grace_group_steal_durations() above for
// the same GraceNoteType-simplification this shares, and
// playback_mapping.hpp's grace_steal_remaining_duration() for the exact
// remaining-duration computation and the composition-order rule for
// feeding this value into event_sounded_duration() as the preceding
// note's own notated duration.
[[nodiscard]] Rational grace_group_remaining_preceding_duration(
    const GraceGroup& group, Rational available_duration);

// The raw duration budget available for a grace group to steal from its
// immediately preceding sounding event in `voice`. A Rest, a missing
// principal event, or a principal event at the start of the voice supplies no
// budget and returns Rational(0), which selects the core fallback duration for
// the grace notes. The principal event itself is never shortened by this
// lookup.
[[nodiscard]] Rational grace_group_preceding_available_duration(
    const VoiceContent& voice, const GraceGroup& group);

// Voice-timeline overloads: apply the default/configured grace-steal policy
// to the event immediately preceding `group.principal_event`. Grace-note
// durations are returned in group order; the remaining-duration overload is
// the preceding event's post-steal budget, before that event's own
// articulation is applied.
[[nodiscard]] std::vector<Rational> grace_group_steal_durations(
    const VoiceContent& voice, const GraceGroup& group);

[[nodiscard]] Rational grace_group_remaining_preceding_duration(
    const VoiceContent& voice, const GraceGroup& group);

}  // namespace graphscore
