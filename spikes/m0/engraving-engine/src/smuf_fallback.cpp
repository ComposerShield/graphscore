// SPDX-License-Identifier: Apache-2.0

#include "smuf_fallback.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace engraving {
namespace {

constexpr float kStaffSpacing = 6.0F;
constexpr float kMinimumStaffWidth = 128.0F;
constexpr float kMaximumStaffWidth = 1000000.0F;
constexpr float kMaximumLayoutWidth = 10000000.0F;
constexpr float kNoteheadWidth = 10.0F;
constexpr float kVoiceColumnStride = 12.0F;
constexpr float kVoiceRightExtent = 16.0F;
constexpr float kMinimumTimelineWidth = 30.0F;
constexpr std::uint32_t kGClef = 0xE050;
constexpr std::uint32_t kFClef = 0xE062;
constexpr std::uint32_t kCClef = 0xE069;
constexpr std::uint32_t kNoteheadBlack = 0xE0A4;
constexpr std::uint32_t kNoteheadHalf = 0xE0A3;
constexpr std::uint32_t kNoteheadWhole = 0xE0A2;
constexpr std::uint32_t kAccidentalSharp = 0xE262;
constexpr std::uint32_t kAccidentalFlat = 0xE260;
constexpr std::uint32_t kAccidentalNatural = 0xE261;
constexpr std::uint32_t kAccidentalDoubleSharp = 0xE263;
constexpr std::uint32_t kAccidentalDoubleFlat = 0xE264;
constexpr std::uint32_t kArticStaccatoAbove = 0xE4A0;
constexpr std::uint32_t kArticAccentAbove = 0xE4A6;
constexpr std::uint32_t kArticTenutoAbove = 0xE4A8;
constexpr std::uint32_t kArticStaccatissimoAbove = 0xE4A4;
constexpr std::uint32_t kArticMarcatoAbove = 0xE4AC;
constexpr std::uint32_t kFlag8thUp = 0xE240;
constexpr std::uint32_t kTimeSig0 = 0xE080;

struct GlyphMetrics {
    std::uint32_t codepoint = 0;
    float width = 0.0F;
    float height = 0.0F;
};

bool IsFinite(const float value) {
    return std::isfinite(value);
}

bool IsFiniteAndBounded(const float value, const float maximum) {
    return IsFinite(value) && value >= 0.0F && value <= maximum;
}

bool IsPitchValid(const Pitch& pitch) {
    return pitch.step >= 0 && pitch.step <= 6 && pitch.octave >= 0 && pitch.octave <= 9 &&
           pitch.accidental >= Accidental::None && pitch.accidental <= Accidental::DoubleFlat;
}

bool IsDurationValid(const NoteDuration duration) {
    return duration >= NoteDuration::Whole && duration <= NoteDuration::ThirtySecond;
}

bool IsArticulationValid(const Articulation articulation) {
    return articulation >= Articulation::None && articulation <= Articulation::Marcato;
}

float DurationUnits(const NoteDuration duration) {
    switch (duration) {
    case NoteDuration::Whole:
        return 4.0F;
    case NoteDuration::Half:
        return 2.0F;
    case NoteDuration::Quarter:
        return 1.0F;
    case NoteDuration::Eighth:
        return 0.5F;
    case NoteDuration::Sixteenth:
        return 0.25F;
    case NoteDuration::ThirtySecond:
        return 0.125F;
    }
    return 0.0F;
}

bool IsXWithinLayout(const float x, const float totalWidth) {
    return IsFinite(x) && x >= 0.0F &&
           static_cast<double>(x) <= static_cast<double>(totalWidth) + 0.01;
}

bool IsRectValid(const Rect& box, const float totalWidth) {
    const double right = static_cast<double>(box.x) + static_cast<double>(box.w);
    return IsFinite(box.x) && IsFinite(box.y) && IsFinite(box.w) && IsFinite(box.h) &&
            box.w > 0.0F && box.h > 0.0F && box.x >= 0.0F && std::isfinite(right) &&
            right <= static_cast<double>(totalWidth) + 0.01;
}

bool IsLayoutElementValid(const LayoutElement& element, const float totalWidth) {
    return IsFinite(element.position.x) && IsFinite(element.position.y) &&
           IsFinite(element.endPoint.x) && IsFinite(element.endPoint.y) &&
           IsFinite(element.controlPoint.x) && IsFinite(element.controlPoint.y) &&
           IsXWithinLayout(element.position.x, totalWidth) &&
           IsXWithinLayout(element.endPoint.x, totalWidth) &&
           IsXWithinLayout(element.controlPoint.x, totalWidth) &&
           IsRectValid(element.boundingBox, totalWidth);
}

bool IsPriorLayoutValid(const LayoutResult& layout) {
    if (!layout.valid || !IsFiniteAndBounded(layout.totalWidth, kMaximumLayoutWidth) ||
        layout.totalWidth <= 0.0F || layout.elements.empty()) {
        return false;
    }
    for (const LayoutElement& element : layout.elements) {
        if (!IsLayoutElementValid(element, layout.totalWidth)) {
            return false;
        }
    }
    return true;
}

bool IsShortDuration(const NoteDuration duration) {
    return duration == NoteDuration::Eighth ||
           duration == NoteDuration::Sixteenth ||
           duration == NoteDuration::ThirtySecond;
}

GlyphMetrics NoteheadFor(const NoteDuration duration) {
    switch (duration) {
    case NoteDuration::Whole:
        return {kNoteheadWhole, kNoteheadWidth, kNoteheadWidth};
    case NoteDuration::Half:
        return {kNoteheadHalf, kNoteheadWidth, kNoteheadWidth};
    case NoteDuration::Quarter:
    case NoteDuration::Eighth:
    case NoteDuration::Sixteenth:
    case NoteDuration::ThirtySecond:
        return {kNoteheadBlack, kNoteheadWidth, kNoteheadWidth};
    }
    return {kNoteheadBlack, kNoteheadWidth, kNoteheadWidth};
}

GlyphMetrics AccidentalFor(const Accidental accidental) {
    switch (accidental) {
    case Accidental::Sharp:
        return {kAccidentalSharp, 8.0F, 14.0F};
    case Accidental::Flat:
        return {kAccidentalFlat, 8.0F, 12.0F};
    case Accidental::Natural:
        return {kAccidentalNatural, 8.0F, 14.0F};
    case Accidental::DoubleSharp:
        return {kAccidentalDoubleSharp, 10.0F, 14.0F};
    case Accidental::DoubleFlat:
        return {kAccidentalDoubleFlat, 10.0F, 12.0F};
    case Accidental::None:
        return {};
    }
    return {};
}

GlyphMetrics ArticulationFor(const Articulation articulation) {
    switch (articulation) {
    case Articulation::Staccato:
        return {kArticStaccatoAbove, 6.0F, 6.0F};
    case Articulation::Accent:
        return {kArticAccentAbove, 10.0F, 8.0F};
    case Articulation::Tenuto:
        return {kArticTenutoAbove, 8.0F, 6.0F};
    case Articulation::Staccatissimo:
        return {kArticStaccatissimoAbove, 6.0F, 10.0F};
    case Articulation::Marcato:
        return {kArticMarcatoAbove, 10.0F, 10.0F};
    case Articulation::None:
        return {};
    }
    return {};
}

std::uint32_t ClefCodepoint(const Clef clef) {
    switch (clef) {
    case Clef::Treble:
        return kGClef;
    case Clef::Bass:
        return kFClef;
    case Clef::Alto:
    case Clef::Tenor:
        return kCClef;
    }
    return kGClef;
}

std::uint32_t AccidentalCodepoint(const bool sharp) {
    return sharp ? kAccidentalSharp : kAccidentalFlat;
}

std::uint32_t TimeDigit(const int digit) {
    return kTimeSig0 + static_cast<std::uint32_t>(digit);
}

Rect BoundsForCurve(
    const Point2D start,
    const Point2D control,
    const Point2D end)
{
    const float minimumX = std::min({start.x, control.x, end.x});
    const float maximumX = std::max({start.x, control.x, end.x});
    const float minimumY = std::min({start.y, control.y, end.y});
    const float maximumY = std::max({start.y, control.y, end.y});
    return {minimumX, minimumY, std::max(1.0F, maximumX - minimumX),
            std::max(1.0F, maximumY - minimumY)};
}

bool HasInvalidInput(const Measure& measure, std::string& diagnostic) {
    if (!IsFinite(measure.staffWidth) || measure.staffWidth < kMinimumStaffWidth ||
        measure.staffWidth > kMaximumStaffWidth) {
        diagnostic = "staffWidth must be finite and between 128 and 1000000";
        return true;
    }
    if (measure.keyFifths < -7 || measure.keyFifths > 7) {
        diagnostic = "keyFifths must be between -7 and 7";
        return true;
    }
    if (measure.timeNumerator < 1 || measure.timeNumerator > 9 ||
        (measure.timeDenominator != 2 && measure.timeDenominator != 4 &&
         measure.timeDenominator != 8)) {
        diagnostic = "time signature must use a one-digit numerator and 2, 4, or 8";
        return true;
    }
    if (measure.voices.empty() || measure.voices.size() > 4U) {
        diagnostic = "measure must contain one to four voices";
        return true;
    }
    for (const Voice& voice : measure.voices) {
        bool hasPrecedingAnchor = false;
        for (const Note& note : voice.notes) {
            if (!IsPitchValid(note.pitch) || !IsDurationValid(note.duration)) {
                diagnostic = "note pitch or duration is outside the supported spike range";
                return true;
            }
            for (const Articulation articulation : note.articulations) {
                if (!IsArticulationValid(articulation)) {
                    diagnostic = "articulation is outside the supported spike range";
                    return true;
                }
            }
            if (note.grace && !hasPrecedingAnchor) {
                diagnostic = "grace note requires a preceding anchored note";
                return true;
            }
            if (!note.grace) {
                hasPrecedingAnchor = true;
            }
        }
        for (const Tuplet& tuplet : voice.tuplets) {
            if (tuplet.actualNotes < 2 || tuplet.actualNotes > 9 || tuplet.normalNotes < 1 ||
                tuplet.normalNotes > 9 ||
                tuplet.notes.empty()) {
                diagnostic = "tuplet must have positive ratios and notes";
                return true;
            }
            for (const Note& note : tuplet.notes) {
                if (!IsPitchValid(note.pitch) || !IsDurationValid(note.duration)) {
                    diagnostic = "tuplet note pitch or duration is outside the supported spike range";
                    return true;
                }
                for (const Articulation articulation : note.articulations) {
                    if (!IsArticulationValid(articulation)) {
                        diagnostic = "tuplet articulation is outside the supported spike range";
                        return true;
                    }
                }
                if (note.grace && !hasPrecedingAnchor) {
                    diagnostic = "grace note requires a preceding anchored note";
                    return true;
                }
                if (!note.grace) {
                    hasPrecedingAnchor = true;
                }
            }
        }
        for (const SlurSpan& slur : voice.slurs) {
            if (slur.startNote >= voice.notes.size() ||
                slur.endNote >= voice.notes.size() ||
                slur.startNote >= slur.endNote) {
                diagnostic = "slur span must reference increasing note indexes";
                return true;
            }
        }
    }
    return false;
}

int HitPriority(const LayoutElement::Type type) {
    switch (type) {
    case LayoutElement::Type::Notehead:
        return 0;
    case LayoutElement::Type::AccidentalGlyph:
        return 1;
    case LayoutElement::Type::ArticulationGlyph:
        return 2;
    default:
        return 3;
    }
}

} // namespace

bool SmuflFallbackEngraver::Initialize() {
    m_initialized = true;
    return true;
}

void SmuflFallbackEngraver::Shutdown() {
    m_initialized = false;
}

LayoutResult SmuflFallbackEngraver::LayoutMeasure(
    const Measure& measure,
    const Measure* context,
    const LayoutResult* appendTo)
{
    LayoutResult result;
    result.incremental = true;
    result.work.reusedPrecedingLayout = appendTo != nullptr;
    if (!m_initialized) {
        result.valid = false;
        result.diagnostic = "engraver is not initialized";
        return result;
    }
    if (HasInvalidInput(measure, result.diagnostic)) {
        result.valid = false;
        return result;
    }
    if (appendTo != nullptr && !IsPriorLayoutValid(*appendTo)) {
        result.valid = false;
        result.diagnostic = "appended layout has non-finite or out-of-range geometry";
        return result;
    }

    const float xOffset = appendTo == nullptr ? 0.0F : appendTo->totalWidth;
    if (static_cast<double>(xOffset) + static_cast<double>(measure.staffWidth) >
        static_cast<double>(kMaximumLayoutWidth)) {
        result.valid = false;
        result.diagnostic = "appended layout width exceeds the spike safety bound";
        return result;
    }
    const float staffEnd = xOffset + measure.staffWidth;
    const float staffTop = 0.0F;
    const float staffBottom = kStaffSpacing * 4.0F;
    const std::string prefix = measure.stableId.empty() ? "measure" : measure.stableId;
    const int keyCount = std::abs(measure.keyFifths);
    const float headerStart = xOffset + 22.0F;
    const float musicStart = headerStart + static_cast<float>(keyCount) * 7.0F + 16.0F;
    const float musicEnd = staffEnd - 8.0F;
    const float availableMusicWidth = musicEnd - musicStart;
    const float maximumVoiceOffset =
        static_cast<float>(measure.voices.size() - 1U) * kVoiceColumnStride;
    const float timelineWidth = availableMusicWidth - maximumVoiceOffset - kVoiceRightExtent;
    float maximumTimelineDuration = 0.0F;
    for (const Voice& voice : measure.voices) {
        float timelineDuration = 0.0F;
        for (const Note& note : voice.notes) {
            if (!note.grace) {
                timelineDuration += DurationUnits(note.duration);
            }
        }
        for (const Tuplet& tuplet : voice.tuplets) {
            const float ratio = static_cast<float>(tuplet.normalNotes) /
                                static_cast<float>(tuplet.actualNotes);
            for (const Note& note : tuplet.notes) {
                if (!note.grace) {
                    timelineDuration += DurationUnits(note.duration) * ratio;
                }
            }
        }
        maximumTimelineDuration = std::max(maximumTimelineDuration, timelineDuration);
    }
    if (!IsFinite(maximumTimelineDuration) || maximumTimelineDuration <= 0.0F) {
        result.valid = false;
        result.diagnostic = "measure has no timed note events";
        return result;
    }
    if (!IsFinite(timelineWidth) || timelineWidth < kMinimumTimelineWidth) {
        result.valid = false;
        result.diagnostic = "staff width cannot reserve header, voice columns, and timeline";
        return result;
    }
    auto add = [&result](LayoutElement element) {
        result.elements.push_back(std::move(element));
    };
    auto glyph = [&add](
                     const LayoutElement::Type type,
                     const std::string& id,
                     const std::string& role,
                     const Point2D position,
                     const GlyphMetrics metrics,
                     const int voiceIndex,
                     const float scale = 1.0F) {
        const float width = metrics.width * scale;
        const float height = metrics.height * scale;
        LayoutElement element;
        element.type = type;
        element.geometry = GeometryKind::Glyph;
        element.position = position;
        element.boundingBox = {position.x, position.y - height * 0.5F, width, height};
        element.voiceIndex = voiceIndex;
        element.smuflCodepoint = metrics.codepoint;
        element.id = id;
        element.role = role;
        add(std::move(element));
    };
    auto line = [&add](
                    const LayoutElement::Type type,
                    const std::string& id,
                    const std::string& role,
                    const Point2D start,
                    const Point2D end,
                    const int voiceIndex = -1) {
        const float thickness = 1.0F;
        LayoutElement element;
        element.type = type;
        element.geometry = GeometryKind::Line;
        element.position = start;
        element.endPoint = end;
        element.boundingBox = {std::min(start.x, end.x), std::min(start.y, end.y),
                               std::max(thickness, std::fabs(end.x - start.x)),
                               std::max(thickness, std::fabs(end.y - start.y))};
        element.voiceIndex = voiceIndex;
        element.id = id;
        element.role = role;
        add(std::move(element));
    };
    auto curve = [&add](
                     const LayoutElement::Type type,
                     const std::string& id,
                     const std::string& role,
                     const Point2D start,
                     const Point2D control,
                     const Point2D end,
                     const int voiceIndex) {
        LayoutElement element;
        element.type = type;
        element.geometry = GeometryKind::Curve;
        element.position = start;
        element.endPoint = end;
        element.controlPoint = control;
        element.boundingBox = BoundsForCurve(start, control, end);
        element.voiceIndex = voiceIndex;
        element.id = id;
        element.role = role;
        add(std::move(element));
    };

    for (int lineIndex = 0; lineIndex < 5; ++lineIndex) {
        const float y = staffTop + static_cast<float>(lineIndex) * kStaffSpacing;
        line(LayoutElement::Type::StaffLine,
             prefix + ":staff-line:" + std::to_string(lineIndex), "staff-line",
             {xOffset, y}, {staffEnd, y});
    }

    const bool hasContext = context != nullptr;
    const float clefY = measure.clef == Clef::Treble ? 12.0F : 15.0F;
    glyph(LayoutElement::Type::Clef, prefix + ":clef", "clef", {xOffset + 4.0F, clefY},
           {ClefCodepoint(measure.clef), 14.0F, 24.0F}, -1);
    float headerX = headerStart;
    for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex) {
        const float y = 9.0F + static_cast<float>((keyIndex * 4) % 7) * 2.0F;
        glyph(LayoutElement::Type::KeySig,
              prefix + ":key:" + std::to_string(keyIndex), "key-signature",
              {headerX, y}, {AccidentalCodepoint(measure.keyFifths > 0), 7.0F, 12.0F}, -1);
        headerX += 7.0F;
    }
    glyph(LayoutElement::Type::TimeSig, prefix + ":time:numerator", "time-signature",
          {headerX + 2.0F, 8.0F}, {TimeDigit(measure.timeNumerator), 9.0F, 12.0F}, -1);
    glyph(LayoutElement::Type::TimeSig, prefix + ":time:denominator", "time-signature",
          {headerX + 2.0F, 20.0F}, {TimeDigit(measure.timeDenominator), 9.0F, 12.0F}, -1);
    for (std::size_t voiceIndex = 0; voiceIndex < measure.voices.size(); ++voiceIndex) {
        const Voice& voice = measure.voices[voiceIndex];
        ++result.work.visitedVoices;
        const int voiceNumber = static_cast<int>(voiceIndex);
        const float collisionOffset = static_cast<float>(voiceIndex) * kVoiceColumnStride;
        std::vector<Point2D> notePositions(voice.notes.size());
        Point2D previousShortStem {};
        bool hasPreviousShortStem = false;
        auto emitNote = [&](const Note& note,
                             const std::string& identity,
                             const float onset,
                             const int tupletActualNotes,
                             const int tupletNormalNotes) {
            ++result.work.visitedNotes;
            const float normalizedTime = onset / maximumTimelineDuration;
            const float slotX = musicStart + normalizedTime * timelineWidth + collisionOffset;
            const float y = StaffStepToY(note.pitch.step, note.pitch.octave);
            const GlyphMetrics notehead = NoteheadFor(note.duration);
            const float noteScale = note.grace ? 0.60F : 1.0F;
            const float x = slotX;
            glyph(LayoutElement::Type::Notehead, identity + ":notehead",
                  note.grace ? "grace-notehead" : "notehead", {x, y}, notehead,
                  voiceNumber, noteScale);
            result.elements.back().normalizedTime = normalizedTime;
            result.elements.back().tupletActualNotes = tupletActualNotes;
            result.elements.back().tupletNormalNotes = tupletNormalNotes;
            if (note.pitch.accidental != Accidental::None) {
                const GlyphMetrics accidental = AccidentalFor(note.pitch.accidental);
                glyph(LayoutElement::Type::AccidentalGlyph, identity + ":accidental",
                      "accidental", {x - accidental.width - 2.0F, y}, accidental,
                      voiceNumber, noteScale);
                result.elements.back().normalizedTime = normalizedTime;
                result.elements.back().tupletActualNotes = tupletActualNotes;
                result.elements.back().tupletNormalNotes = tupletNormalNotes;
            }
            if (note.duration != NoteDuration::Whole) {
                const bool stemUp = (voiceIndex % 2U) == 0U;
                const Point2D stemStart = {x + (stemUp ? notehead.width * noteScale : 1.0F), y};
                const Point2D stemEnd = {stemStart.x, y + (stemUp ? -24.0F : 24.0F)};
                line(LayoutElement::Type::Stem, identity + ":stem", "stem", stemStart,
                     stemEnd, voiceNumber);
                if (IsShortDuration(note.duration)) {
                    glyph(LayoutElement::Type::Flag, identity + ":flag", "flag",
                          stemEnd, {kFlag8thUp, 6.0F, 12.0F}, voiceNumber, noteScale);
                    if (hasPreviousShortStem) {
                        line(LayoutElement::Type::Beam, identity + ":beam", "beam",
                             previousShortStem, stemEnd, voiceNumber);
                    }
                    previousShortStem = stemEnd;
                    hasPreviousShortStem = true;
                } else {
                    hasPreviousShortStem = false;
                }
            }
            if (y < staffTop || y > staffBottom) {
                line(LayoutElement::Type::LedgerLine, identity + ":ledger", "ledger-line",
                     {x - 3.0F, y}, {x + notehead.width * noteScale + 3.0F, y}, voiceNumber);
            }
            float articulationY = y - 13.0F;
            for (std::size_t articulationIndex = 0;
                 articulationIndex < note.articulations.size(); ++articulationIndex) {
                const Articulation articulation = note.articulations[articulationIndex];
                if (articulation == Articulation::None) {
                    continue;
                }
                const GlyphMetrics marking = ArticulationFor(articulation);
                articulationY -= marking.height * noteScale * 0.5F;
                glyph(LayoutElement::Type::ArticulationGlyph,
                       identity + ":articulation:" + std::to_string(articulationIndex),
                       "articulation", {x + 2.0F, articulationY}, marking, voiceNumber,
                       noteScale);
                result.elements.back().normalizedTime = normalizedTime;
                result.elements.back().tupletActualNotes = tupletActualNotes;
                result.elements.back().tupletNormalNotes = tupletNormalNotes;
                articulationY -= marking.height * noteScale * 0.5F + 2.0F;
            }
        };

        float timelineCursor = 0.0F;
        float anchorStart = 0.0F;
        float anchorDuration = 0.0F;
        float stolenDuration = 0.0F;
        for (std::size_t noteIndex = 0; noteIndex < voice.notes.size(); ++noteIndex) {
            const Note& note = voice.notes[noteIndex];
            float onset = timelineCursor;
            if (note.grace) {
                const float steal = std::min(DurationUnits(note.duration) * 0.5F,
                                             anchorDuration * 0.5F - stolenDuration);
                onset = anchorStart + anchorDuration - stolenDuration - steal;
                stolenDuration += steal;
            } else {
                anchorStart = timelineCursor;
                anchorDuration = DurationUnits(note.duration);
                stolenDuration = 0.0F;
                timelineCursor += anchorDuration;
            }
            emitNote(note,
                     prefix + ":voice:" + std::to_string(voiceIndex) + ":note:" +
                         std::to_string(noteIndex),
                     onset, 0, 0);
            notePositions[noteIndex] = {
                musicStart + (onset / maximumTimelineDuration) * timelineWidth + collisionOffset,
                StaffStepToY(note.pitch.step, note.pitch.octave)};
        }
        for (std::size_t tupletIndex = 0; tupletIndex < voice.tuplets.size(); ++tupletIndex) {
            const Tuplet& tuplet = voice.tuplets[tupletIndex];
            const std::string tupletId = prefix + ":voice:" + std::to_string(voiceIndex) +
                                         ":tuplet:" + std::to_string(tupletIndex);
            const float tupletStart = timelineCursor;
            const float ratio = static_cast<float>(tuplet.normalNotes) /
                                static_cast<float>(tuplet.actualNotes);
            float tupletDuration = 0.0F;
            for (const Note& note : tuplet.notes) {
                if (!note.grace) {
                    tupletDuration += DurationUnits(note.duration) * ratio;
                }
            }
            const float bracketStart = musicStart +
                                       (tupletStart / maximumTimelineDuration) * timelineWidth +
                                       collisionOffset;
            const float bracketEnd = musicStart +
                                     ((tupletStart + tupletDuration) / maximumTimelineDuration) *
                                         timelineWidth +
                                     collisionOffset;
            line(LayoutElement::Type::TupletBracket, tupletId + ":bracket", "tuplet-bracket",
                 {bracketStart, -13.0F}, {bracketEnd, -13.0F}, voiceNumber);
            result.elements.back().tupletActualNotes = tuplet.actualNotes;
            result.elements.back().tupletNormalNotes = tuplet.normalNotes;
            const float tupletNumberX = std::clamp(
                (bracketStart + bracketEnd) * 0.5F - 4.0F, musicStart, musicEnd - 17.0F);
            glyph(LayoutElement::Type::TupletBracket, tupletId + ":actual-number",
                   "tuplet-actual-number",
                   {tupletNumberX, -16.0F},
                    {TimeDigit(tuplet.actualNotes), 8.0F, 10.0F}, voiceNumber);
            result.elements.back().tupletActualNotes = tuplet.actualNotes;
            result.elements.back().tupletNormalNotes = tuplet.normalNotes;
            glyph(LayoutElement::Type::TupletBracket, tupletId + ":normal-number",
                   "tuplet-normal-number",
                   {tupletNumberX + 9.0F, -16.0F},
                  {TimeDigit(tuplet.normalNotes), 8.0F, 10.0F}, voiceNumber);
            result.elements.back().tupletActualNotes = tuplet.actualNotes;
            result.elements.back().tupletNormalNotes = tuplet.normalNotes;
            for (std::size_t noteIndex = 0; noteIndex < tuplet.notes.size(); ++noteIndex) {
                const Note& note = tuplet.notes[noteIndex];
                float onset = timelineCursor;
                if (note.grace) {
                    const float steal = std::min(DurationUnits(note.duration) * 0.5F,
                                                 anchorDuration * 0.5F - stolenDuration);
                    onset = anchorStart + anchorDuration - stolenDuration - steal;
                    stolenDuration += steal;
                } else {
                    anchorStart = timelineCursor;
                    anchorDuration = DurationUnits(note.duration) * ratio;
                    stolenDuration = 0.0F;
                    timelineCursor += anchorDuration;
                }
                emitNote(note, tupletId + ":note:" + std::to_string(noteIndex), onset,
                         tuplet.actualNotes, tuplet.normalNotes);
            }
        }
        for (std::size_t noteIndex = 0; noteIndex + 1U < voice.notes.size(); ++noteIndex) {
            if (!voice.notes[noteIndex].tied) {
                continue;
            }
            const Point2D start = {notePositions[noteIndex].x + kNoteheadWidth,
                                   notePositions[noteIndex].y + 7.0F};
            const Point2D end = {notePositions[noteIndex + 1U].x,
                                 notePositions[noteIndex + 1U].y + 7.0F};
            curve(LayoutElement::Type::Tie,
                  prefix + ":voice:" + std::to_string(voiceIndex) + ":tie:" +
                      std::to_string(noteIndex),
                  "tie", start, {(start.x + end.x) * 0.5F, std::max(start.y, end.y) + 6.0F},
                  end, voiceNumber);
        }
        for (std::size_t slurIndex = 0; slurIndex < voice.slurs.size(); ++slurIndex) {
            const SlurSpan& slur = voice.slurs[slurIndex];
            const Point2D start = {notePositions[slur.startNote].x,
                                   notePositions[slur.startNote].y - 7.0F};
            const Point2D end = {notePositions[slur.endNote].x + kNoteheadWidth,
                                 notePositions[slur.endNote].y - 7.0F};
            curve(LayoutElement::Type::Slur,
                  prefix + ":voice:" + std::to_string(voiceIndex) + ":slur:" +
                      std::to_string(slurIndex),
                  "slur", start, {(start.x + end.x) * 0.5F, std::min(start.y, end.y) - 10.0F},
                  end, voiceNumber);
        }
    }
    if (measure.hasHairpin) {
        const float middleX = (musicStart + musicEnd) * 0.5F;
        const float hairpinY = 42.0F;
        const Point2D closed = measure.hairpinCrescendo ? Point2D {musicStart, hairpinY}
                                                         : Point2D {musicEnd, hairpinY};
        const Point2D upper = measure.hairpinCrescendo ? Point2D {musicEnd, hairpinY - 5.0F}
                                                        : Point2D {musicStart, hairpinY - 5.0F};
        const Point2D lower = measure.hairpinCrescendo ? Point2D {musicEnd, hairpinY + 5.0F}
                                                        : Point2D {musicStart, hairpinY + 5.0F};
        line(LayoutElement::Type::Hairpin, prefix + ":hairpin:upper", "hairpin-upper",
             closed, upper);
        line(LayoutElement::Type::Hairpin, prefix + ":hairpin:lower", "hairpin-lower",
             closed, lower);
        glyph(LayoutElement::Type::Hairpin, prefix + ":hairpin:anchor", "hairpin-anchor",
              {middleX, hairpinY}, {0, 1.0F, 1.0F}, -1);
    }
    line(LayoutElement::Type::Barline, prefix + ":barline", "barline",
         {staffEnd - 4.0F, staffTop}, {staffEnd - 4.0F, staffBottom});
    result.totalWidth = staffEnd;
    result.work.emittedElements = result.elements.size();
    if (hasContext && appendTo == nullptr) {
        result.diagnostic = "context supplied without an appended layout";
    }
    if (!IsPriorLayoutValid(result)) {
        result.elements.clear();
        result.totalWidth = 0.0F;
        result.valid = false;
        result.diagnostic = "layout geometry exceeds the declared staff bounds";
        result.work.emittedElements = 0U;
    }
    return result;
}

std::optional<std::string> SmuflFallbackEngraver::HitTest(
    const Point2D point,
    const LayoutResult& layout)
{
    if (!IsPriorLayoutValid(layout) || !IsFinite(point.x) || !IsFinite(point.y)) {
        return std::nullopt;
    }
    const LayoutElement* best = nullptr;
    for (const LayoutElement& element : layout.elements) {
        const Rect& box = element.boundingBox;
        if (point.x < box.x || point.x > box.x + box.w || point.y < box.y ||
            point.y > box.y + box.h) {
            continue;
        }
        if (best == nullptr || HitPriority(element.type) < HitPriority(best->type) ||
            (HitPriority(element.type) == HitPriority(best->type) && element.id < best->id)) {
            best = &element;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return best->id;
}

float SmuflFallbackEngraver::StaffStepToY(const int step, const int octave) const {
    const int diatonicOffset = (octave - 4) * 7 + (step - 2);
    return 24.0F - static_cast<float>(diatonicOffset) * (kStaffSpacing * 0.5F);
}

} // namespace engraving
