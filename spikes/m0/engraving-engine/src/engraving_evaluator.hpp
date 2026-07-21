// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "engraving_candidate.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engraving {

/// Result of a single evaluation test case.
struct EvaluationResult {
    std::string testName;
    bool passed = false;
    std::string detail;
};

class EngravingEvaluator {
public:
    EngravingEvaluator() = default;

    /// Register a candidate for evaluation.
    void RegisterCandidate(std::unique_ptr<IEngravingCandidate> candidate);

    /// Run the full evaluation suite against all registered candidates.
    /// Returns true if all tests pass for all candidates.
    bool EvaluateAll();

    /// Get all results from the last evaluation run.
    const std::vector<EvaluationResult>& Results() const;

    /// Save results as JSON to path.
    bool SaveResults(const char* path) const;

private:
    std::vector<std::unique_ptr<IEngravingCandidate>> m_candidates;
    std::vector<EvaluationResult> m_results;
    std::string m_candidateName;

    void AddResult(std::string_view name, bool passed, std::string detail = "");

    // ---- Test cases ----

    /// Test that a candidate can initialize and shut down cleanly.
    void Test_Initialization(IEngravingCandidate& candidate);

    /// Test layout of a simple single-voice measure with no complications.
    void Test_BasicSingleVoice(IEngravingCandidate& candidate);

    /// Test layout of a four-voice SATB measure.
    void Test_FourVoices(IEngravingCandidate& candidate);

    /// Test that incremental layout (appending measures) produces
    /// correct continuous output.
    void Test_IncrementalLayout(IEngravingCandidate& candidate);

    /// Test tuplet rendering (triplets in a single voice).
    void Test_Tuplets(IEngravingCandidate& candidate);

    /// Test that tuplet actual counts affect retained semantics and timing.
    void Test_TupletActualCounts(IEngravingCandidate& candidate);

    /// Test grace note rendering.
    void Test_GraceNotes(IEngravingCandidate& candidate);

    /// Test clef, key, and time signature changes across measures.
    void Test_ClefKeyTimeChanges(IEngravingCandidate& candidate);

    /// Test ties across consecutive notes.
    void Test_Ties(IEngravingCandidate& candidate);

    /// Test slur rendering.
    void Test_Slurs(IEngravingCandidate& candidate);

    /// Test hairpin (crescendo/diminuendo) rendering.
    void Test_Hairpins(IEngravingCandidate& candidate);

    /// Test articulation placement (staccato, accent, tenuto).
    void Test_ArticulationPlacement(IEngravingCandidate& candidate);

    /// Test that hit testing maps coordinates back to notation elements.
    void Test_HitTesting(IEngravingCandidate& candidate);

    /// Test layout with an arbitrarily wide staff (non-standard width).
    void Test_ArbitraryStaffWidth(IEngravingCandidate& candidate);

    /// Verify the engine is not page-oriented (rejection criterion).
    void Test_NotPageOriented(IEngravingCandidate& candidate);
};

} // namespace engraving
