// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "engraving_candidate.hpp"

#include <string>
#include <string_view>

namespace engraving {

class SmuflFallbackEngraver final : public IEngravingCandidate {
public:
    std::string_view Name() const override { return "Owned SMuFL fallback"; }
    std::string_view Version() const override { return "0.3.1-spike"; }
    bool Initialize() override;
    void Shutdown() override;
    LayoutResult LayoutMeasure(
        const Measure& measure,
        const Measure* context,
        const LayoutResult* appendTo) override;
    std::optional<std::string> HitTest(
        Point2D point,
        const LayoutResult& layout) override;

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
    float StaffStepToY(int step, int octave) const;
    bool m_initialized = false;
};

} // namespace engraving
