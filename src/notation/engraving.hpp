// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
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
                      NotationPoint, double, const std::string&, bool, bool);
[[nodiscard]] bool add_rhythm(LayoutBuilder&, const SystemLayout&,
                              const StaffSystemLayout&, const StaveVoices&,
                              const IndexedStaff&, const std::vector<double>&,
                              const std::vector<MeasureLayout>&);
[[nodiscard]] bool add_pedal_spans(LayoutBuilder&, const SystemLayout&,
                                   const StaffSystemLayout&,
                                   const std::vector<PedalSpan>&);

template <typename Record>
[[nodiscard]] std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<Record>&    family,
    const std::vector<MeasureLayout>& measures);
}  // namespace graphscore
