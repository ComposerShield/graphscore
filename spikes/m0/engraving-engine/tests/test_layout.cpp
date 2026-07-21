// SPDX-License-Identifier: Apache-2.0

#include "engraving_evaluator.hpp"
#include "smuf_fallback.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace engraving {
namespace {

Measure FixtureMeasure(const std::string& identifier = "fixture") {
    Measure measure;
    measure.stableId = identifier;
    measure.staffWidth = 256.0F;
    Voice voice;
    voice.notes = {{Pitch {0, 4}, NoteDuration::Quarter},
                   {Pitch {2, 4}, NoteDuration::Eighth},
                   {Pitch {4, 4}, NoteDuration::Eighth},
                   {Pitch {6, 5}, NoteDuration::Half}};
    measure.voices.push_back(std::move(voice));
    return measure;
}

const LayoutElement* FindRole(const LayoutResult& layout, const std::string& role) {
    for (const LayoutElement& element : layout.elements) {
        if (element.role == role) {
            return &element;
        }
    }
    return nullptr;
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

bool GeometryIsFiniteAndBounded(const LayoutResult& layout) {
    if (!layout.valid || !std::isfinite(layout.totalWidth) || layout.totalWidth <= 0.0F) {
        return false;
    }
    for (const LayoutElement& element : layout.elements) {
        const Rect& box = element.boundingBox;
        if (!std::isfinite(element.position.x) || !std::isfinite(element.position.y) ||
            !std::isfinite(element.endPoint.x) || !std::isfinite(element.endPoint.y) ||
            !std::isfinite(element.controlPoint.x) || !std::isfinite(element.controlPoint.y) ||
            element.position.x < 0.0F || element.position.x > layout.totalWidth ||
            element.endPoint.x < 0.0F || element.endPoint.x > layout.totalWidth ||
            element.controlPoint.x < 0.0F || element.controlPoint.x > layout.totalWidth ||
            !std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.w) ||
            !std::isfinite(box.h) || box.w <= 0.0F || box.h <= 0.0F || box.x < 0.0F ||
            static_cast<double>(box.x) + static_cast<double>(box.w) >
                static_cast<double>(layout.totalWidth) + 0.01) {
            return false;
        }
    }
    return true;
}

bool BoundsOverlap(const Rect& left, const Rect& right) {
    return left.x < right.x + right.w && right.x < left.x + left.w &&
           left.y < right.y + right.h && right.y < left.y + left.h;
}

enum class Mutation : std::uint8_t {
    Hairpin, Tuplet, TupletActual, TupletActualTiming, Articulation, Grace, IncrementalWork
};

class MutatingCandidate final : public IEngravingCandidate {
public:
    explicit MutatingCandidate(const Mutation mutation)
        : m_mutation(mutation) {}

    std::string_view Name() const override { return "Mutating evaluator double"; }
    std::string_view Version() const override { return "0.1"; }
    bool Initialize() override { return m_fallback.Initialize(); }
    void Shutdown() override { m_fallback.Shutdown(); }
    LayoutResult LayoutMeasure(
        const Measure& measure,
        const Measure* const context,
        const LayoutResult* const appendTo) override
    {
        Measure mutatedTupletMeasure = measure;
        bool mutatedActualTiming = false;
        if (m_mutation == Mutation::TupletActual || m_mutation == Mutation::TupletActualTiming) {
            for (Voice& voice : mutatedTupletMeasure.voices) {
                for (Tuplet& tuplet : voice.tuplets) {
                    if (m_mutation == Mutation::TupletActual || tuplet.actualNotes == 4) {
                        mutatedActualTiming = mutatedActualTiming || tuplet.actualNotes == 4;
                        tuplet.actualNotes = 3;
                    }
                }
            }
        }
        const Measure& layoutMeasure =
            m_mutation == Mutation::TupletActual || m_mutation == Mutation::TupletActualTiming
                ? mutatedTupletMeasure
                : measure;
        LayoutResult layout = m_fallback.LayoutMeasure(layoutMeasure, context, appendTo);
        for (LayoutElement& element : layout.elements) {
            if (m_mutation == Mutation::Hairpin && element.role == "hairpin-upper") {
                std::swap(element.position.x, element.endPoint.x);
            }
            if (m_mutation == Mutation::Tuplet && element.tupletActualNotes != 0) {
                element.tupletNormalNotes = element.tupletActualNotes;
            }
            if (m_mutation == Mutation::TupletActualTiming && mutatedActualTiming &&
                element.tupletActualNotes != 0) {
                element.tupletActualNotes = 4;
            }
            if (m_mutation == Mutation::Articulation && element.role == "articulation") {
                element.position.y = 0.0F;
                element.boundingBox.y = 0.0F;
            }
            if (m_mutation == Mutation::Grace && element.role == "grace-notehead") {
                element.normalizedTime = 0.0F;
            }
        }
        if (m_mutation == Mutation::IncrementalWork && appendTo != nullptr) {
            ++layout.work.visitedNotes;
        }
        return layout;
    }
    std::optional<std::string> HitTest(const Point2D point, const LayoutResult& layout) override {
        return m_fallback.HitTest(point, layout);
    }
    bool SupportsIncrementalLayout() const override { return true; }
    bool IsPageOriented() const override { return false; }
    bool SupportsTuplets() const override { return true; }
    bool SupportsGraceNotes() const override { return true; }
    bool SupportsNVoices(const int n) const override { return n >= 1 && n <= 4; }
    bool SupportsTies() const override { return true; }
    bool SupportsSlurs() const override { return true; }
    bool SupportsHairpins() const override { return true; }
    bool SupportsArticulations() const override { return true; }
    bool SupportsClefChanges() const override { return true; }
    bool SupportsKeyChanges() const override { return true; }
    bool SupportsTimeChanges() const override { return true; }
    bool SupportsArbitraryStaffWidth() const override { return true; }

private:
    Mutation m_mutation;
    SmuflFallbackEngraver m_fallback;
};

void ExpectEvaluatorRejectsMutation(
    const Mutation mutation,
    const std::string_view criterion)
{
    EngravingEvaluator evaluator;
    evaluator.RegisterCandidate(std::make_unique<MutatingCandidate>(mutation));
    EXPECT_FALSE(evaluator.EvaluateAll());
    bool rejected = false;
    for (const EvaluationResult& result : evaluator.Results()) {
        if (result.testName.ends_with(criterion) && !result.passed) {
            rejected = true;
        }
    }
    EXPECT_TRUE(rejected) << criterion;
}

class SmuflFallbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engraver.Initialize());
    }

    void TearDown() override {
        engraver.Shutdown();
    }

    SmuflFallbackEngraver engraver;
};

TEST_F(SmuflFallbackTest, EmitsToolkitNeutralSemanticGlyphsAndGeometry) {
    const LayoutResult layout = engraver.LayoutMeasure(FixtureMeasure(), nullptr, nullptr);
    ASSERT_TRUE(layout.valid);
    EXPECT_TRUE(layout.incremental);
    EXPECT_TRUE(GeometryIsFiniteAndBounded(layout));
    EXPECT_NE(FindRole(layout, "staff-line"), nullptr);
    EXPECT_NE(FindRole(layout, "clef"), nullptr);
    EXPECT_NE(FindRole(layout, "time-signature"), nullptr);
    EXPECT_NE(FindRole(layout, "stem"), nullptr);
    EXPECT_NE(FindRole(layout, "beam"), nullptr);
    EXPECT_NE(FindRole(layout, "flag"), nullptr);
    EXPECT_NE(FindRole(layout, "ledger-line"), nullptr);
    EXPECT_GT(FindRole(layout, "clef")->smuflCodepoint, 0U);
    EXPECT_EQ(CountType(layout, LayoutElement::Type::StaffLine), 5U);
}

TEST_F(SmuflFallbackTest, SeparatesFourSimultaneousVoices) {
    Measure measure = FixtureMeasure("four-voices");
    measure.voices.clear();
    for (int index = 0; index < 4; ++index) {
        Voice voice;
        voice.notes = {{Pitch {0, 4}, NoteDuration::Quarter}};
        measure.voices.push_back(std::move(voice));
    }
    const LayoutResult layout = engraver.LayoutMeasure(measure, nullptr, nullptr);
    ASSERT_TRUE(layout.valid);
    std::vector<float> notePositions;
    for (const LayoutElement& element : layout.elements) {
        if (element.role == "notehead") {
            notePositions.push_back(element.position.x);
        }
    }
    ASSERT_EQ(notePositions.size(), 4U);
    EXPECT_GE(notePositions[1] - notePositions[0], 10.0F);
    EXPECT_GE(notePositions[2] - notePositions[1], 10.0F);
    EXPECT_GE(notePositions[3] - notePositions[2], 10.0F);
    EXPECT_EQ(CountType(layout, LayoutElement::Type::Stem), 4U);
}

TEST_F(SmuflFallbackTest, RepresentsTupletsAndGraceNotes) {
    Measure measure = FixtureMeasure("ornaments");
    measure.voices[0].notes = {{Pitch {0, 4}, NoteDuration::Quarter},
                                {Pitch {6, 4}, NoteDuration::Eighth, false, true},
                                {Pitch {0, 5}, NoteDuration::Quarter}};
    Tuplet triplet;
    triplet.notes = {{Pitch {0, 4}, NoteDuration::Eighth},
                     {Pitch {1, 4}, NoteDuration::Eighth},
                     {Pitch {2, 4}, NoteDuration::Eighth}};
    measure.voices[0].tuplets.push_back(std::move(triplet));
    const LayoutResult layout = engraver.LayoutMeasure(measure, nullptr, nullptr);
    ASSERT_TRUE(layout.valid);
    EXPECT_NE(FindRole(layout, "grace-notehead"), nullptr);
    EXPECT_NE(FindRole(layout, "tuplet-bracket"), nullptr);
    EXPECT_NE(FindRole(layout, "tuplet-actual-number"), nullptr);
    EXPECT_NE(FindRole(layout, "tuplet-normal-number"), nullptr);
    EXPECT_EQ(CountType(layout, LayoutElement::Type::Notehead), 6U);
}

TEST_F(SmuflFallbackTest, RetainsTupletRatioAndUsesItForTiming) {
    Measure measure = FixtureMeasure("tuplet-timing");
    measure.voices[0].notes = {{Pitch {0, 4}, NoteDuration::Quarter},
                                {Pitch {0, 5}, NoteDuration::Quarter}};
    Tuplet triplet;
    triplet.notes = {{Pitch {0, 4}, NoteDuration::Eighth},
                     {Pitch {1, 4}, NoteDuration::Eighth},
                     {Pitch {2, 4}, NoteDuration::Eighth}};
    measure.voices[0].tuplets.push_back(triplet);
    const LayoutResult compressed = engraver.LayoutMeasure(measure, nullptr, nullptr);
    measure.voices[0].tuplets[0].normalNotes = 3;
    const LayoutResult uncompressed = engraver.LayoutMeasure(measure, nullptr, nullptr);
    const LayoutElement* compressedTuplet = nullptr;
    const LayoutElement* uncompressedTuplet = nullptr;
    for (const LayoutElement& element : compressed.elements) {
        if (element.id == "tuplet-timing:voice:0:tuplet:0:note:0:notehead") {
            compressedTuplet = &element;
        }
    }
    for (const LayoutElement& element : uncompressed.elements) {
        if (element.id == "tuplet-timing:voice:0:tuplet:0:note:0:notehead") {
            uncompressedTuplet = &element;
        }
    }
    ASSERT_NE(compressedTuplet, nullptr);
    ASSERT_NE(uncompressedTuplet, nullptr);
    EXPECT_EQ(compressedTuplet->tupletActualNotes, 3);
    EXPECT_EQ(compressedTuplet->tupletNormalNotes, 2);
    EXPECT_NE(compressedTuplet->normalizedTime, uncompressedTuplet->normalizedTime);
    EXPECT_NE(compressedTuplet->position.x, uncompressedTuplet->position.x);
}

TEST_F(SmuflFallbackTest, RetainsTupletActualCountAndUsesItForTiming) {
    Measure tripletMeasure = FixtureMeasure("tuplet-actual-three");
    tripletMeasure.voices[0].notes = {{Pitch {6, 4}, NoteDuration::Quarter},
                                      {Pitch {0, 5}, NoteDuration::Quarter}};
    Tuplet triplet;
    triplet.actualNotes = 3;
    triplet.normalNotes = 2;
    triplet.notes = {{Pitch {0, 4}, NoteDuration::Eighth},
                     {Pitch {1, 4}, NoteDuration::Eighth},
                     {Pitch {2, 4}, NoteDuration::Eighth}};
    tripletMeasure.voices[0].tuplets.push_back(triplet);
    const LayoutResult tripletLayout = engraver.LayoutMeasure(tripletMeasure, nullptr, nullptr);

    Measure quadrupletMeasure = tripletMeasure;
    quadrupletMeasure.stableId = "tuplet-actual-four";
    Tuplet& quadruplet = quadrupletMeasure.voices[0].tuplets[0];
    quadruplet.actualNotes = 4;
    quadruplet.notes.push_back({Pitch {3, 4}, NoteDuration::Eighth});
    const LayoutResult quadrupletLayout = engraver.LayoutMeasure(quadrupletMeasure, nullptr, nullptr);

    const LayoutElement* tripletNote = nullptr;
    const LayoutElement* quadrupletNote = nullptr;
    for (const LayoutElement& element : tripletLayout.elements) {
        if (element.id == "tuplet-actual-three:voice:0:tuplet:0:note:1:notehead") {
            tripletNote = &element;
        }
    }
    for (const LayoutElement& element : quadrupletLayout.elements) {
        if (element.id == "tuplet-actual-four:voice:0:tuplet:0:note:1:notehead") {
            quadrupletNote = &element;
        }
    }
    ASSERT_NE(tripletNote, nullptr);
    ASSERT_NE(quadrupletNote, nullptr);
    EXPECT_EQ(tripletNote->tupletActualNotes, 3);
    EXPECT_EQ(tripletNote->tupletNormalNotes, 2);
    EXPECT_EQ(quadrupletNote->tupletActualNotes, 4);
    EXPECT_EQ(quadrupletNote->tupletNormalNotes, 2);
    constexpr float kMusicStart = 38.0F;
    constexpr float kRightMusicInset = 8.0F;
    constexpr float kVoiceRightExtent = 16.0F;
    const float timelineWidth = tripletMeasure.staffWidth - kMusicStart -
                                kRightMusicInset - kVoiceRightExtent;
    EXPECT_NEAR(tripletNote->normalizedTime, 7.0F / 9.0F, 0.0001F);
    EXPECT_NEAR(tripletNote->position.x,
                kMusicStart + timelineWidth * (7.0F / 9.0F), 0.0001F);
    EXPECT_NEAR(quadrupletNote->normalizedTime, 3.0F / 4.0F, 0.0001F);
    EXPECT_NEAR(quadrupletNote->position.x,
                kMusicStart + timelineWidth * (3.0F / 4.0F), 0.0001F);
}

TEST_F(SmuflFallbackTest, GraceStealsTimeFromItsPrecedingAnchor) {
    Measure measure = FixtureMeasure("grace-anchor");
    measure.voices[0].notes = {{Pitch {0, 4}, NoteDuration::Quarter},
                                {Pitch {1, 4}, NoteDuration::Eighth, false, true},
                                {Pitch {2, 4}, NoteDuration::Quarter}};
    const LayoutResult layout = engraver.LayoutMeasure(measure, nullptr, nullptr);
    ASSERT_TRUE(layout.valid);
    const LayoutElement* anchor = nullptr;
    const LayoutElement* grace = nullptr;
    const LayoutElement* following = nullptr;
    for (const LayoutElement& element : layout.elements) {
        if (element.id == "grace-anchor:voice:0:note:0:notehead") {
            anchor = &element;
        } else if (element.id == "grace-anchor:voice:0:note:1:notehead") {
            grace = &element;
        } else if (element.id == "grace-anchor:voice:0:note:2:notehead") {
            following = &element;
        }
    }
    ASSERT_NE(anchor, nullptr);
    ASSERT_NE(grace, nullptr);
    ASSERT_NE(following, nullptr);
    EXPECT_GT(grace->normalizedTime, anchor->normalizedTime);
    EXPECT_LT(grace->normalizedTime, following->normalizedTime);
    Measure invalid = measure;
    invalid.voices[0].notes.erase(invalid.voices[0].notes.begin());
    const LayoutResult rejected = engraver.LayoutMeasure(invalid, nullptr, nullptr);
    EXPECT_FALSE(rejected.valid);
    EXPECT_NE(rejected.diagnostic.find("preceding anchored note"), std::string::npos);
}

TEST_F(SmuflFallbackTest, DistinguishesTieAndSlurCurves) {
    Measure measure = FixtureMeasure("spans");
    measure.voices[0].notes[0].tied = true;
    measure.voices[0].slurs.push_back({0U, 2U});
    const LayoutResult layout = engraver.LayoutMeasure(measure, nullptr, nullptr);
    ASSERT_TRUE(layout.valid);
    const LayoutElement* tie = FindRole(layout, "tie");
    const LayoutElement* slur = FindRole(layout, "slur");
    ASSERT_NE(tie, nullptr);
    ASSERT_NE(slur, nullptr);
    EXPECT_EQ(tie->geometry, GeometryKind::Curve);
    EXPECT_EQ(slur->geometry, GeometryKind::Curve);
    EXPECT_NE(tie->id, slur->id);
    EXPECT_NE(tie->controlPoint.y, slur->controlPoint.y);
}

TEST_F(SmuflFallbackTest, EmitsClefKeyAndTimeChangeState) {
    Measure first = FixtureMeasure("first");
    Measure second = FixtureMeasure("second");
    second.clef = Clef::Bass;
    second.keyFifths = -3;
    second.timeNumerator = 3;
    second.timeDenominator = 8;
    const LayoutResult firstLayout = engraver.LayoutMeasure(first, nullptr, nullptr);
    const LayoutResult secondLayout = engraver.LayoutMeasure(second, &first, &firstLayout);
    ASSERT_TRUE(secondLayout.valid);
    EXPECT_GT(secondLayout.totalWidth, firstLayout.totalWidth);
    EXPECT_EQ(CountType(secondLayout, LayoutElement::Type::KeySig), 3U);
    EXPECT_EQ(CountType(secondLayout, LayoutElement::Type::TimeSig), 2U);
    ASSERT_NE(FindRole(secondLayout, "clef"), nullptr);
    EXPECT_EQ(FindRole(secondLayout, "clef")->smuflCodepoint, 0xE062U);
}

TEST_F(SmuflFallbackTest, EmitsCrescendoDiminuendoAndArticulations) {
    Measure crescendo = FixtureMeasure("crescendo");
    crescendo.hasHairpin = true;
    crescendo.voices[0].notes[0].articulations = {Articulation::Staccato,
                                                    Articulation::Accent,
                                                    Articulation::Tenuto};
    const LayoutResult first = engraver.LayoutMeasure(crescendo, nullptr, nullptr);
    Measure diminuendo = crescendo;
    diminuendo.stableId = "diminuendo";
    diminuendo.hairpinCrescendo = false;
    const LayoutResult second = engraver.LayoutMeasure(diminuendo, nullptr, nullptr);
    EXPECT_EQ(CountType(first, LayoutElement::Type::Hairpin), 3U);
    EXPECT_EQ(CountType(second, LayoutElement::Type::Hairpin), 3U);
    EXPECT_EQ(CountType(first, LayoutElement::Type::ArticulationGlyph), 3U);
    ASSERT_NE(FindRole(first, "articulation"), nullptr);
    EXPECT_LT(FindRole(first, "articulation")->position.y, 24.0F);
    std::vector<const LayoutElement*> articulations;
    for (const LayoutElement& element : first.elements) {
        if (element.role == "articulation") {
            articulations.push_back(&element);
        }
    }
    ASSERT_EQ(articulations.size(), 3U);
    for (std::size_t left = 0; left < articulations.size(); ++left) {
        for (std::size_t right = left + 1U; right < articulations.size(); ++right) {
            EXPECT_FALSE(BoundsOverlap(articulations[left]->boundingBox,
                                       articulations[right]->boundingBox));
        }
    }
}

TEST_F(SmuflFallbackTest, HitTestingReturnsStableSemanticIdentity) {
    const LayoutResult layout = engraver.LayoutMeasure(FixtureMeasure("hit"), nullptr, nullptr);
    const LayoutElement* notehead = FindRole(layout, "notehead");
    ASSERT_NE(notehead, nullptr);
    const Point2D point = {notehead->boundingBox.x + notehead->boundingBox.w * 0.5F,
                           notehead->boundingBox.y + notehead->boundingBox.h * 0.5F};
    const std::optional<std::string> first = engraver.HitTest(point, layout);
    const std::optional<std::string> second = engraver.HitTest(point, layout);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, notehead->id);
    EXPECT_EQ(*first, *second);
    EXPECT_FALSE(engraver.HitTest({-1.0F, -1.0F}, layout).has_value());
    EXPECT_FALSE(engraver.HitTest({NAN, 0.0F}, layout).has_value());
}

TEST_F(SmuflFallbackTest, IncrementalRelayoutPreservesPriorLayoutAndBoundsWork) {
    const Measure first = FixtureMeasure("incremental-first");
    Measure second = FixtureMeasure("incremental-second");
    second.voices[0].notes[1].pitch.accidental = Accidental::Sharp;
    const LayoutResult firstLayout = engraver.LayoutMeasure(first, nullptr, nullptr);
    const std::vector<LayoutElement> snapshot = firstLayout.elements;
    const LayoutResult secondLayout = engraver.LayoutMeasure(second, &first, &firstLayout);
    ASSERT_TRUE(secondLayout.valid);
    EXPECT_TRUE(secondLayout.work.reusedPrecedingLayout);
    EXPECT_EQ(secondLayout.work.visitedNotes, second.voices[0].notes.size());
    EXPECT_GT(secondLayout.totalWidth, firstLayout.totalWidth);
    ASSERT_EQ(firstLayout.elements.size(), snapshot.size());
    for (std::size_t index = 0; index < snapshot.size(); ++index) {
        EXPECT_EQ(firstLayout.elements[index].id, snapshot[index].id);
        EXPECT_EQ(firstLayout.elements[index].position.x, snapshot[index].position.x);
        EXPECT_EQ(firstLayout.elements[index].boundingBox.y, snapshot[index].boundingBox.y);
    }
}

TEST_F(SmuflFallbackTest, AdaptsToArbitraryWidthsWithoutOutOfBoundsGeometry) {
    Measure narrow = FixtureMeasure("narrow");
    narrow.staffWidth = 128.0F;
    Measure wide = narrow;
    wide.stableId = "wide";
    wide.staffWidth = 384.0F;
    const LayoutResult narrowLayout = engraver.LayoutMeasure(narrow, nullptr, nullptr);
    const LayoutResult wideLayout = engraver.LayoutMeasure(wide, nullptr, nullptr);
    EXPECT_TRUE(narrowLayout.valid);
    EXPECT_TRUE(wideLayout.valid);
    EXPECT_TRUE(GeometryIsFiniteAndBounded(narrowLayout));
    EXPECT_TRUE(GeometryIsFiniteAndBounded(wideLayout));
    EXPECT_EQ(narrowLayout.totalWidth, 128.0F);
    EXPECT_EQ(wideLayout.totalWidth, 384.0F);
}

TEST_F(SmuflFallbackTest, ReservesFourVoiceColumnsAtNarrowWidthAndAppends) {
    Measure measure = FixtureMeasure("narrow-four-voices");
    measure.staffWidth = 128.0F;
    measure.voices.clear();
    for (int voiceIndex = 0; voiceIndex < 4; ++voiceIndex) {
        Voice voice;
        voice.notes = {{Pitch {0, 4}, NoteDuration::Eighth},
                       {Pitch {2, 4}, NoteDuration::Eighth},
                       {Pitch {4, 4}, NoteDuration::Eighth}};
        measure.voices.push_back(std::move(voice));
    }
    const LayoutResult layout = engraver.LayoutMeasure(measure, nullptr, nullptr);
    ASSERT_TRUE(layout.valid);
    EXPECT_TRUE(GeometryIsFiniteAndBounded(layout));
    const LayoutResult repeated = engraver.LayoutMeasure(measure, nullptr, nullptr);
    ASSERT_TRUE(repeated.valid);
    ASSERT_EQ(repeated.elements.size(), layout.elements.size());
    for (std::size_t elementIndex = 0; elementIndex < layout.elements.size(); ++elementIndex) {
        const LayoutElement& first = layout.elements[elementIndex];
        const LayoutElement& second = repeated.elements[elementIndex];
        EXPECT_EQ(first.id, second.id);
        EXPECT_EQ(first.position.x, second.position.x);
        EXPECT_EQ(first.position.y, second.position.y);
        EXPECT_EQ(first.endPoint.x, second.endPoint.x);
        EXPECT_EQ(first.endPoint.y, second.endPoint.y);
        EXPECT_EQ(first.controlPoint.x, second.controlPoint.x);
        EXPECT_EQ(first.controlPoint.y, second.controlPoint.y);
        EXPECT_EQ(first.boundingBox.x, second.boundingBox.x);
        EXPECT_EQ(first.boundingBox.y, second.boundingBox.y);
        EXPECT_EQ(first.boundingBox.w, second.boundingBox.w);
        EXPECT_EQ(first.boundingBox.h, second.boundingBox.h);
    }
    std::vector<std::vector<float>> positions(4U);
    for (const LayoutElement& element : layout.elements) {
        if (element.role == "notehead") {
            positions[static_cast<std::size_t>(element.voiceIndex)].push_back(element.position.x);
        }
    }
    for (std::size_t voiceIndex = 0; voiceIndex < positions.size(); ++voiceIndex) {
        ASSERT_EQ(positions[voiceIndex].size(), 3U);
        if (voiceIndex == 0U) {
            continue;
        }
        for (std::size_t noteIndex = 0; noteIndex < positions[voiceIndex].size(); ++noteIndex) {
            EXPECT_GE(positions[voiceIndex][noteIndex] - positions[voiceIndex - 1U][noteIndex],
                      10.0F);
        }
    }
    Measure appendedMeasure = measure;
    appendedMeasure.stableId = "narrow-four-voices-appended";
    const LayoutResult appended = engraver.LayoutMeasure(appendedMeasure, &measure, &layout);
    EXPECT_TRUE(appended.valid);
    EXPECT_TRUE(appended.work.reusedPrecedingLayout);
    EXPECT_EQ(appended.totalWidth, 256.0F);
    EXPECT_TRUE(GeometryIsFiniteAndBounded(appended));

    Measure impossible = measure;
    impossible.keyFifths = 7;
    const LayoutResult rejected = engraver.LayoutMeasure(impossible, nullptr, nullptr);
    EXPECT_FALSE(rejected.valid);
    EXPECT_TRUE(rejected.elements.empty());
    EXPECT_TRUE(std::isfinite(rejected.totalWidth));
    EXPECT_NE(rejected.diagnostic.find("cannot reserve"), std::string::npos);
}

TEST_F(SmuflFallbackTest, RejectsMalformedMeasuresWithoutGeometry) {
    Measure noVoice = FixtureMeasure("bad-voices");
    noVoice.voices.clear();
    const LayoutResult emptyVoices = engraver.LayoutMeasure(noVoice, nullptr, nullptr);
    EXPECT_FALSE(emptyVoices.valid);
    EXPECT_TRUE(emptyVoices.elements.empty());
    Measure badWidth = FixtureMeasure("bad-width");
    const std::vector<float> invalidWidths = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::max(),
        127.0F,
    };
    for (const float width : invalidWidths) {
        badWidth.staffWidth = width;
        const LayoutResult rejected = engraver.LayoutMeasure(badWidth, nullptr, nullptr);
        EXPECT_FALSE(rejected.valid);
        EXPECT_TRUE(rejected.elements.empty());
        EXPECT_TRUE(std::isfinite(rejected.totalWidth));
    }
    Measure badSlur = FixtureMeasure("bad-slur");
    badSlur.voices[0].slurs.push_back({2U, 1U});
    EXPECT_FALSE(engraver.LayoutMeasure(badSlur, nullptr, nullptr).valid);
    Measure badTuplet = FixtureMeasure("bad-tuplet");
    badTuplet.voices[0].tuplets.push_back({0, 2, {}});
    EXPECT_FALSE(engraver.LayoutMeasure(badTuplet, nullptr, nullptr).valid);
    Measure badPitch = FixtureMeasure("bad-pitch");
    badPitch.voices[0].notes[0].pitch.step = std::numeric_limits<int>::max();
    EXPECT_FALSE(engraver.LayoutMeasure(badPitch, nullptr, nullptr).valid);
    badPitch.voices[0].notes[0].pitch.step = 0;
    badPitch.voices[0].notes[0].pitch.octave = std::numeric_limits<int>::min();
    EXPECT_FALSE(engraver.LayoutMeasure(badPitch, nullptr, nullptr).valid);
}

TEST_F(SmuflFallbackTest, RejectsHostilePriorLayoutGeometry) {
    const Measure measure = FixtureMeasure("prior");
    LayoutResult prior = engraver.LayoutMeasure(measure, nullptr, nullptr);
    ASSERT_TRUE(prior.valid);
    prior.totalWidth = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(engraver.LayoutMeasure(measure, &measure, &prior).valid);
    prior = engraver.LayoutMeasure(measure, nullptr, nullptr);
    prior.elements[0].boundingBox.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(engraver.LayoutMeasure(measure, &measure, &prior).valid);
    prior = engraver.LayoutMeasure(measure, nullptr, nullptr);
    prior.elements[0].endPoint.x = -1.0F;
    EXPECT_FALSE(engraver.LayoutMeasure(measure, &measure, &prior).valid);
    prior = engraver.LayoutMeasure(measure, nullptr, nullptr);
    prior.totalWidth = 10000000.0F;
    EXPECT_FALSE(engraver.LayoutMeasure(measure, &measure, &prior).valid);
}

TEST(SmuflFallbackLifecycleTest, RejectsLayoutBeforeInitialization) {
    SmuflFallbackEngraver engraver;
    const LayoutResult layout = engraver.LayoutMeasure(FixtureMeasure("uninitialized"), nullptr, nullptr);
    EXPECT_FALSE(layout.valid);
    EXPECT_TRUE(layout.elements.empty());
}

TEST(EngravingEvaluatorTest, EvaluatesOwnedFallbackAgainstEveryCriterion) {
    EngravingEvaluator evaluator;
    evaluator.RegisterCandidate(std::make_unique<SmuflFallbackEngraver>());
    ASSERT_TRUE(evaluator.EvaluateAll());
    const std::vector<EvaluationResult>& results = evaluator.Results();
    ASSERT_EQ(results.size(), 15U);
    for (const EvaluationResult& result : results) {
        EXPECT_TRUE(result.passed) << result.testName << ": " << result.detail;
        EXPECT_FALSE(result.detail.empty());
    }
}

TEST(EngravingEvaluatorTest, RejectsMutatedOutputDespiteCapabilityFlags) {
    ExpectEvaluatorRejectsMutation(Mutation::Hairpin, "hairpins");
    ExpectEvaluatorRejectsMutation(Mutation::Tuplet, "tuplets");
    ExpectEvaluatorRejectsMutation(Mutation::TupletActual, "tuplet actual counts");
    ExpectEvaluatorRejectsMutation(Mutation::TupletActualTiming, "tuplet actual counts");
    ExpectEvaluatorRejectsMutation(Mutation::Articulation, "articulation placement");
    ExpectEvaluatorRejectsMutation(Mutation::Grace, "grace notes");
    ExpectEvaluatorRejectsMutation(Mutation::IncrementalWork, "incremental layout");
}

} // namespace
} // namespace engraving
