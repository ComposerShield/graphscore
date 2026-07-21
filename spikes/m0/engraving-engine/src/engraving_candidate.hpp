// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engraving {

struct Point2D {
    float x = 0.0F;
    float y = 0.0F;
};

struct Rect {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
};

enum class Clef : std::uint8_t { Treble, Bass, Alto, Tenor };

enum class NoteDuration : std::uint8_t {
    Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond
};

enum class Accidental : std::uint8_t {
    None, Sharp, Flat, Natural, DoubleSharp, DoubleFlat
};

enum class Articulation : std::uint8_t {
    None, Staccato, Accent, Tenuto, Staccatissimo, Marcato
};

struct Pitch {
    int step = 0;
    int octave = 4;
    Accidental accidental = Accidental::None;
};

struct Note {
    Pitch pitch;
    NoteDuration duration = NoteDuration::Quarter;
    bool tied = false;
    bool grace = false;
    std::vector<Articulation> articulations;
};

struct Tuplet {
    int actualNotes = 3;
    int normalNotes = 2;
    std::vector<Note> notes;
};

struct SlurSpan {
    std::size_t startNote = 0;
    std::size_t endNote = 0;
};

struct Voice {
    std::vector<Note> notes;
    std::vector<Tuplet> tuplets;
    std::vector<SlurSpan> slurs;
};

struct Measure {
    std::string stableId = "measure";
    int timeNumerator = 4;
    int timeDenominator = 4;
    Clef clef = Clef::Treble;
    int keyFifths = 0;
    float staffWidth = 240.0F;
    std::vector<Voice> voices;
    bool hasHairpin = false;
    bool hairpinCrescendo = true;
};

enum class GeometryKind : std::uint8_t { Glyph, Line, Curve };

struct LayoutElement {
    enum class Type : std::uint8_t {
        Notehead, Rest, Clef, KeySig, TimeSig, AccidentalGlyph,
        ArticulationGlyph, Stem, Beam, Flag, LedgerLine, Slur, Tie,
        TupletBracket, Hairpin, Barline, StaffLine
    };

    Type type = Type::Notehead;
    GeometryKind geometry = GeometryKind::Glyph;
    Point2D position;
    Point2D endPoint;
    Point2D controlPoint;
    Rect boundingBox;
    int voiceIndex = -1;
    std::uint32_t smuflCodepoint = 0;
    float normalizedTime = 0.0F;
    int tupletActualNotes = 0;
    int tupletNormalNotes = 0;
    std::string id;
    std::string role;
};

struct LayoutWork {
    std::size_t visitedVoices = 0;
    std::size_t visitedNotes = 0;
    std::size_t emittedElements = 0;
    bool reusedPrecedingLayout = false;
};

struct LayoutResult {
    std::vector<LayoutElement> elements;
    float totalWidth = 0.0F;
    bool incremental = false;
    bool valid = true;
    std::string diagnostic;
    LayoutWork work;
};

class IEngravingCandidate {
public:
    virtual ~IEngravingCandidate() = default;

    virtual std::string_view Name() const = 0;
    virtual std::string_view Version() const = 0;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual LayoutResult LayoutMeasure(
        const Measure& measure,
        const Measure* context,
        const LayoutResult* appendTo) = 0;
    virtual std::optional<std::string> HitTest(
        Point2D point,
        const LayoutResult& layout) = 0;

    virtual bool SupportsIncrementalLayout() const = 0;
    virtual bool IsPageOriented() const = 0;
    virtual bool SupportsTuplets() const = 0;
    virtual bool SupportsGraceNotes() const = 0;
    virtual bool SupportsNVoices(int n) const = 0;
    virtual bool SupportsTies() const = 0;
    virtual bool SupportsSlurs() const = 0;
    virtual bool SupportsHairpins() const = 0;
    virtual bool SupportsArticulations() const = 0;
    virtual bool SupportsClefChanges() const = 0;
    virtual bool SupportsKeyChanges() const = 0;
    virtual bool SupportsTimeChanges() const = 0;
    virtual bool SupportsArbitraryStaffWidth() const = 0;
};

} // namespace engraving
