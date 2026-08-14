// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace graphscore::writer_app {

struct KeySelectionProject {
  graphscore::Project                project;
  graphscore::NodeId                 node_id;
  std::array<graphscore::TrackId, 3> track_ids{};
  std::array<graphscore::StaveId, 3> stave_ids{};
  graphscore::NotationLayout         layout;
};

[[nodiscard]] std::optional<KeySelectionProject> build_key_selection_project(
    const graphscore::GlyphMetrics& metrics);

struct NoteheadMoveFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId first_note_id;
  graphscore::NotationEntityId second_note_id;
  graphscore::NotationLayout   layout;
};

[[nodiscard]] std::optional<NoteheadMoveFixture> build_notehead_move_fixture(
    const graphscore::GlyphMetrics& metrics);

// A single-staff fixture for the chord/grace click tests: a leading C4
// quarter (so the grace-attached principal sits off the very first beat),
// the grace principal D4 quarter, a two-note chord (E4, G4 quarter), and a
// grace group (F4 eighth) attached to the principal. The lead is present so
// the grace notehead engraves at a positive x and is reachable by a click.
struct NoteheadClickFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId chord_id;        // top-level Chord event id
  graphscore::NotationEntityId chord_note_id;   // E4 (moved chord notehead)
  graphscore::NotationEntityId chord_other_id;  // G4 (untouched chord notehead)
  graphscore::NotationEntityId grace_id;        // F4 grace notehead
  graphscore::NotationLayout   layout;
};

[[nodiscard]] std::optional<NoteheadClickFixture> build_notehead_click_fixture(
    const graphscore::GlyphMetrics& metrics);

// A single-staff, two-measure fixture whose voice is three quarter rests then
// a tied C4 quarter in measure 0, and a C4 quarter in measure 1 -- the tied
// notehead and its target sit on opposite sides of the barline, so the
// connected tie chain spans measures 0 and 1.
struct CrossMeasureTieFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId first_id;   // tied C4 (end of measure 0)
  graphscore::NotationEntityId second_id;  // C4 (start of measure 1)
  graphscore::NotationLayout   layout;
};

[[nodiscard]] std::optional<CrossMeasureTieFixture>
build_cross_measure_tie_fixture(
    const graphscore::GlyphMetrics&          metrics,
    const graphscore::NotationLayoutOptions& options);

// A single-staff, two-measure fixture whose voice is three quarter rests
// then a two-pitch Chord {C4, E4} in measure 0 -- with ONLY E4
// tied_to_next, not C4 -- immediately followed by a second two-pitch Chord
// {C4, E4} at the start of measure 1. Converting measure 1's chord to a
// rest normalizes away measure 0's E4 tie (ConvertEventToRestCommand's
// internal::clear_incoming_ties), but the pitch a "R" press on measure 1's
// C4 ChordNote directly names -- C4 -- never crosses the barline on its
// own, since only E4 carried the tie. This reproduces the finding that a
// single-pitch invalidation scope derived from the clicked pitch alone
// misses measure 0 entirely, even though converting the whole chord can
// clear a tie on any of its pitches. Callers pass one-measure-per-system
// options (as build_cross_measure_tie_fixture's own callers do) so the two
// measures land in two separate systems -- otherwise a single system
// spanning both measures would redraw both from current content on ANY
// invalidation inside it, masking the under-invalidation this fixture
// exists to expose.
struct CrossMeasureChordTieFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId measure0_e4_id;  // tied E4 (end of measure 0)
  graphscore::NotationEntityId
      measure1_chord_id;                        // top-level Chord (measure 1)
  graphscore::NotationEntityId measure1_c4_id;  // C4 ChordNote (clicked)
  graphscore::NotationLayout   layout;
};

[[nodiscard]] std::optional<CrossMeasureChordTieFixture>
build_cross_measure_chord_tie_fixture(
    const graphscore::GlyphMetrics&          metrics,
    const graphscore::NotationLayoutOptions& options);

// A single-staff, two-measure fixture whose two measures land in two separate
// systems (via narrow options) and whose voice carries one UNTIED note in each
// measure. Moving the measure-0 note is a single-measure (local) edit, so the
// fixture proves a cold-cache first move rebuilds only the affected system.
struct TwoSystemLocalFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId first_id;   // C4 quarter (measure 0)
  graphscore::NotationEntityId second_id;  // E4 quarter (measure 1)
  graphscore::NotationLayout   layout;
};

[[nodiscard]] std::optional<TwoSystemLocalFixture>
build_two_system_local_fixture(
    const graphscore::GlyphMetrics&          metrics,
    const graphscore::NotationLayoutOptions& options);

struct IntervalEntryFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId source_id;  // the Note id or first ChordNote id
  graphscore::NotationLayout   layout;
};

[[nodiscard]] std::optional<IntervalEntryFixture> build_interval_note_fixture(
    const graphscore::GlyphMetrics& metrics, std::int8_t fifths);

[[nodiscard]] std::optional<IntervalEntryFixture> build_interval_chord_fixture(
    const graphscore::GlyphMetrics& metrics, std::int8_t fifths);

}  // namespace graphscore::writer_app
