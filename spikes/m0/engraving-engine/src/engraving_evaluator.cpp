// SPDX-License-Identifier: Apache-2.0

#include "engraving_evaluator.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace engraving {
namespace {

bool HasRole(const LayoutResult& layout, const std::string_view role) {
    for (const LayoutElement& element : layout.elements) {
        if (element.role == role) {
            return true;
        }
    }
    return false;
}

std::size_t CountType(const LayoutResult& layout, const LayoutElement::Type type) {
    std::size_t count = 0;
    for (const LayoutElement& element : layout.elements) {
        if (element.type == type) {
            ++count;
        }
    }
    return count;
}

bool HasFiniteGeometry(const LayoutResult& layout) {
    if (!layout.valid || !std::isfinite(layout.totalWidth) || layout.totalWidth <= 0.0F) {
        return false;
    }
    for (const LayoutElement& element : layout.elements) {
        const Rect& box = element.boundingBox;
        if (!std::isfinite(element.position.x) || !std::isfinite(element.position.y) ||
            !std::isfinite(element.endPoint.x) || !std::isfinite(element.endPoint.y) ||
            !std::isfinite(element.controlPoint.x) || !std::isfinite(element.controlPoint.y) ||
            !std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.w) ||
            !std::isfinite(box.h) || box.w <= 0.0F || box.h <= 0.0F || box.x < 0.0F ||
            static_cast<double>(box.x) + static_cast<double>(box.w) >
                static_cast<double>(layout.totalWidth) + 0.01 ||
            element.id.empty() ||
            element.role.empty()) {
            return false;
        }
    }
    return true;
}

const LayoutElement* FindByRole(const LayoutResult& layout, const std::string_view role) {
    for (const LayoutElement& element : layout.elements) {
        if (element.role == role) {
            return &element;
        }
    }
    return nullptr;
}

const LayoutElement* FindById(const LayoutResult& layout, const std::string_view id) {
    for (const LayoutElement& element : layout.elements) {
        if (element.id == id) {
            return &element;
        }
    }
    return nullptr;
}

bool HasExpectedTiming(
    const LayoutElement* const element,
    const float expectedNormalizedTime,
    const float expectedPositionX)
{
    constexpr float kTolerance = 0.0001F;
    return element != nullptr &&
           std::fabs(element->normalizedTime - expectedNormalizedTime) <= kTolerance &&
           std::fabs(element->position.x - expectedPositionX) <= kTolerance;
}

bool BoundsOverlap(const Rect& left, const Rect& right) {
    return left.x < right.x + right.w && right.x < left.x + left.w &&
           left.y < right.y + right.h && right.y < left.y + left.h;
}

bool HasTupletRatio(const LayoutResult& layout, const int actual, const int normal) {
    for (const LayoutElement& element : layout.elements) {
        if (element.role == "tuplet-bracket" && element.tupletActualNotes == actual &&
            element.tupletNormalNotes == normal) {
            return true;
        }
    }
    return false;
}

bool HasTupletNoteRatio(const LayoutResult& layout, const int actual, const int normal) {
    for (const LayoutElement& element : layout.elements) {
        if (element.role == "notehead" && element.tupletActualNotes == actual &&
            element.tupletNormalNotes == normal) {
            return true;
        }
    }
    return false;
}

Measure BaseMeasure(const std::string_view stableId) {
    Measure measure;
    measure.stableId = stableId;
    measure.staffWidth = 256.0F;
    Voice voice;
    voice.notes = {{Pitch {0, 4}, NoteDuration::Quarter},
                   {Pitch {2, 4}, NoteDuration::Quarter},
                   {Pitch {4, 4}, NoteDuration::Half}};
    measure.voices.push_back(std::move(voice));
    return measure;
}

bool HasStableElements(const LayoutResult& before, const LayoutResult& after) {
    if (before.elements.size() != after.elements.size()) {
        return false;
    }
    for (std::size_t index = 0; index < before.elements.size(); ++index) {
        const LayoutElement& left = before.elements[index];
        const LayoutElement& right = after.elements[index];
        if (left.id != right.id || left.role != right.role ||
            left.position.x != right.position.x || left.position.y != right.position.y ||
            left.boundingBox.x != right.boundingBox.x ||
            left.boundingBox.y != right.boundingBox.y ||
            left.boundingBox.w != right.boundingBox.w ||
            left.boundingBox.h != right.boundingBox.h) {
            return false;
        }
    }
    return true;
}

void WriteJsonString(FILE* const file, const std::string_view value) {
    std::fputc('"', file);
    for (const char character : value) {
        switch (character) {
        case '"':
            std::fputs("\\\"", file);
            break;
        case '\\':
            std::fputs("\\\\", file);
            break;
        case '\n':
            std::fputs("\\n", file);
            break;
        case '\r':
            std::fputs("\\r", file);
            break;
        case '\t':
            std::fputs("\\t", file);
            break;
        default:
            std::fputc(static_cast<unsigned char>(character), file);
            break;
        }
    }
    std::fputc('"', file);
}

} // namespace

void EngravingEvaluator::RegisterCandidate(std::unique_ptr<IEngravingCandidate> candidate) {
    if (candidate != nullptr) {
        m_candidates.push_back(std::move(candidate));
    }
}

bool EngravingEvaluator::EvaluateAll() {
    m_results.clear();
    bool allPassed = !m_candidates.empty();
    for (const std::unique_ptr<IEngravingCandidate>& candidate : m_candidates) {
        m_candidateName = std::string(candidate->Name());
        std::fprintf(stdout, "\n=== Evaluating: %s %s ===\n", m_candidateName.c_str(),
                     std::string(candidate->Version()).c_str());
        Test_Initialization(*candidate);
        if (m_results.empty() || !m_results.back().passed) {
            allPassed = false;
            continue;
        }
        Test_NotPageOriented(*candidate);
        Test_BasicSingleVoice(*candidate);
        Test_FourVoices(*candidate);
        Test_IncrementalLayout(*candidate);
        Test_Tuplets(*candidate);
        Test_TupletActualCounts(*candidate);
        Test_GraceNotes(*candidate);
        Test_ClefKeyTimeChanges(*candidate);
        Test_Ties(*candidate);
        Test_Slurs(*candidate);
        Test_Hairpins(*candidate);
        Test_ArticulationPlacement(*candidate);
        Test_HitTesting(*candidate);
        Test_ArbitraryStaffWidth(*candidate);
        candidate->Shutdown();
    }
    for (const EvaluationResult& result : m_results) {
        std::fprintf(stdout, "  [%s] %s", result.passed ? "PASS" : "FAIL",
                     result.testName.c_str());
        if (!result.detail.empty()) {
            std::fprintf(stdout, " — %s", result.detail.c_str());
        }
        std::fprintf(stdout, "\n");
        allPassed = allPassed && result.passed;
    }
    std::fprintf(stdout, "\nEvaluation: %s (%zu criteria)\n",
                 allPassed ? "ALL PASSED" : "SOME FAILED", m_results.size());
    return allPassed;
}

const std::vector<EvaluationResult>& EngravingEvaluator::Results() const {
    return m_results;
}

void EngravingEvaluator::AddResult(
    const std::string_view name,
    const bool passed,
    std::string detail)
{
    m_results.push_back({m_candidateName + ": " + std::string(name), passed,
                         std::move(detail)});
}

bool EngravingEvaluator::SaveResults(const char* const path) const {
    if (path == nullptr) {
        return false;
    }
    FILE* const file = std::fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    std::fputs("{\n  \"results\": [\n", file);
    for (std::size_t index = 0; index < m_results.size(); ++index) {
        const EvaluationResult& result = m_results[index];
        std::fputs("    {\"name\":", file);
        WriteJsonString(file, result.testName);
        std::fputs(result.passed ? ",\"passed\":true,\"detail\":"
                                 : ",\"passed\":false,\"detail\":",
                   file);
        WriteJsonString(file, result.detail);
        std::fputs(index + 1U < m_results.size() ? "},\n" : "}\n", file);
    }
    std::fputs("  ]\n}\n", file);
    return std::ferror(file) == 0 && std::fclose(file) == 0;
}

void EngravingEvaluator::Test_Initialization(IEngravingCandidate& candidate) {
    AddResult("initialization", candidate.Initialize(), "initialization call completed");
}

void EngravingEvaluator::Test_BasicSingleVoice(IEngravingCandidate& candidate) {
    if (!candidate.SupportsNVoices(1)) {
        AddResult("basic single voice", false, "capability query rejects one voice");
        return;
    }
    const LayoutResult layout = candidate.LayoutMeasure(BaseMeasure("basic"), nullptr, nullptr);
    const bool passed = HasFiniteGeometry(layout) && HasRole(layout, "clef") &&
                        HasRole(layout, "time-signature") && HasRole(layout, "staff-line") &&
                        CountType(layout, LayoutElement::Type::Notehead) == 3U;
    AddResult("basic single voice", passed,
              std::to_string(layout.elements.size()) + " semantic elements");
}

void EngravingEvaluator::Test_FourVoices(IEngravingCandidate& candidate) {
    if (!candidate.SupportsNVoices(4)) {
        AddResult("four voices", false, "capability query rejects four voices");
        return;
    }
    Measure measure;
    measure.stableId = "four-voices";
    measure.staffWidth = 256.0F;
    for (int voiceIndex = 0; voiceIndex < 4; ++voiceIndex) {
        Voice voice;
        voice.notes = {{Pitch {0, 4}, NoteDuration::Quarter}};
        measure.voices.push_back(std::move(voice));
    }
    const LayoutResult layout = candidate.LayoutMeasure(measure, nullptr, nullptr);
    std::vector<float> positions;
    for (const LayoutElement& element : layout.elements) {
        if (element.type == LayoutElement::Type::Notehead && element.role == "notehead") {
            positions.push_back(element.position.x);
        }
    }
    bool separated = positions.size() == 4U;
    for (std::size_t index = 1; index < positions.size(); ++index) {
        separated = separated && positions[index] - positions[index - 1U] >= 10.0F;
    }
    AddResult("four voices", HasFiniteGeometry(layout) && separated &&
                                  CountType(layout, LayoutElement::Type::Stem) == 4U,
              "four same-onset noteheads use separate collision columns");
}

void EngravingEvaluator::Test_IncrementalLayout(IEngravingCandidate& candidate) {
    if (!candidate.SupportsIncrementalLayout()) {
        AddResult("incremental layout", false, "capability query rejects incremental layout");
        return;
    }
    const Measure first = BaseMeasure("incremental-first");
    Measure second = BaseMeasure("incremental-second");
    second.voices[0].notes.push_back({Pitch {6, 5}, NoteDuration::Eighth});
    const LayoutResult firstLayout = candidate.LayoutMeasure(first, nullptr, nullptr);
    const LayoutResult firstSnapshot = firstLayout;
    const LayoutResult secondLayout = candidate.LayoutMeasure(second, &first, &firstLayout);
    const bool bounded = secondLayout.work.reusedPrecedingLayout &&
                         secondLayout.work.visitedVoices == 1U &&
                         secondLayout.work.visitedNotes == second.voices[0].notes.size();
    AddResult("incremental layout",
              HasFiniteGeometry(firstLayout) && HasFiniteGeometry(secondLayout) &&
                  secondLayout.incremental && secondLayout.totalWidth > firstLayout.totalWidth &&
                  HasStableElements(firstSnapshot, firstLayout) && bounded,
              "preceding identities/geometry unchanged; work only visits changed measure");
}

void EngravingEvaluator::Test_Tuplets(IEngravingCandidate& candidate) {
    if (!candidate.SupportsTuplets()) {
        AddResult("tuplets", false, "capability query rejects tuplets");
        return;
    }
    Measure measure = BaseMeasure("tuplet");
    measure.voices[0].notes.clear();
    Tuplet triplet;
    triplet.notes = {{Pitch {0, 4}, NoteDuration::Eighth},
                     {Pitch {1, 4}, NoteDuration::Eighth},
                     {Pitch {2, 4}, NoteDuration::Eighth}};
    measure.voices[0].tuplets.push_back(triplet);
    measure.voices[0].notes = {{Pitch {6, 4}, NoteDuration::Quarter},
                                {Pitch {0, 5}, NoteDuration::Quarter}};
    const LayoutResult layout = candidate.LayoutMeasure(measure, nullptr, nullptr);
    Tuplet uncompressed = triplet;
    uncompressed.normalNotes = 3;
    measure.voices[0].tuplets[0] = std::move(uncompressed);
    const LayoutResult uncompressedLayout = candidate.LayoutMeasure(measure, nullptr, nullptr);
    const LayoutElement* compressedFirst = FindByRole(layout, "notehead");
    const LayoutElement* uncompressedFirst = FindByRole(uncompressedLayout, "notehead");
    bool spacingChanged = false;
    if (compressedFirst != nullptr && uncompressedFirst != nullptr) {
        for (const LayoutElement& element : layout.elements) {
            if (element.tupletActualNotes == 3 && element.tupletNormalNotes == 2) {
                for (const LayoutElement& comparison : uncompressedLayout.elements) {
                    if (comparison.id == element.id &&
                        comparison.normalizedTime != element.normalizedTime) {
                        spacingChanged = true;
                    }
                }
            }
        }
    }
    AddResult("tuplets", HasFiniteGeometry(layout) && HasFiniteGeometry(uncompressedLayout) &&
                               HasTupletRatio(layout, 3, 2) &&
                               HasRole(layout, "tuplet-actual-number") &&
                               HasRole(layout, "tuplet-normal-number") && spacingChanged &&
                               CountType(layout, LayoutElement::Type::Notehead) == 5U,
                "3:2 ratio is retained and changes normalized tuplet timing versus 3:3");
}

void EngravingEvaluator::Test_TupletActualCounts(IEngravingCandidate& candidate) {
    if (!candidate.SupportsTuplets()) {
        AddResult("tuplet actual counts", false, "capability query rejects tuplets");
        return;
    }
    Measure tripletMeasure = BaseMeasure("tuplet-actual-three");
    tripletMeasure.voices[0].notes = {{Pitch {6, 4}, NoteDuration::Quarter},
                                      {Pitch {0, 5}, NoteDuration::Quarter}};
    Tuplet triplet;
    triplet.actualNotes = 3;
    triplet.normalNotes = 2;
    triplet.notes = {{Pitch {0, 4}, NoteDuration::Eighth},
                     {Pitch {1, 4}, NoteDuration::Eighth},
                     {Pitch {2, 4}, NoteDuration::Eighth}};
    tripletMeasure.voices[0].tuplets.push_back(triplet);
    const LayoutResult tripletLayout = candidate.LayoutMeasure(tripletMeasure, nullptr, nullptr);

    Measure quadrupletMeasure = tripletMeasure;
    quadrupletMeasure.stableId = "tuplet-actual-four";
    Tuplet& quadruplet = quadrupletMeasure.voices[0].tuplets[0];
    quadruplet.actualNotes = 4;
    quadruplet.notes.push_back({Pitch {3, 4}, NoteDuration::Eighth});
    const LayoutResult quadrupletLayout = candidate.LayoutMeasure(quadrupletMeasure, nullptr, nullptr);

    const LayoutElement* const tripletNote = FindById(
        tripletLayout, "tuplet-actual-three:voice:0:tuplet:0:note:1:notehead");
    const LayoutElement* const quadrupletNote = FindById(
        quadrupletLayout, "tuplet-actual-four:voice:0:tuplet:0:note:1:notehead");
    constexpr float kMusicStart = 38.0F;
    constexpr float kRightMusicInset = 8.0F;
    constexpr float kVoiceRightExtent = 16.0F;
    const float timelineWidth = tripletMeasure.staffWidth - kMusicStart -
                                kRightMusicInset - kVoiceRightExtent;
    const bool expectedTiming =
        HasExpectedTiming(tripletNote, 7.0F / 9.0F,
                          kMusicStart + timelineWidth * (7.0F / 9.0F)) &&
        HasExpectedTiming(quadrupletNote, 3.0F / 4.0F,
                          kMusicStart + timelineWidth * (3.0F / 4.0F));
    AddResult("tuplet actual counts",
              HasFiniteGeometry(tripletLayout) && HasFiniteGeometry(quadrupletLayout) &&
                  HasTupletRatio(tripletLayout, 3, 2) && HasTupletNoteRatio(tripletLayout, 3, 2) &&
                  HasTupletRatio(quadrupletLayout, 4, 2) &&
                  HasTupletNoteRatio(quadrupletLayout, 4, 2) && expectedTiming,
              "3:2 and 4:2 retain actual/normal fields and exact normalized tuplet timing");
}

void EngravingEvaluator::Test_GraceNotes(IEngravingCandidate& candidate) {
    if (!candidate.SupportsGraceNotes()) {
        AddResult("grace notes", false, "capability query rejects grace notes");
        return;
    }
    Measure measure = BaseMeasure("grace");
    measure.voices[0].notes = {{Pitch {0, 4}, NoteDuration::Quarter},
                                {Pitch {6, 4}, NoteDuration::Eighth, false, true},
                                {Pitch {0, 5}, NoteDuration::Quarter}};
    const LayoutResult layout = candidate.LayoutMeasure(measure, nullptr, nullptr);
    const LayoutElement* grace = FindByRole(layout, "grace-notehead");
    const LayoutElement* anchor = FindByRole(layout, "notehead");
    bool anchored = grace != nullptr && anchor != nullptr && grace->normalizedTime > anchor->normalizedTime;
    if (grace != nullptr) {
        for (const LayoutElement& element : layout.elements) {
            if (element.id == "grace:voice:0:note:2:notehead") {
                anchored = anchored && grace->normalizedTime < element.normalizedTime;
            }
        }
    }
    Measure invalid = measure;
    invalid.voices[0].notes.erase(invalid.voices[0].notes.begin());
    const LayoutResult invalidLayout = candidate.LayoutMeasure(invalid, nullptr, nullptr);
    AddResult("grace notes", HasFiniteGeometry(layout) && grace != nullptr && anchored &&
                                   !invalidLayout.valid &&
                                   CountType(layout, LayoutElement::Type::Notehead) == 3U,
               "grace steals time after its anchor; an unanchored first event is rejected");
}

void EngravingEvaluator::Test_ClefKeyTimeChanges(IEngravingCandidate& candidate) {
    if (!candidate.SupportsClefChanges() || !candidate.SupportsKeyChanges() ||
        !candidate.SupportsTimeChanges()) {
        AddResult("clef/key/time changes", false, "one or more capability queries reject changes");
        return;
    }
    const Measure first = BaseMeasure("state-first");
    Measure second = BaseMeasure("state-second");
    second.clef = Clef::Bass;
    second.keyFifths = -3;
    second.timeNumerator = 3;
    second.timeDenominator = 8;
    const LayoutResult firstLayout = candidate.LayoutMeasure(first, nullptr, nullptr);
    const LayoutResult secondLayout = candidate.LayoutMeasure(second, &first, &firstLayout);
    AddResult("clef/key/time changes", HasFiniteGeometry(secondLayout) &&
                                          HasRole(secondLayout, "clef") &&
                                          CountType(secondLayout, LayoutElement::Type::KeySig) == 3U &&
                                          CountType(secondLayout, LayoutElement::Type::TimeSig) == 2U,
              "bass clef, three flats, and 3/8 glyphs emitted in appended measure");
}

void EngravingEvaluator::Test_Ties(IEngravingCandidate& candidate) {
    if (!candidate.SupportsTies()) {
        AddResult("ties", false, "capability query rejects ties");
        return;
    }
    Measure measure = BaseMeasure("tie");
    measure.voices[0].notes[0].tied = true;
    const LayoutResult layout = candidate.LayoutMeasure(measure, nullptr, nullptr);
    AddResult("ties", HasFiniteGeometry(layout) && HasRole(layout, "tie") &&
                           CountType(layout, LayoutElement::Type::Tie) == 1U,
              "tie is retained as its own curve span");
}

void EngravingEvaluator::Test_Slurs(IEngravingCandidate& candidate) {
    if (!candidate.SupportsSlurs()) {
        AddResult("slurs", false, "capability query rejects slurs");
        return;
    }
    Measure measure = BaseMeasure("slur");
    measure.voices[0].slurs.push_back({0U, 2U});
    const LayoutResult layout = candidate.LayoutMeasure(measure, nullptr, nullptr);
    AddResult("slurs", HasFiniteGeometry(layout) && HasRole(layout, "slur") &&
                            CountType(layout, LayoutElement::Type::Slur) == 1U,
              "slur is retained separately from tie geometry");
}

void EngravingEvaluator::Test_Hairpins(IEngravingCandidate& candidate) {
    if (!candidate.SupportsHairpins()) {
        AddResult("hairpins", false, "capability query rejects hairpins");
        return;
    }
    Measure crescendo = BaseMeasure("crescendo");
    crescendo.hasHairpin = true;
    Measure diminuendo = crescendo;
    diminuendo.stableId = "diminuendo";
    diminuendo.hairpinCrescendo = false;
    const LayoutResult first = candidate.LayoutMeasure(crescendo, nullptr, nullptr);
    const LayoutResult second = candidate.LayoutMeasure(diminuendo, nullptr, nullptr);
    const LayoutElement* crescendoUpper = FindByRole(first, "hairpin-upper");
    const LayoutElement* diminuendoUpper = FindByRole(second, "hairpin-upper");
    const bool directionsDiffer = crescendoUpper != nullptr && diminuendoUpper != nullptr &&
                                  crescendoUpper->position.x < crescendoUpper->endPoint.x &&
                                  diminuendoUpper->position.x > diminuendoUpper->endPoint.x;
    AddResult("hairpins", HasFiniteGeometry(first) && HasFiniteGeometry(second) &&
                                CountType(first, LayoutElement::Type::Hairpin) >= 2U &&
                                CountType(second, LayoutElement::Type::Hairpin) >= 2U &&
                                directionsDiffer,
               "crescendo opens right and diminuendo opens left in retained line geometry");
}

void EngravingEvaluator::Test_ArticulationPlacement(IEngravingCandidate& candidate) {
    if (!candidate.SupportsArticulations()) {
        AddResult("articulation placement", false, "capability query rejects articulations");
        return;
    }
    Measure measure = BaseMeasure("articulation");
    measure.voices[0].notes[0].articulations = {Articulation::Staccato,
                                                  Articulation::Accent,
                                                  Articulation::Tenuto};
    const LayoutResult layout = candidate.LayoutMeasure(measure, nullptr, nullptr);
    std::vector<const LayoutElement*> articulations;
    bool aboveNotehead = true;
    for (const LayoutElement& element : layout.elements) {
        if (element.role == "articulation") {
            aboveNotehead = aboveNotehead && element.position.y < 24.0F;
            articulations.push_back(&element);
        }
    }
    bool collisionFree = articulations.size() == 3U;
    for (std::size_t left = 0; left < articulations.size(); ++left) {
        for (std::size_t right = left + 1U; right < articulations.size(); ++right) {
            collisionFree = collisionFree &&
                            !BoundsOverlap(articulations[left]->boundingBox,
                                           articulations[right]->boundingBox);
        }
    }
    AddResult("articulation placement", HasFiniteGeometry(layout) && aboveNotehead &&
                                           collisionFree &&
                                           CountType(layout, LayoutElement::Type::ArticulationGlyph) == 3U,
               "three articulation glyphs stack above the note with disjoint bounds");
}

void EngravingEvaluator::Test_HitTesting(IEngravingCandidate& candidate) {
    const LayoutResult layout = candidate.LayoutMeasure(BaseMeasure("hit"), nullptr, nullptr);
    const LayoutElement* notehead = nullptr;
    for (const LayoutElement& element : layout.elements) {
        if (element.type == LayoutElement::Type::Notehead && element.role == "notehead") {
            notehead = &element;
            break;
        }
    }
    if (notehead == nullptr) {
        AddResult("hit testing", false, "fixture did not emit a notehead");
        return;
    }
    const Point2D center = {notehead->boundingBox.x + notehead->boundingBox.w * 0.5F,
                            notehead->boundingBox.y + notehead->boundingBox.h * 0.5F};
    const std::optional<std::string> first = candidate.HitTest(center, layout);
    const std::optional<std::string> second = candidate.HitTest(center, layout);
    AddResult("hit testing", first.has_value() && second.has_value() &&
                                 *first == notehead->id && *first == *second &&
                                 !candidate.HitTest({-1.0F, -1.0F}, layout).has_value(),
              "notehead identity and repeated hit ordering are deterministic");
}

void EngravingEvaluator::Test_ArbitraryStaffWidth(IEngravingCandidate& candidate) {
    if (!candidate.SupportsArbitraryStaffWidth()) {
        AddResult("arbitrary staff width", false, "capability query rejects arbitrary width");
        return;
    }
    Measure narrow = BaseMeasure("narrow");
    narrow.staffWidth = 128.0F;
    Measure wide = narrow;
    wide.stableId = "wide";
    wide.staffWidth = 384.0F;
    const LayoutResult narrowLayout = candidate.LayoutMeasure(narrow, nullptr, nullptr);
    const LayoutResult wideLayout = candidate.LayoutMeasure(wide, nullptr, nullptr);
    AddResult("arbitrary staff width", HasFiniteGeometry(narrowLayout) &&
                                          HasFiniteGeometry(wideLayout) &&
                                          narrowLayout.totalWidth == narrow.staffWidth &&
                                          wideLayout.totalWidth == wide.staffWidth &&
                                          wideLayout.totalWidth > narrowLayout.totalWidth,
              "128 and 384 unit staff fixtures remain bounded and adapt spacing");
}

void EngravingEvaluator::Test_NotPageOriented(IEngravingCandidate& candidate) {
    AddResult("interactive model", !candidate.IsPageOriented() &&
                                       candidate.SupportsIncrementalLayout(),
              "requires direct measure layout without page serialization");
}

} // namespace engraving
