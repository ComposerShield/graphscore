// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <graphscore/core/accidental.hpp>
#include <graphscore/core/articulation.hpp>
#include <graphscore/core/clef.hpp>
#include <graphscore/core/dynamic.hpp>
#include <graphscore/core/key_signature.hpp>
#include <graphscore/core/rational.hpp>
#include <graphscore/core/spelled_pitch.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/core/voice.hpp>
#include <graphscore/domain/measure_map.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/notation/notation_types.hpp>

#include "layout_index.hpp"

namespace graphscore {
class NodeTimeline;
class StaveVoices;
struct IndexedStaff;
struct LayoutBuilder;
template <typename Record>
struct ReferenceFamily;

struct EventPlacement {
  const VoiceEvent* event = nullptr;
  Voice             voice;
  Rational          onset;
  std::size_t       measure  = 0;
  double            x        = 0.0;
  double            anchor_y = 0.0;
  bool              stem_up  = true;
};

// Everything the shared rhythm/marking engraver needs that differs between a
// main-region system and the trailing pickdown region. Both engrave through
// the exact same command-emission code path (add_region_rhythm); only the
// horizontal mapping, the event/reference selection, and the accidental/key
// context differ, so there is no second reduced engraver.
struct RhythmRegion {
  Rational start;    // inclusive musical start
  Rational end;      // exclusive musical end
  double   left_x;   // x at `start`
  double   right_x;  // x at `end`
  // x-clamp inset at each region end for span/dynamic endpoint mapping
  // (dynamics, slur/hairpin, ties, tuplets). The main region clamps half a
  // staff space inside each measure; the pickdown region clamps to its
  // metric-aware content inset, so a boundary/node-end dynamic or span
  // endpoint stays inside the transition-to-node-end area under arbitrary
  // injected metrics.
  double span_inset = 0.0;
  // Node-local musical position -> x, for positions inside [start, end).
  std::function<double(Rational)> x_at;
  // Key signature for accidental/grace context at a placement's measure
  // ordinal (the pickdown region ignores the ordinal and always returns the
  // final main measure's key signature).
  std::function<KeySignature(std::size_t)>     key_at;
  std::vector<EventPlacement>                  placements;
  std::array<std::vector<NotationEntityId>, 4> dynamics;
  std::array<std::vector<NotationEntityId>, 4> hairpins;
  std::array<std::vector<NotationEntityId>, 4> slurs;
  std::array<std::vector<NotationEntityId>, 4> grace_groups;
  // Per-voice local event records for tie/tuplet engraving (may include one
  // event before the region start for a boundary-crossing tie).
  std::array<std::vector<IndexedEvent>, 4> local_events;
  // Precomputed beam pairs (automatic + manual overrides), region-scoped.
  std::vector<std::pair<NotationEntityId, NotationEntityId>> beam_pairs;
  // Whether voice diagnostics are emitted here (main region, first system
  // only). The pickdown region never re-emits them.
  bool emit_diagnostics = false;
  // Span/pedal segment-role suffix: "system-N" for the main region,
  // "pickdown" for the trailing region (keeps a crossing span's two
  // segments' ids distinct).
  std::string segment_suffix;
};

[[nodiscard]] char32_t clef_glyph(Clef) noexcept;
[[nodiscard]] int      diatonic_index(const SpelledPitch&) noexcept;
[[nodiscard]] double   pitch_y(const SpelledPitch&, Clef, double,
                               double) noexcept;
[[nodiscard]] std::optional<SpelledPitch> spelled_pitch_at(double, Clef, double,
                                                           double) noexcept;
[[nodiscard]] SmuflGlyph                  notehead_glyph(NoteValue) noexcept;
[[nodiscard]] SmuflGlyph                  rest_glyph(NoteValue) noexcept;
[[nodiscard]] SmuflGlyph                  accidental_glyph(Accidental) noexcept;
[[nodiscard]] Accidental key_accidental(const KeySignature&, Letter) noexcept;
[[nodiscard]] bool       stem_up_for(Voice, StemDirection) noexcept;
[[nodiscard]] double     event_anchor_y(const VoiceEvent&, Voice, Clef, double,
                                        double);
[[nodiscard]] SmuflGlyph articulation_glyph(Articulation);
[[nodiscard]] std::vector<SmuflGlyph>     dynamic_glyphs(Dynamic);
[[nodiscard]] std::vector<EventPlacement> placements_for_system(
    const NodeTimeline&, StaveId, const StaveVoices&, const IndexedStaff&,
    const StaffSystemLayout&, const std::vector<double>&,
    const std::vector<MeasureLayout>&);
[[nodiscard]] std::vector<std::pair<NotationEntityId, SpelledPitch>> pitches(
    const VoiceEvent&);
[[nodiscard]] bool add_signature_glyphs(LayoutBuilder&, const Measure&, Clef,
                                        const NotationId&, NotationPoint,
                                        std::size_t, bool, bool, bool,
                                        std::optional<KeySignature>);
void add_ledger_lines(LayoutBuilder&, const NotationEntityId&, double, double,
                      double);
void add_span_segment(LayoutBuilder&, const NotationEntityId&,
                      const NotationId&, const SystemLayout&, NotationPoint,
                      NotationPoint, double, const std::string&,
                      const std::string& segment_suffix, bool wedge,
                      bool reverse);
[[nodiscard]] bool add_rhythm(LayoutBuilder&, const SystemLayout&,
                              const StaffSystemLayout&, const StaveVoices&,
                              const IndexedStaff&, const std::vector<double>&,
                              const std::vector<MeasureLayout>&);
[[nodiscard]] bool add_pedal_spans(
    LayoutBuilder&, const SystemLayout&, const StaffSystemLayout&,
    const std::vector<PedalSpan>&, Rational start, Rational end, double left_x,
    double right_x, const std::function<double(Rational)>& x_at,
    bool emit_diagnostics, const std::string& segment_suffix);

// Draws the note/rest/marking content whose onset or span enters the node's
// pickdown region (docs/plan/05-notation-editor.md M5-phase-31). The region is
// a non-MeasureMap layout region sharing the final system's geometry, engraved
// through the same add_region_rhythm pipeline as the main region, with a
// region-aware time-to-x mapper. `content_inset` is the content-aware inset
// already baked into `pickdown_width` by layout_internal (so boundary
// accidentals/grace/stems and node-end dots/dynamics stay inside the
// transition-to-node-end area); it is passed through to the time-to-x mapper
// and to clef-change emission. The distinct transition boundary and the
// node-end barline are drawn by layout_internal, not here. Returns false on
// invalid glyph metrics.
[[nodiscard]] bool add_pickdown_region(LayoutBuilder&, const SystemLayout&,
                                       const StaffSystemLayout&,
                                       const StaveVoices&, const IndexedStaff&,
                                       double boundary_x, double pickdown_width,
                                       double content_inset);

// Emits one staff's complete pickdown content -- the region's pedal spans plus
// its rhythm/marking content -- through the same command-emission path the
// final layout uses. layout_internal calls this for both a throwaway preflight
// (to measure the actual emitted glyph/line/path/hit extents that decide the
// content-aware inset, M5-phase-31) and the final pass, so placement and bounds
// are single-sourced in the engraver rather than mirrored in a parallel list.
[[nodiscard]] bool add_pickdown_content(LayoutBuilder&, const SystemLayout&,
                                        const StaffSystemLayout&,
                                        const StaveVoices&, const IndexedStaff&,
                                        double boundary_x,
                                        double pickdown_width,
                                        double content_inset);

template <typename Record>
[[nodiscard]] std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<Record>&    family,
    const std::vector<MeasureLayout>& measures);
}  // namespace graphscore
