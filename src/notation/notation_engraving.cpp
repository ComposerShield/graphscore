// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/voice_content.hpp>

#include "engraving.hpp"
#include "layout_builder.hpp"
#include "layout_index.hpp"
#include "measure_math.hpp"
#include "notation_ids.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

[[nodiscard]] char32_t clef_glyph(Clef clef) noexcept {
  switch (clef) {
    case Clef::kTreble:
      return smufl_codepoint(SmuflGlyph::kGClef);
    case Clef::kBass:
      return smufl_codepoint(SmuflGlyph::kFClef);
    case Clef::kAlto:
    case Clef::kTenor:
      return smufl_codepoint(SmuflGlyph::kCClef);
  }
}

[[nodiscard]] bool add_signature_glyphs(
    LayoutBuilder& builder, const Measure& measure, Clef clef,
    const NotationId& staff_id, NotationPoint origin, std::size_t ordinal,
    bool emit_clef, bool emit_key, bool emit_time,
    std::optional<KeySignature> cancelled_key = std::nullopt) {
  const std::string ordinal_text = std::to_string(ordinal);
  double            x            = origin.x + builder.options.staff_space;
  char32_t          previous     = U'\0';
  const auto        place = [&](const NotationId& id, char32_t glyph, double y,
                         double minimum_advance) {
    if (previous != U'\0') {
      const double kerning =
          builder.metrics.kerning(previous, glyph, builder.options.staff_space);
      if (!std::isfinite(kerning)) {
        builder.error = NotationLayoutError::kInvalidMetrics;
        return false;
      }
      x += kerning;
    }
    const std::optional<double> advance =
        builder.add_glyph(id, glyph, NotationPoint{x, y});
    if (!advance.has_value()) {
      return false;
    }
    x += std::max(*advance, minimum_advance);
    previous = glyph;
    return true;
  };
  if (emit_clef &&
      !place(make_id(staff_id.value, "measure/" + ordinal_text + "/clef"),
             clef_glyph(clef), origin.y, builder.options.staff_space * 3.0)) {
    return false;
  }
  const std::uint8_t encoded_fifths =
      static_cast<std::uint8_t>(measure.key_signature.fifths());
  const int           fifths = encoded_fifths <= 7
                                   ? static_cast<int>(encoded_fifths)
                                   : static_cast<int>(encoded_fifths) - 256;
  const std::uint16_t fifth_count =
      static_cast<std::uint16_t>(fifths < 0 ? -fifths : fifths);
  constexpr std::array<std::array<double, 7>, 4> kSharpPositions = {{
      {0.0, 1.5, -0.5, 1.0, 2.5, 0.5, 2.0},
      {1.0, 2.5, 0.5, 2.0, 3.5, 1.5, 3.0},
      {1.5, 3.0, 1.0, 2.5, 4.0, 2.0, 3.5},
      {0.5, 2.0, 0.0, 1.5, 3.0, 1.0, 2.5},
  }};
  constexpr std::array<std::array<double, 7>, 4> kFlatPositions  = {{
      {2.0, 0.5, 2.5, 1.0, 3.0, 1.5, 3.5},
      {3.0, 1.5, 3.5, 2.0, 4.0, 2.5, 4.5},
      {3.5, 2.0, 4.0, 2.5, 4.5, 3.0, 5.0},
      {2.5, 1.0, 3.0, 1.5, 3.5, 2.0, 4.0},
  }};
  const auto clef_index = static_cast<std::size_t>(clef);
  if (cancelled_key.has_value()) {
    const int old_fifths = [&] {
      const std::uint8_t encoded =
          static_cast<std::uint8_t>(cancelled_key->fifths());
      return encoded <= 7 ? static_cast<int>(encoded)
                          : static_cast<int>(encoded) - 256;
    }();
    const std::size_t old_count =
        static_cast<std::size_t>(std::abs(old_fifths));
    for (std::size_t index = 0; index < old_count; ++index) {
      if (!place(make_id(staff_id.value, "measure/" + ordinal_text +
                                             "/key-cancel/" +
                                             std::to_string(index)),
                 smufl_codepoint(SmuflGlyph::kAccidentalNatural),
                 origin.y + builder.options.staff_space *
                                (old_fifths < 0
                                     ? kFlatPositions[clef_index][index]
                                     : kSharpPositions[clef_index][index]),
                 builder.options.staff_space)) {
        return false;
      }
    }
  }
  for (std::uint16_t index = 0; emit_key && index < fifth_count; ++index) {
    const char32_t glyph = fifths < 0
                               ? smufl_codepoint(SmuflGlyph::kAccidentalFlat)
                               : smufl_codepoint(SmuflGlyph::kAccidentalSharp);
    if (!place(make_id(staff_id.value, "measure/" + ordinal_text + "/key/" +
                                           std::to_string(index)),
               glyph,
               origin.y + builder.options.staff_space *
                              (fifths < 0 ? kFlatPositions[clef_index][index]
                                          : kSharpPositions[clef_index][index]),
               builder.options.staff_space)) {
      return false;
    }
  }
  const auto add_number = [&](std::uint16_t number, const std::string& role,
                              double y) {
    const std::string digits = std::to_string(number);
    for (std::size_t index = 0; index < digits.size(); ++index) {
      const auto  digit      = static_cast<char32_t>(digits[index] - '0');
      std::string glyph_role = "measure/" + ordinal_text;
      glyph_role.append("/time/").append(role).append("/").append(
          std::to_string(index));
      if (!place(make_id(staff_id.value, glyph_role), kTimeZero + digit, y,
                 builder.options.staff_space)) {
        return false;
      }
    }
    return true;
  };
  if (!emit_time) {
    return true;
  }
  const double   time_x                = x;
  const char32_t signature_predecessor = previous;
  if (!add_number(measure.time_signature.numerator(), "numerator", origin.y)) {
    return false;
  }
  x        = time_x;
  previous = signature_predecessor;
  return add_number(measure.time_signature.denominator(), "denominator",
                    origin.y + builder.options.staff_space * 2.0);
}

[[nodiscard]] int diatonic_index(const SpelledPitch& pitch) noexcept {
  constexpr std::array<int, 7> kFromC = {5, 6, 0, 1, 2, 3, 4};
  return (static_cast<int>(pitch.octave()) + 1) * 7 +
         kFromC[static_cast<std::size_t>(pitch.letter())];
}

namespace {

[[nodiscard]] int clef_middle_line(Clef clef) noexcept {
  switch (clef) {
    case Clef::kTreble:
      return 41;  // B4
    case Clef::kBass:
      return 29;  // D3
    case Clef::kAlto:
      return 35;  // C4
    case Clef::kTenor:
      return 33;  // A3
  }
}

}  // namespace

[[nodiscard]] double pitch_y(const SpelledPitch& pitch, Clef clef, double top,
                             double space) noexcept {
  return top +
         static_cast<double>(clef_middle_line(clef) - diatonic_index(pitch)) *
             space * 0.5 +
         space * 2.0;
}

// The exact inverse of pitch_y(): the natural diatonic staff step nearest
// `y`, spelled with Accidental::kNatural (a staff position selects a step,
// never an accidental). Returns std::nullopt rather than a clamped value
// when the nearest step's octave falls outside SpelledPitch's valid
// [kMinOctave, kMaxOctave] range, when `space` is not strictly positive, or
// when any input is non-finite.
[[nodiscard]] std::optional<SpelledPitch> spelled_pitch_at(
    double y, Clef clef, double top, double space) noexcept {
  if (!(space > 0.0) || !std::isfinite(y) || !std::isfinite(top)) {
    return std::nullopt;
  }
  const double raw = static_cast<double>(clef_middle_line(clef)) -
                     (y - top - space * 2.0) * 2.0 / space;
  if (!std::isfinite(raw) || std::abs(raw) > 1e6) {
    return std::nullopt;
  }
  const std::int64_t step         = std::llround(raw);
  std::int64_t       octave_plus1 = step / 7;
  std::int64_t       letter_index = step % 7;
  if (letter_index < 0) {
    --octave_plus1;
    letter_index += 7;
  }
  const std::int64_t octave = octave_plus1 - 1;
  if (octave < SpelledPitch::kMinOctave || octave > SpelledPitch::kMaxOctave) {
    return std::nullopt;
  }
  constexpr std::array<Letter, 7> kLettersFromC = {
      Letter::kC, Letter::kD, Letter::kE, Letter::kF,
      Letter::kG, Letter::kA, Letter::kB};
  return SpelledPitch::create(
      kLettersFromC[static_cast<std::size_t>(letter_index)],
      static_cast<std::int8_t>(octave), Accidental::kNatural);
}

[[nodiscard]] SmuflGlyph notehead_glyph(NoteValue value) noexcept {
  if (value == NoteValue::kWhole) {
    return SmuflGlyph::kNoteheadWhole;
  }
  if (value == NoteValue::kHalf) {
    return SmuflGlyph::kNoteheadHalf;
  }
  return SmuflGlyph::kNoteheadBlack;
}

[[nodiscard]] SmuflGlyph rest_glyph(NoteValue value) noexcept {
  constexpr std::array<SmuflGlyph, 7> kRests = {
      SmuflGlyph::kRestWhole,  SmuflGlyph::kRestHalf, SmuflGlyph::kRestQuarter,
      SmuflGlyph::kRestEighth, SmuflGlyph::kRest16th, SmuflGlyph::kRest32nd,
      SmuflGlyph::kRest64th};
  return kRests[static_cast<std::size_t>(value)];
}

[[nodiscard]] SmuflGlyph accidental_glyph(Accidental accidental) noexcept {
  constexpr std::array<SmuflGlyph, 5> kAccidentals = {
      SmuflGlyph::kAccidentalDoubleFlat, SmuflGlyph::kAccidentalFlat,
      SmuflGlyph::kAccidentalNatural, SmuflGlyph::kAccidentalSharp,
      SmuflGlyph::kAccidentalDoubleSharp};
  return kAccidentals[static_cast<std::size_t>(
      static_cast<int>(accidental) -
      static_cast<int>(Accidental::kDoubleFlat))];
}

[[nodiscard]] Accidental key_accidental(const KeySignature& key,
                                        Letter              letter) noexcept {
  constexpr std::array<Letter, 7> kSharps = {Letter::kF, Letter::kC, Letter::kG,
                                             Letter::kD, Letter::kA, Letter::kE,
                                             Letter::kB};
  constexpr std::array<Letter, 7> kFlats  = {Letter::kB, Letter::kE, Letter::kA,
                                             Letter::kD, Letter::kG, Letter::kC,
                                             Letter::kF};
  const std::uint8_t              encoded_fifths_key =
      static_cast<std::uint8_t>(key.fifths());
  const int fifths = encoded_fifths_key <= 7
                         ? static_cast<int>(encoded_fifths_key)
                         : static_cast<int>(encoded_fifths_key) - 256;
  if (fifths > 0 && std::ranges::find(kSharps.begin(), kSharps.begin() + fifths,
                                      letter) != kSharps.begin() + fifths) {
    return Accidental::kSharp;
  }
  if (fifths < 0 && std::ranges::find(kFlats.begin(), kFlats.begin() - fifths,
                                      letter) != kFlats.begin() - fifths) {
    return Accidental::kFlat;
  }
  return Accidental::kNatural;
}

[[nodiscard]] bool stem_up_for(Voice voice, StemDirection override) noexcept {
  if (override != StemDirection::kAuto) {
    return override == StemDirection::kUp;
  }
  // Voices 1/3 are the upper pair and 2/4 the lower pair. This remains
  // semantic policy even when a crossing voice happens to sit on the other
  // side of the middle line; explicit domain overrides always win.
  return voice.index() == 1 || voice.index() == 3;
}

[[nodiscard]] double event_anchor_y(const VoiceEvent& event, Voice voice,
                                    Clef clef, double staff_top, double space) {
  if (const auto* note = std::get_if<Note>(&event)) {
    return pitch_y(note->pitch, clef, staff_top, space);
  }
  if (const auto* chord = std::get_if<Chord>(&event);
      chord != nullptr && !chord->notes.empty()) {
    const bool up = stem_up_for(voice, chord->stem);
    double result = pitch_y(chord->notes.front().pitch, clef, staff_top, space);
    for (const ChordNote& chord_note : chord->notes) {
      const double y = pitch_y(chord_note.pitch, clef, staff_top, space);
      result         = up ? std::min(result, y) : std::max(result, y);
    }
    return result;
  }
  return event_y(voice, staff_top, space);
}

[[nodiscard]] std::vector<EventPlacement> placements_for_system(
    const NodeTimeline& timeline, StaveId stave_id, const StaveVoices& voices,
    const IndexedStaff& indexed, const StaffSystemLayout& staff,
    const std::vector<double>&        widths,
    const std::vector<MeasureLayout>& measures) {
  std::vector<EventPlacement> placements;
  const MeasureMap&           measure_map = timeline.measures();
  const ClefLane*             clefs       = timeline.clef_lane(stave_id);
  for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
    const auto voice_value = Voice::create(index);
    if (!voice_value.has_value()) {
      continue;
    }
    const Voice voice  = *voice_value;
    const auto& events = voices.voice(voice).events();
    for (std::size_t measure = measures.front().ordinal;
         measure <= measures.back().ordinal; ++measure) {
      for (const IndexedEvent& record :
           indexed.voices[index - Voice::kMin].measures[measure]) {
        const VoiceEvent& event = events[record.event_index];
        const auto        local = measure - measures.front().ordinal;
        const Clef        clef =
            clefs == nullptr ? Clef::kTreble : clefs->clef_at(record.onset);
        const double staff_space = staff.bounds.height / 4.0;
        const double y =
            event_anchor_y(event, voice, clef, staff.bounds.y, staff_space);
        placements.push_back(EventPlacement{
            &event, voice, record.onset, measure,
            position_x(measure_map, widths, measure, record.onset,
                       measures[local].bounds.x, staff_space),
            y, stem_up_for(voice, event_stem(event))});
      }
    }
  }
  std::ranges::sort(placements, [](const auto& left, const auto& right) {
    if (left.onset != right.onset) {
      return left.onset < right.onset;
    }
    return left.voice.index() < right.voice.index();
  });
  return placements;
}

[[nodiscard]] std::vector<std::pair<NotationEntityId, SpelledPitch>> pitches(
    const VoiceEvent& event) {
  std::vector<std::pair<NotationEntityId, SpelledPitch>> result;
  if (const auto* note = std::get_if<Note>(&event)) {
    result.emplace_back(note->id, note->pitch);
  } else if (const auto* chord = std::get_if<Chord>(&event)) {
    result.reserve(chord->notes.size());
    for (const ChordNote& chord_note : chord->notes) {
      result.emplace_back(chord_note.id, chord_note.pitch);
    }
  }
  std::ranges::sort(result, [](const auto& left, const auto& right) {
    const int left_index  = diatonic_index(left.second);
    const int right_index = diatonic_index(right.second);
    return left_index == right_index
               ? left.first.to_string() < right.first.to_string()
               : left_index < right_index;
  });
  return result;
}

void add_ledger_lines(LayoutBuilder& builder, const NotationEntityId& id,
                      double x, double y, double staff_top) {
  const double space   = builder.options.staff_space;
  int          ordinal = 0;
  for (double line_y = staff_top - space; y <= line_y + space * 0.25;
       line_y -= space) {
    builder.add_line(make_id(id, "ledger/above/" + std::to_string(ordinal++)),
                     {x - space * 0.85, line_y}, {x + space * 0.85, line_y},
                     space * 0.12);
  }
  ordinal = 0;
  for (double line_y = staff_top + space * 5.0; y >= line_y - space * 0.25;
       line_y += space) {
    builder.add_line(make_id(id, "ledger/below/" + std::to_string(ordinal++)),
                     {x - space * 0.85, line_y}, {x + space * 0.85, line_y},
                     space * 0.12);
  }
}

[[nodiscard]] SmuflGlyph articulation_glyph(Articulation articulation) {
  switch (articulation) {
    case Articulation::kAccent:
      return SmuflGlyph::kArticAccentAbove;
    case Articulation::kMarcato:
      return SmuflGlyph::kArticMarcatoAbove;
    case Articulation::kStaccato:
      return SmuflGlyph::kArticStaccatoAbove;
    case Articulation::kStaccatissimo:
      return SmuflGlyph::kArticStaccatissimoAbove;
    case Articulation::kTenuto:
      return SmuflGlyph::kArticTenutoAbove;
  }
}

[[nodiscard]] std::vector<SmuflGlyph> dynamic_glyphs(Dynamic dynamic) {
  using enum SmuflGlyph;
  switch (dynamic) {
    case Dynamic::kPpp:
      return {kDynamicP, kDynamicP, kDynamicP};
    case Dynamic::kPp:
      return {kDynamicP, kDynamicP};
    case Dynamic::kP:
      return {kDynamicP};
    case Dynamic::kMp:
      return {kDynamicM, kDynamicP};
    case Dynamic::kMf:
      return {kDynamicM, kDynamicF};
    case Dynamic::kF:
      return {kDynamicF};
    case Dynamic::kFf:
      return {kDynamicF, kDynamicF};
    case Dynamic::kFff:
      return {kDynamicF, kDynamicF, kDynamicF};
  }
}

namespace {

// Evaluates a cubic Bézier at parameter t ∈ [0, 1].
[[nodiscard]] NotationPoint bezier_point(NotationPoint p0, NotationPoint p1,
                                         NotationPoint p2, NotationPoint p3,
                                         double t) {
  const double t1 = 1.0 - t;
  return {
      t1 * t1 * t1 * p0.x + 3.0 * t1 * t1 * t * p1.x + 3.0 * t1 * t * t * p2.x +
          t * t * t * p3.x,
      t1 * t1 * t1 * p0.y + 3.0 * t1 * t1 * t * p1.y + 3.0 * t1 * t * t * p2.y +
          t * t * t * p3.y,
  };
}

}  // namespace

void add_span_segment(LayoutBuilder& builder, const NotationEntityId& id,
                      const NotationId& semantic, const SystemLayout& system,
                      NotationPoint from, NotationPoint to, double lane,
                      const std::string& role, bool wedge, bool reverse) {
  const std::string segment_role =
      role + "/segment/system-" + std::to_string(system.first_measure);
  const NotationId segment = make_id(id, segment_role);
  builder.output.commands.emplace_back(
      ClipCommand{make_id(segment.value, "clip/begin"), system.bounds, true});
  if (wedge) {
    const double open       = builder.options.staff_space * 0.7;
    const double left_open  = reverse ? open : 0.0;
    const double right_open = reverse ? 0.0 : open;
    builder.add_line(make_id(segment.value, "upper"),
                     {from.x, lane - left_open}, {to.x, lane - right_open},
                     builder.options.staff_space * 0.1);
    builder.add_line(make_id(segment.value, "lower"),
                     {from.x, lane + left_open}, {to.x, lane + right_open},
                     builder.options.staff_space * 0.1);
  } else {
    const double arch = builder.options.staff_space * 1.2;
    builder.add_path(make_id(segment.value, "curve"),
                     {{PathVerb::kMove, {}, {}, from},
                      {PathVerb::kCubic,
                       {from.x + (to.x - from.x) / 3.0, lane - arch},
                       {from.x + (to.x - from.x) * 2.0 / 3.0, lane - arch},
                       to}},
                     builder.options.staff_space * 0.12);
  }
  builder.output.commands.emplace_back(
      ClipCommand{make_id(segment.value, "clip/end"), system.bounds, false});
  const double space = builder.options.staff_space;
  if (role == std::string(kHitRoleTie)) {
    // Tight hit regions bound to the actual tie curve rather than a
    // universal four-staff-space band.  The tie is a cubic bezier arching
    // from (from.x, lane) to (to.x, lane) via control points at
    // y = lane - arch where arch = space * 1.2, stroked at space * 0.12.
    // The curve is subdivided into 8 segments; each segment's own
    // expanded rect is clipped to system.bounds.  A half-space tolerance
    // on each side of the visual extent keeps the drawn curve selectable
    // while leaving the notehead column and articulation glyphs on the
    // same chord reachable away from it.
    constexpr int       kSubdivs    = 8;
    const double        arch        = space * 1.2;
    const double        half_stroke = space * 0.06;
    const double        tolerance   = space * 0.5;
    const double        dx          = to.x - from.x;
    const NotationPoint cp1{from.x + dx / 3.0, lane - arch};
    const NotationPoint cp2{from.x + dx * 2.0 / 3.0, lane - arch};

    // Evaluate at kSubdivs+1 equally-spaced points along the curve.
    std::vector<NotationPoint> eval;
    eval.reserve(kSubdivs + 1);
    for (std::size_t i = 0; i <= static_cast<std::size_t>(kSubdivs); ++i) {
      const double t = static_cast<double>(i) / kSubdivs;
      eval.push_back(bezier_point(from, cp1, cp2, to, t));
    }

    for (std::size_t i = 0; i < static_cast<std::size_t>(kSubdivs); ++i) {
      const NotationPoint& seg_a = eval[i];
      const NotationPoint& seg_b = eval[i + 1];

      // y-extent: endpoints, plus the global apex y(0.5)=lane-0.9*space
      // when this segment spans t=0.5.
      double       seg_min_y = std::min(seg_a.y, seg_b.y);
      const double seg_max_y = std::max(seg_a.y, seg_b.y);
      if (static_cast<double>(i) / kSubdivs <= 0.5 &&
          0.5 <= static_cast<double>(i + 1) / kSubdivs) {
        const double apex_y = lane - 0.75 * arch;
        seg_min_y           = std::min(seg_min_y, apex_y);
      }

      const NotationRect seg_rect{
          std::min(seg_a.x, seg_b.x) - tolerance,
          seg_min_y - half_stroke,
          std::abs(seg_b.x - seg_a.x) + tolerance * 2.0,
          seg_max_y - seg_min_y + half_stroke * 2.0,
      };

      // Clip to system bounds so a tie extending past a system edge
      // cannot produce a hit region outside the visible system.
      const double clipped_left  = std::max(seg_rect.x, system.bounds.x);
      const double clipped_top   = std::max(seg_rect.y, system.bounds.y);
      const double clipped_right = std::min(
          seg_rect.x + seg_rect.width, system.bounds.x + system.bounds.width);
      const double clipped_bottom = std::min(
          seg_rect.y + seg_rect.height, system.bounds.y + system.bounds.height);
      if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) {
        continue;
      }

      const NotationId sub_id =
          make_id(segment.value, "sub/" + std::to_string(i));
      builder.add_hit(
          sub_id, semantic, HitRole::kMarking,
          NotationRect{clipped_left, clipped_top, clipped_right - clipped_left,
                       clipped_bottom - clipped_top},
          kHitPrioritySpanSegment);
    }
  } else {
    builder.add_hit(segment, semantic, HitRole::kMarking,
                    {std::min(from.x, to.x), lane - space * 2.0,
                     std::abs(to.x - from.x), space * 4.0},
                    kHitPrioritySpanSegment);
  }
}

[[nodiscard]] bool add_pedal_spans(LayoutBuilder&                builder,
                                   const SystemLayout&           system,
                                   const StaffSystemLayout&      staff,
                                   const std::vector<PedalSpan>& spans) {
  const MeasureMap& map   = builder.timeline.measures();
  const Rational    start = map.measure_start(system.measures.front().ordinal);
  const MeasureLayout& last = system.measures.back();
  const Rational       end =
      map.measure_start(last.ordinal) + map.measure_length(last.ordinal);
  const double space = builder.options.staff_space;
  const auto   x_for = [&](Rational position) {
    if (position <= start) {
      return system.measures.front().bounds.x + space * 0.5;
    }
    if (position >= end) {
      return last.bounds.x + last.bounds.width - space * 0.5;
    }
    const std::size_t    measure = *map.measure_index_at(position);
    const MeasureLayout& layout =
        system.measures[measure - system.first_measure];
    const double fraction =
        ((position - map.measure_start(measure)).to_double() /
         map.measure_length(measure).to_double());
    return layout.bounds.x + layout.bounds.width * fraction;
  };
  if (system.first_measure == 0) {
    for (const NotationDiagnostic& diagnostic :
         validate_pedal_spans(spans, builder.timeline.node_end())) {
      builder.output.diagnostics.push_back(
          {diagnostic.entity_id,
           "omitted-invalid-pedal:" +
               std::to_string(static_cast<int>(diagnostic.code))});
    }
  }
  std::vector<std::pair<Rational, Rational>> lanes;
  for (const PedalSpan& span : spans) {
    if (!(span.start < span.end) || span.start < Rational(0) ||
        span.end > builder.timeline.node_end() || span.end <= start ||
        span.start >= end) {
      continue;
    }
    std::size_t lane = 0;
    while (lane < lanes.size() && lanes[lane].first < span.end &&
           span.start < lanes[lane].second) {
      ++lane;
    }
    if (lane == lanes.size()) {
      lanes.emplace_back(span.start, span.end);
    } else {
      lanes[lane] = {span.start, span.end};
    }
    const double y = staff.bounds.y + staff.bounds.height +
                     space * (7.0 + static_cast<double>(lane) * 1.6);
    const NotationId  semantic{span.id.to_string()};
    const std::string role =
        "pedal/segment/system-" + std::to_string(system.first_measure);
    const NotationId segment = make_id(span.id, role);
    builder.output.commands.emplace_back(
        ClipCommand{make_id(segment.value, "clip/begin"), system.bounds, true});
    if (span.start >= start) {
      if (!builder
               .add_glyph(make_id(segment.value, "down"),
                          smufl_codepoint(SmuflGlyph::kPedalDown),
                          {x_for(span.start), y}, semantic)
               .has_value()) {
        return false;
      }
      builder.output.hit_regions.back().role = HitRole::kMarking;
    }
    builder.add_line(make_id(segment.value, "line"),
                     {x_for(span.start), y + space * 0.7},
                     {x_for(span.end), y + space * 0.7}, space * 0.12);
    if (span.end <= end) {
      if (!builder
               .add_glyph(make_id(segment.value, "up"),
                          smufl_codepoint(SmuflGlyph::kPedalUp),
                          {x_for(span.end), y}, semantic)
               .has_value()) {
        return false;
      }
      builder.output.hit_regions.back().role = HitRole::kMarking;
    }
    builder.output.commands.emplace_back(
        ClipCommand{make_id(segment.value, "clip/end"), system.bounds, false});
    builder.add_hit(segment, semantic, HitRole::kMarking,
                    {x_for(span.start), y - space,
                     x_for(span.end) - x_for(span.start), space * 2.0},
                    kHitPrioritySpanSegment);
  }
  return true;
}

}  // namespace graphscore
