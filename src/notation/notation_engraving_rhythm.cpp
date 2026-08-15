// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/voice_content.hpp>
#include <graphscore/notation/notation_editing.hpp>

#include "engraving.hpp"
#include "layout_builder.hpp"
#include "layout_index.hpp"
#include "measure_math.hpp"
#include "notation_ids.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

template <typename Record>
[[nodiscard]] std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<Record>&    family,
    const std::vector<MeasureLayout>& measures) {
  std::unordered_set<NotationEntityId> seen;
  std::vector<NotationEntityId>        result;
  for (std::size_t measure = measures.front().ordinal;
       measure <= measures.back().ordinal; ++measure) {
    for (const NotationEntityId& id : family.by_measure[measure]) {
      if (seen.insert(id).second)
        result.push_back(id);
    }
  }
  std::ranges::sort(
      result, [&](const NotationEntityId& a, const NotationEntityId& b) {
        return family.entries.at(a).order_key < family.entries.at(b).order_key;
      });
  return result;
}

template std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<DynamicMarking>&, const std::vector<MeasureLayout>&);
template std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<Hairpin>&, const std::vector<MeasureLayout>&);
template std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<Slur>&, const std::vector<MeasureLayout>&);
template std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<BeamOverride>&, const std::vector<MeasureLayout>&);
template std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<GraceGroup>&, const std::vector<MeasureLayout>&);
template std::vector<NotationEntityId> system_reference_ids(
    const ReferenceFamily<PedalSpan>&, const std::vector<MeasureLayout>&);

[[nodiscard]] bool add_rhythm(LayoutBuilder&                    builder,
                              const SystemLayout&               system,
                              const StaffSystemLayout&          staff,
                              const StaveVoices&                voices,
                              const IndexedStaff&               indexed,
                              const std::vector<double>&        widths,
                              const std::vector<MeasureLayout>& measures) {
  const double      space       = builder.options.staff_space;
  const MeasureMap& measure_map = builder.timeline.measures();
  auto              placements =
      placements_for_system(builder.timeline, staff.stave_id, voices, indexed,
                            staff, widths, measures);
  std::unordered_map<NotationEntityId, EventPlacement> all_events;
  for (const EventPlacement& placement : placements) {
    all_events.emplace(event_id(*placement.event), placement);
  }
  const auto resolve_event = [&](Voice            voice,
                                 NotationEntityId id) -> const EventPlacement* {
    if (const auto found = all_events.find(id); found != all_events.end()) {
      return &found->second;
    }
    const IndexedVoice& indexed_voice =
        indexed.voices[voice.index() - Voice::kMin];
    const auto record = indexed_voice.by_id.find(id);
    if (record == indexed_voice.by_id.end()) {
      return nullptr;
    }
    const VoiceEvent& event =
        voices.voice(voice).events()[record->second.event_index];
    const ClefLane* lane = builder.timeline.clef_lane(staff.stave_id);
    const Clef      clef =
        lane == nullptr ? Clef::kTreble : lane->clef_at(record->second.onset);
    const auto [inserted, unused] = all_events.emplace(
        id, EventPlacement{
                &event, voice, record->second.onset, record->second.measure,
                0.0, event_anchor_y(event, voice, clef, staff.bounds.y, space),
                stem_up_for(voice, event_stem(event))});
    (void)unused;
    return &inserted->second;
  };

  // Automatic beams remain inside a beat and measure. Manual joins/breaks
  // deterministically override that decision for each adjacent listed pair.
  std::vector<std::pair<NotationEntityId, NotationEntityId>> beam_pairs;
  const auto insert_beam = [&beam_pairs](const auto& pair) {
    if (std::ranges::find(beam_pairs, pair) == beam_pairs.end()) {
      beam_pairs.push_back(pair);
    }
  };
  for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
       ++voice_index) {
    const Voice         voice   = *Voice::create(voice_index);
    const VoiceContent& content = voices.voice(voice);
    const IndexedVoice& voice_indexed =
        indexed.voices[voice_index - Voice::kMin];
    const VoiceEvent* previous = nullptr;
    Rational          previous_onset;
    if (measures.front().ordinal > 0) {
      for (std::size_t ordinal = measures.front().ordinal; ordinal > 0;
           --ordinal) {
        if (!voice_indexed.measures[ordinal - 1].empty()) {
          const IndexedEvent& record =
              voice_indexed.measures[ordinal - 1].back();
          previous       = &content.events()[record.event_index];
          previous_onset = record.onset;
          break;
        }
      }
    }
    for (std::size_t ordinal = measures.front().ordinal;
         ordinal <= measures.back().ordinal; ++ordinal) {
      for (const IndexedEvent& record : voice_indexed.measures[ordinal]) {
        const VoiceEvent& event = content.events()[record.event_index];
        const Rational    onset = record.onset;
        if (previous != nullptr && event_is_beamable(*previous) &&
            event_is_beamable(event)) {
          const auto left_measure =
              measure_map.measure_index_at(previous_onset);
          const auto right_measure = measure_map.measure_index_at(onset);
          if (left_measure == right_measure && left_measure.has_value()) {
            const TimeSignature meter =
                measure_map.measure(*left_measure).time_signature;
            const bool compound = meter.numerator() > 3 &&
                                  meter.numerator() % 3 == 0 &&
                                  meter.denominator() == 8;
            const Rational beat =
                compound ? *Rational::create(3, 8)
                         : *Rational::create(1, meter.denominator());
            const int left_beat = static_cast<int>(
                ((previous_onset - measure_map.measure_start(*left_measure)) /
                 beat)
                    .to_double());
            const int right_beat = static_cast<int>(
                ((onset - measure_map.measure_start(*right_measure)) / beat)
                    .to_double());
            if (left_beat == right_beat) {
              insert_beam(std::pair{event_id(*previous), event_id(event)});
            }
          }
        }
        previous       = &event;
        previous_onset = onset;
      }
    }
    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.beam_overrides, measures)) {
      const BeamOverride& override =
          voice_indexed.beam_overrides.entries.at(id).record;
      if (std::ranges::any_of(
              voice_indexed.diagnostics, [&](const auto& diagnostic) {
                return diagnostic.entity_id == override.id &&
                       diagnostic.code ==
                           NotationDiagnosticCode::kInvalidBeamOverride;
              })) {
        continue;
      }
      for (std::size_t index = 1; index < override.events.size(); ++index) {
        const auto pair =
            std::pair{override.events[index - 1], override.events[index]};
        if (override.kind == BeamOverride::Kind::kJoin) {
          insert_beam(pair);
        } else {
          const auto found = std::ranges::find(beam_pairs, pair);
          if (found != beam_pairs.end()) {
            beam_pairs.erase(found);
          }
        }
      }
    }
  }

  std::size_t group_begin        = 0;
  std::size_t accidental_measure = std::numeric_limits<std::size_t>::max();
  std::map<int, Accidental> accidental_state;
  while (group_begin < placements.size()) {
    std::size_t group_end = group_begin + 1;
    while (group_end < placements.size() &&
           placements[group_end].onset == placements[group_begin].onset) {
      ++group_end;
    }
    std::vector<std::vector<double>> accidental_columns;
    if (placements[group_begin].measure != accidental_measure) {
      accidental_measure = placements[group_begin].measure;
      accidental_state.clear();
    }
    for (std::size_t event_index = group_begin; event_index < group_end;
         ++event_index) {
      const EventPlacement&  placed        = placements[event_index];
      const VoiceEvent&      event         = *placed.event;
      const auto             event_pitches = pitches(event);
      const NotationEntityId entity        = event_id(event);
      const NotationId       semantic{entity.to_string()};
      if (const auto* rest = std::get_if<Rest>(&event)) {
        if (!builder
                 .add_glyph(
                     make_id(rest->id,
                             "voice/" + std::to_string(placed.voice.index()) +
                                 "/" + std::string{kHitSuffixRest}),
                     smufl_codepoint(rest_glyph(rest->duration.base())),
                     {placed.x, placed.anchor_y}, semantic)
                 .has_value()) {
          return false;
        }
        for (std::uint8_t dot = 0; dot < rest->duration.dots(); ++dot) {
          if (!builder
                   .add_glyph(make_id(rest->id, "dot/" + std::to_string(dot)),
                              smufl_codepoint(SmuflGlyph::kAugmentationDot),
                              {placed.x + space * (1.2 + dot * 0.65),
                               placed.anchor_y - space * 0.25},
                              semantic)
                   .has_value()) {
            return false;
          }
        }
      } else {
        const ClefLane* lane = builder.timeline.clef_lane(staff.stave_id);
        const Clef      clef =
            lane == nullptr ? Clef::kTreble : lane->clef_at(placed.onset);
        // Half-extents of one notehead's own hit region, named rather than
        // written inline because the stemless notehead-column region below
        // is defined as exactly the union of them.
        constexpr double    kNoteheadHitHalfWidth  = 0.6;
        constexpr double    kNoteheadHitHalfHeight = 0.225;
        std::vector<double> head_ys;
        double      column_min_x = std::numeric_limits<double>::infinity();
        double      column_max_x = -std::numeric_limits<double>::infinity();
        std::size_t seconds_run  = 0;
        for (std::size_t note_index = 0; note_index < event_pitches.size();
             ++note_index) {
          const auto& [note_id, pitch] = event_pitches[note_index];
          const double y      = pitch_y(pitch, clef, staff.bounds.y, space);
          double       head_x = placed.x;
          if (note_index > 0 && std::abs(y - head_ys.back()) <= space * 0.55) {
            ++seconds_run;
          } else {
            seconds_run = 0;
          }
          if (seconds_run % 2 == 1) {
            head_x += (placed.stem_up ? -1.0 : 1.0) * space * 0.75;
          }
          const bool voice_collision = std::any_of(
              placements.begin() + static_cast<std::ptrdiff_t>(group_begin),
              placements.begin() + static_cast<std::ptrdiff_t>(group_end),
              [&](const EventPlacement& other) {
                if (&other == &placed ||
                    std::holds_alternative<Rest>(*other.event)) {
                  return false;
                }
                const Clef other_clef = lane == nullptr
                                            ? Clef::kTreble
                                            : lane->clef_at(other.onset);
                return std::ranges::any_of(
                    pitches(*other.event), [&](const auto& other_pitch) {
                      return std::abs(pitch_y(other_pitch.second, other_clef,
                                              staff.bounds.y, space) -
                                      y) < space * 0.7;
                    });
              });
          if (voice_collision) {
            head_x += placed.stem_up ? -space * 0.22 : space * 0.22;
          }
          const NotationId head_id =
              make_id(note_id, std::string{kHitSuffixNotehead});
          if (!builder
                   .add_glyph(head_id,
                              smufl_codepoint(
                                  notehead_glyph(event_duration(event).base())),
                              {head_x, y}, NotationId{note_id.to_string()})
                   .has_value()) {
            return false;
          }
          if (!builder.output.hit_regions.empty()) {
            builder.output.hit_regions.back().role     = HitRole::kNotehead;
            builder.output.hit_regions.back().priority = kHitPriorityNotehead;
            builder.output.hit_regions.back().bounds   = {
                head_x - space * kNoteheadHitHalfWidth,
                y - space * kNoteheadHitHalfHeight,
                space * kNoteheadHitHalfWidth * 2.0,
                space * kNoteheadHitHalfHeight * 2.0};
          }
          column_min_x = std::min(column_min_x, head_x);
          column_max_x = std::max(column_max_x, head_x);
          add_ledger_lines(builder, note_id, head_x, y, staff.bounds.y);
          const int        pitch_key = diatonic_index(pitch);
          const auto       state     = accidental_state.find(pitch_key);
          const Accidental prevailing =
              state == accidental_state.end()
                  ? key_accidental(
                        measure_map.measure(placed.measure).key_signature,
                        pitch.letter())
                  : state->second;
          if (pitch.accidental() != prevailing) {
            const auto overlaps_accidental = [&](double occupied_y) {
              return std::abs(y - occupied_y) < space * 1.6;
            };
            std::size_t column = 0;
            while (column < accidental_columns.size() &&
                   std::ranges::any_of(accidental_columns[column],
                                       overlaps_accidental)) {
              ++column;
            }
            if (column == accidental_columns.size()) {
              accidental_columns.push_back({y});
            } else {
              accidental_columns[column].push_back(y);
            }
            const double accidental_x =
                head_x - space * (1.3 + static_cast<double>(column) * 1.05);
            if (!builder
                     .add_glyph(
                         make_id(note_id,
                                 "accidental/column-" + std::to_string(column)),
                         smufl_codepoint(accidental_glyph(pitch.accidental())),
                         {accidental_x, y}, NotationId{note_id.to_string()})
                     .has_value()) {
              return false;
            }
          }
          accidental_state.insert_or_assign(pitch_key, pitch.accidental());
          for (std::uint8_t dot = 0; dot < event_duration(event).dots();
               ++dot) {
            if (!builder
                     .add_glyph(make_id(note_id, "dot/" + std::to_string(dot)),
                                smufl_codepoint(SmuflGlyph::kAugmentationDot),
                                {head_x + space * (1.2 + dot * 0.65),
                                 y - space * 0.25},
                                NotationId{note_id.to_string()})
                     .has_value()) {
              return false;
            }
          }
          head_ys.push_back(y);
        }
        const double stem_x =
            placed.x + (placed.stem_up ? space * 0.65 : -space * 0.65);
        const double head_y   = placed.stem_up
                                    ? *std::ranges::min_element(head_ys)
                                    : *std::ranges::max_element(head_ys);
        const double stem_end = head_y + (placed.stem_up ? -3.5 : 3.5) * space;
        if (event_duration(event).base() != NoteValue::kWhole) {
          const NotationId stem_id =
              make_id(entity, std::string{kHitSuffixStem});
          builder.add_line(stem_id, {stem_x, head_y}, {stem_x, stem_end},
                           space * 0.12);
          // A stem's own drawn width (space * 0.12) is too thin to click
          // comfortably; widen the hit target to half a staff-space while
          // staying well short of the notehead's own tightened width
          // (space * 1.2, kHitPriorityNotehead) so an overlap between the
          // two -- the stem sits right at that notehead's edge, space * 0.65
          // out from its head_x versus the notehead's own space * 0.6
          // half-width -- is resolved by priority, not accidentally avoided
          // by geometry.
          // Role kEvent (not a new HitRole) matches the existing rest/flag
          // convention: kEvent already means "the whole event", which is
          // exactly what clicking a stem (as opposed to one notehead of a
          // chord) selects.
          constexpr double kStemHitHalfWidth = 0.25;
          const double     stem_hit_width    = space * kStemHitHalfWidth * 2.0;
          const double     stem_top          = std::min(head_y, stem_end);
          const double     stem_height       = std::abs(stem_end - head_y);
          builder.add_hit(stem_id, semantic, HitRole::kEvent,
                          NotationRect{stem_x - space * kStemHitHalfWidth,
                                       stem_top, stem_hit_width, stem_height},
                          kHitPriorityGlyph);
        } else {
          // NoteValue::kWhole is the longest value the enum has (there is no
          // breve or longa), so "not stemmed" and "whole" are the same
          // predicate here and this else is exactly the stemless case. A
          // stemless event has no stem region, and so -- before this region
          // existed -- no kEvent geometry at all: a whole-note chord could
          // only ever be clicked into a one-notehead NoteheadSet, never a
          // ChordSet.
          //
          // The substitute is the bounding box of the event's own noteheads,
          // i.e. the union of their own hit regions, which for a chord
          // additionally covers the vertical gaps between them.
          //
          // kHitPriorityNoteheadColumn is a rank of its own, strictly above
          // the containers (so a click in the column beats the blank-staff
          // insertion caret) and strictly below every engraved object (so
          // each notehead, and each per-notehead accidental and augmentation
          // dot the column necessarily overlaps, keeps selecting its own
          // notehead). Neither of the two cheaper separations works here:
          // the equal-priority smaller-area tie-break would decide those
          // overlaps by the font's glyph metrics, and geometry cannot
          // separate them at all, because the seconds rule (space * 0.75)
          // and the voice-collision rule (space * 0.22) both displace a
          // notehead horizontally and can carry column_min_x/column_max_x
          // out past an accidental's own space * 1.3 or a dot's space * 1.2
          // placement offset. A tie's span segment ranks above the column
          // (kHitPrioritySpanSegment > kHitPriorityNoteheadColumn), but its
          // hit region is now tight to the actual drawn tie curve
          // (add_span_segment, kHitRoleTie branch), not a universal
          // four-staff-space band, so a close-voiced tied stemless chord's
          // column affordance is no longer shadowed away from the curve.
          //
          // head_ys is non-empty here for the same reason the head_y above
          // may dereference min_element/max_element on it unconditionally.
          const NotationId column_id =
              make_id(entity, std::string{kHitSuffixNoteheadColumn});
          const double column_top = *std::ranges::min_element(head_ys) -
                                    space * kNoteheadHitHalfHeight;
          const double column_bottom = *std::ranges::max_element(head_ys) +
                                       space * kNoteheadHitHalfHeight;
          builder.add_hit(
              column_id, semantic, HitRole::kEvent,
              NotationRect{column_min_x - space * kNoteheadHitHalfWidth,
                           column_top,
                           column_max_x - column_min_x +
                               space * kNoteheadHitHalfWidth * 2.0,
                           column_bottom - column_top},
              kHitPriorityNoteheadColumn);
          builder.output.hit_regions.back().owner_system_id = system.id;
          builder.output.hit_regions.back().owner_staff_id  = staff.id;
        }
        const bool beamed =
            std::ranges::any_of(beam_pairs, [&](const auto& pair) {
              return pair.first == entity || pair.second == entity;
            });
        if (event_is_beamable(event) && !beamed) {
          const auto level =
              static_cast<std::size_t>(event_duration(event).base()) -
              static_cast<std::size_t>(NoteValue::kEighth);
          const auto     base = placed.stem_up ? SmuflGlyph::kFlag8thUp
                                               : SmuflGlyph::kFlag8thDown;
          const char32_t flag = smufl_codepoint(
              static_cast<SmuflGlyph>(static_cast<std::uint8_t>(base) + level));
          if (!builder
                   .add_glyph(make_id(entity, "flag"), flag, {stem_x, stem_end},
                              semantic)
                   .has_value()) {
            return false;
          }
        }
        if (const auto* articulations = event_articulations(event)) {
          const auto duration_count =
              std::ranges::count_if(*articulations, is_duration_articulation);
          for (std::size_t index = 0; index < articulations->size(); ++index) {
            if (duration_count > 1 &&
                is_duration_articulation((*articulations)[index])) {
              continue;
            }
            const double y = head_y + (placed.stem_up ? 1.0 : -1.0) * space *
                                          (2.0 + static_cast<double>(index));
            const NotationId id =
                make_id(entity, std::string(kHitRoleArticulation) + "/" +
                                    std::to_string(index));
            if (!builder
                     .add_glyph(id,
                                smufl_codepoint(articulation_glyph(
                                    (*articulations)[index])),
                                {placed.x, y}, semantic)
                     .has_value()) {
              return false;
            }
            builder.output.hit_regions.back().role = HitRole::kMarking;
          }
        }
      }
    }
    group_begin = group_end;
  }

  for (const auto& [left_id, right_id] : beam_pairs) {
    const auto left_found = std::ranges::find_if(
        placements,
        [&](const auto& value) { return event_id(*value.event) == left_id; });
    const auto right_found = std::ranges::find_if(
        placements,
        [&](const auto& value) { return event_id(*value.event) == right_id; });
    const EventPlacement* left =
        left_found == placements.end() ? nullptr : &*left_found;
    const EventPlacement* right =
        right_found == placements.end() ? nullptr : &*right_found;
    for (std::uint8_t voice_index = Voice::kMin;
         voice_index <= Voice::kMax && (left == nullptr || right == nullptr);
         ++voice_index) {
      const Voice voice = *Voice::create(voice_index);
      if (left == nullptr) {
        left = resolve_event(voice, left_id);
      }
      if (right == nullptr) {
        right = resolve_event(voice, right_id);
      }
    }
    if (left == nullptr || right == nullptr) {
      continue;
    }
    const Rational beam_system_start =
        measure_map.measure_start(measures.front().ordinal);
    const Rational beam_system_end =
        measure_map.measure_start(measures.back().ordinal) +
        measure_map.measure_length(measures.back().ordinal);
    if (right->onset < beam_system_start || left->onset >= beam_system_end) {
      continue;
    }
    const bool   up     = left->stem_up;
    const double left_y = left->anchor_y + (up ? -3.5 : 3.5) * space;
    const double natural_right_y =
        right->anchor_y + (right->stem_up ? -3.5 : 3.5) * space;
    const double right_y =
        left_y + std::clamp(natural_right_y - left_y, -space, space);
    const double right_stem_x =
        (right->onset >= beam_system_end
             ? measures.back().bounds.x + measures.back().bounds.width -
                   space * 0.5
             : right->x + (right->stem_up ? 0.65 : -0.65) * space);
    if (natural_right_y != right_y) {
      builder.add_line(
          make_id(right_id, "beam-stem-extension/from/" + left_id.to_string()),
          {right_stem_x, natural_right_y}, {right_stem_x, right_y},
          space * 0.12);
    }
    const std::size_t levels =
        std::min(
            static_cast<std::size_t>(event_duration(*left->event).base()),
            static_cast<std::size_t>(event_duration(*right->event).base())) -
        static_cast<std::size_t>(NoteValue::kEighth) + 1;
    for (std::size_t level = 0; level < levels; ++level) {
      const double offset =
          (up ? 1.0 : -1.0) * static_cast<double>(level) * space * 0.65;
      builder.add_line(make_id(left_id, "beam/to/" + right_id.to_string() +
                                            "/level/" + std::to_string(level)),
                       {left->onset < beam_system_start
                            ? measures.front().bounds.x + space * 0.5
                            : left->x + (up ? 0.65 : -0.65) * space,
                        left_y + offset},
                       {right_stem_x, right_y + offset}, space * 0.5);
    }
  }

  const Rational system_start =
      measure_map.measure_start(measures.front().ordinal);
  const auto&    last_measure = measures.back();
  const Rational system_end = measure_map.measure_start(last_measure.ordinal) +
                              measure_map.measure_length(last_measure.ordinal);
  const double left_x = measures.front().bounds.x + space * 0.5;
  const double right_x =
      last_measure.bounds.x + last_measure.bounds.width - space * 0.5;
  const auto span_x = [&](Rational position) {
    if (position <= system_start) {
      return left_x;
    }
    if (position >= system_end) {
      return right_x;
    }
    const auto measure = measure_map.measure_index_at(position);
    const auto local   = *measure - measures.front().ordinal;
    return position_x(measure_map, widths, *measure, position,
                      measures[local].bounds.x, space);
  };

  struct LaneUse {
    double      from = 0.0;
    double      to   = 0.0;
    std::size_t lane = 0;
  };

  std::vector<LaneUse> below_uses;
  std::vector<LaneUse> above_uses;
  const auto allocate_lane = [](std::vector<LaneUse>& uses, double from,
                                double to) {
    std::size_t lane = 0;
    while (std::ranges::any_of(uses, [&](const LaneUse& use) {
      return use.lane == lane && use.from < to && from < use.to;
    })) {
      ++lane;
    }
    uses.push_back({from, to, lane});
    return lane;
  };

  for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
       ++voice_index) {
    const Voice         voice   = *Voice::create(voice_index);
    const VoiceContent& content = voices.voice(voice);
    const IndexedVoice& voice_indexed =
        indexed.voices[voice_index - Voice::kMin];
    if (measures.front().ordinal == 0) {
      for (const NotationDiagnostic& diagnostic : voice_indexed.diagnostics) {
        builder.output.diagnostics.push_back(
            {diagnostic.entity_id,
             "omitted-invalid-reference:" +
                 std::to_string(static_cast<int>(diagnostic.code))});
      }
    }
    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.dynamics, measures)) {
      const DynamicMarking& dynamic =
          voice_indexed.dynamics.entries.at(id).record;
      const EventPlacement* at = resolve_event(voice, dynamic.at_event);
      if (at == nullptr || at->onset < system_start ||
          at->onset >= system_end) {
        continue;
      }
      double            x          = span_x(at->onset);
      const auto        glyphs     = dynamic_glyphs(dynamic.value);
      const std::size_t local_lane = allocate_lane(
          below_uses, x - space * 0.3,
          x + static_cast<double>(glyphs.size()) * space + space * 0.3);
      const double y = staff.bounds.y + staff.bounds.height +
                       space * (2.0 + static_cast<double>(local_lane) * 1.6);
      const NotationId semantic{dynamic.id.to_string()};
      for (std::size_t index = 0; index < glyphs.size(); ++index) {
        if (!builder
                 .add_glyph(
                     make_id(dynamic.id, "glyph/" + std::to_string(index)),
                     smufl_codepoint(glyphs[index]), {x, y}, semantic)
                 .has_value()) {
          return false;
        }
        builder.output.hit_regions.back().role = HitRole::kMarking;
        x += space;
      }
    }
    const auto add_event_span = [&](const auto& span, const std::string& role,
                                    std::size_t stack, bool wedge,
                                    bool reverse) {
      const EventPlacement* start = resolve_event(voice, span.start_event);
      const EventPlacement* end   = resolve_event(voice, span.end_event);
      if (start == nullptr || end == nullptr || !(start->onset < end->onset) ||
          end->onset < system_start || start->onset >= system_end) {
        return;
      }
      if (role == "slur" && (std::holds_alternative<Rest>(*start->event) ||
                             std::holds_alternative<Rest>(*end->event))) {
        return;
      }
      const double          from_x = span_x(start->onset);
      const double          to_x   = span_x(end->onset);
      std::vector<LaneUse>& uses   = role == "slur" ? above_uses : below_uses;
      const std::size_t     local_lane = allocate_lane(uses, from_x, to_x);
      const double          lane =
          role == "slur"
                       ? staff.bounds.y -
                    space * (2.0 + static_cast<double>(local_lane) * 1.6)
                       : staff.bounds.y + staff.bounds.height +
                    space * (4.0 + static_cast<double>(local_lane) * 1.6);
      (void)stack;
      add_span_segment(builder, span.id, NotationId{span.id.to_string()},
                       system, {from_x, lane}, {to_x, lane}, lane, role, wedge,
                       reverse);
    };
    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.hairpins, measures)) {
      const Hairpin& hairpin = voice_indexed.hairpins.entries.at(id).record;
      add_event_span(hairpin, "hairpin", 0, true,
                     hairpin.direction == HairpinDirection::kDiminuendo);
    }
    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.slurs, measures)) {
      const Slur& slur = voice_indexed.slurs.entries.at(id).record;
      add_event_span(slur, "slur", 0, false, false);
    }

    const auto&               events = content.events();
    std::vector<IndexedEvent> local_events;
    if (measures.front().ordinal > 0 &&
        !voice_indexed.measures[measures.front().ordinal - 1].empty()) {
      local_events.push_back(
          voice_indexed.measures[measures.front().ordinal - 1].back());
    }
    for (std::size_t ordinal = measures.front().ordinal;
         ordinal <= measures.back().ordinal; ++ordinal) {
      local_events.insert(local_events.end(),
                          voice_indexed.measures[ordinal].begin(),
                          voice_indexed.measures[ordinal].end());
    }
    for (const IndexedEvent& record : local_events) {
      const VoiceEvent& event = events[record.event_index];
      if (record.event_index + 1 < events.size()) {
        const auto source_pitches = pitches(event);
        const auto target_pitches = pitches(events[record.event_index + 1]);
        for (const auto& [note_id, pitch] : source_pitches) {
          const bool tied = std::visit(
              [&](const auto& concrete) {
                using Event = std::decay_t<decltype(concrete)>;
                if constexpr (std::is_same_v<Event, Note>) {
                  return concrete.id == note_id && concrete.tied_to_next;
                } else if constexpr (std::is_same_v<Event, Chord>) {
                  const auto found = std::ranges::find(concrete.notes, note_id,
                                                       &ChordNote::id);
                  return found != concrete.notes.end() && found->tied_to_next;
                }
                return false;
              },
              event);
          if (!tied ||
              !std::ranges::any_of(target_pitches, [&](const auto& item) {
                return item.second == pitch;
              })) {
            continue;
          }
          const Rational end_onset =
              record.onset + event_duration(event).resolved();
          if (end_onset >= system_start && record.onset < system_end) {
            const ClefLane* lane = builder.timeline.clef_lane(staff.stave_id);
            const Clef      clef =
                lane == nullptr ? Clef::kTreble : lane->clef_at(record.onset);
            const double y =
                pitch_y(pitch, clef, staff.bounds.y, space) + space;
            add_span_segment(builder, note_id, NotationId{note_id.to_string()},
                             system, {span_x(record.onset), y},
                             {span_x(end_onset), y}, y,
                             std::string(kHitRoleTie), false, false);
          }
        }
      }
    }

    for (const NotationEntityId& id :
         system_reference_ids(voice_indexed.grace_groups, measures)) {
      const GraceGroup& group =
          voice_indexed.grace_groups.entries.at(id).record;
      const EventPlacement* principal =
          resolve_event(voice, group.principal_event);
      if (principal == nullptr || principal->onset < system_start ||
          principal->onset >= system_end ||
          std::holds_alternative<Rest>(*principal->event)) {
        continue;
      }
      const ClefLane* lane = builder.timeline.clef_lane(staff.stave_id);
      const Clef      clef =
          lane == nullptr ? Clef::kTreble : lane->clef_at(principal->onset);
      for (std::size_t index = 0; index < group.notes.size(); ++index) {
        const GraceNote& grace = group.notes[index];
        const double distance = static_cast<double>(group.notes.size() - index);
        const double x =
            span_x(principal->onset) - space * (2.0 + distance * 1.3);
        const double y = pitch_y(grace.pitch, clef, staff.bounds.y, space);
        if (!builder
                 .add_glyph(
                     make_id(grace.id, std::string{kHitSuffixGraceNotehead}),
                     smufl_codepoint(notehead_glyph(grace.duration.base())),
                     {x, y}, NotationId{grace.id.to_string()}, 0.65)
                 .has_value()) {
          return false;
        }
        builder.output.hit_regions.back().role     = HitRole::kNotehead;
        builder.output.hit_regions.back().priority = kHitPriorityNotehead;
        const double grace_space                   = space * 0.65;
        const double stem_x                        = x + grace_space * 0.65;
        const double stem_end                      = y - grace_space * 3.5;
        if (grace.duration.base() != NoteValue::kWhole) {
          builder.add_line(make_id(grace.id, "grace-stem"), {stem_x, y},
                           {stem_x, stem_end}, grace_space * 0.12);
        }
        if (grace.duration.base() >= NoteValue::kEighth) {
          const auto level = static_cast<std::size_t>(grace.duration.base()) -
                             static_cast<std::size_t>(NoteValue::kEighth);
          const char32_t flag = smufl_codepoint(static_cast<SmuflGlyph>(
              static_cast<std::uint8_t>(SmuflGlyph::kFlag8thUp) + level));
          if (!builder
                   .add_glyph(make_id(grace.id, "grace-flag"), flag,
                              {stem_x, stem_end}, std::nullopt, 0.65)
                   .has_value()) {
            return false;
          }
        }
        if (grace.pitch.accidental() !=
            key_accidental(
                measure_map.measure(principal->measure).key_signature,
                grace.pitch.letter())) {
          if (!builder
                   .add_glyph(make_id(grace.id, "grace-accidental"),
                              smufl_codepoint(
                                  accidental_glyph(grace.pitch.accidental())),
                              {x - grace_space * 1.5, y}, std::nullopt, 0.65)
                   .has_value()) {
            return false;
          }
        }
        for (std::uint8_t dot = 0; dot < grace.duration.dots(); ++dot) {
          if (!builder
                   .add_glyph(
                       make_id(grace.id, "grace-dot/" + std::to_string(dot)),
                       smufl_codepoint(SmuflGlyph::kAugmentationDot),
                       {x + grace_space * (1.2 + dot * 0.65),
                        y - grace_space * 0.25},
                       std::nullopt, 0.65)
                   .has_value()) {
            return false;
          }
        }
        int ledger = 0;
        for (double ledger_y = staff.bounds.y - space;
             y <= ledger_y + space * 0.25; ledger_y -= space) {
          builder.add_line(make_id(grace.id, "grace-ledger/above/" +
                                                 std::to_string(ledger++)),
                           {x - grace_space * 0.85, ledger_y},
                           {x + grace_space * 0.85, ledger_y},
                           grace_space * 0.12);
        }
        ledger = 0;
        for (double ledger_y = staff.bounds.y + space * 5.0;
             y >= ledger_y - space * 0.25; ledger_y += space) {
          builder.add_line(make_id(grace.id, "grace-ledger/below/" +
                                                 std::to_string(ledger++)),
                           {x - grace_space * 0.85, ledger_y},
                           {x + grace_space * 0.85, ledger_y},
                           grace_space * 0.12);
        }
        if (grace.slashed) {
          builder.add_line(make_id(grace.id, "slash"),
                           {x - grace_space * 0.4, y + grace_space},
                           {x + grace_space * 0.8, y - grace_space},
                           grace_space * 0.12);
        }
      }
    }

    std::size_t local_index = 0;
    while (local_index < local_events.size()) {
      const IndexedEvent& first_record = local_events[local_index];
      const auto          ratio =
          event_duration(events[first_record.event_index]).tuplet();
      const auto group = event_tuplet_group(events[first_record.event_index]);
      const Rational run_start = first_record.onset;
      std::size_t    end       = local_index + 1;
      Rational       run_end =
          run_start +
          event_duration(events[first_record.event_index]).resolved();
      while (
          ratio.has_value() && group.has_value() && end < local_events.size() &&
          event_tuplet_group(events[local_events[end].event_index]) == group) {
        run_end =
            run_end +
            event_duration(events[local_events[end].event_index]).resolved();
        ++end;
      }
      // The domain keys kIncompleteTupletGroup to the run's true global first
      // event (its backward walk over the whole voice), while first_record is
      // only the first event of this system's local fragment -- a mid-run
      // event whenever the run began in an earlier system. Walk back to the
      // true run start before comparing so a malformed run suppresses its
      // digit on every system it spans, not just the one holding the run start.
      const NotationEntityId true_run_start = [&]() {
        if (!ratio.has_value()) {
          return first_record.id;
        }
        std::size_t index = first_record.event_index;
        while (index > 0) {
          if (event_tuplet_group(events[index - 1]) != group) {
            break;
          }
          --index;
        }
        return event_id(events[index]);
      }();
      const auto is_incomplete_tuplet_diagnostic = [&](const auto& diagnostic) {
        return diagnostic.entity_id == true_run_start &&
               diagnostic.code ==
                   NotationDiagnosticCode::kIncompleteTupletGroup;
      };
      if (ratio.has_value() && run_end > system_start &&
          run_start < system_end &&
          !std::ranges::any_of(voice_indexed.diagnostics,
                               is_incomplete_tuplet_diagnostic)) {
        const NotationEntityId id = first_record.id;
        const double y = staff.bounds.y - space * (2.5 + voice_index * 0.7);
        builder.add_line(make_id(id, "tuplet/bracket"), {span_x(run_start), y},
                         {span_x(run_end), y}, space * 0.1);
        const std::string number = tuplet_label(*ratio);
        double            x      = (span_x(run_start) + span_x(run_end)) * 0.5 -
                   static_cast<double>(number.size()) * space * 0.4;
        for (std::size_t digit = 0; digit < number.size(); ++digit) {
          const char32_t code =
              number[digit] == ':'
                  ? smufl_codepoint(SmuflGlyph::kTupletColon)
                  : smufl_codepoint(SmuflGlyph::kTupletDigit0) +
                        static_cast<char32_t>(number[digit] - '0');
          if (!builder
                   .add_glyph(make_id(id, std::string(kHitRoleTupletDigit) +
                                              "/" + std::to_string(digit)),
                              code, {x, y}, NotationId{id.to_string()})
                   .has_value()) {
            return false;
          }
          builder.output.hit_regions.back().role = HitRole::kMarking;
          x += space * 0.8;
        }
      }
      local_index = end;
    }
  }
  return true;
}

}  // namespace graphscore
