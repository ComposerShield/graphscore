// SPDX-License-Identifier: Apache-2.0

#include "engraving_evaluator.hpp"
#include "smuf_fallback.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engraving {
namespace {

Measure MeasurementMeasure(const std::size_t index) {
    Measure measure;
    measure.stableId = "scale-" + std::to_string(index);
    measure.staffWidth = 320.0F;
    Voice voice;
    voice.notes = {{Pitch {0, 4}, NoteDuration::Eighth},
                   {Pitch {2, 4}, NoteDuration::Eighth},
                   {Pitch {4, 4}, NoteDuration::Eighth},
                   {Pitch {6, 4}, NoteDuration::Eighth}};
    measure.voices.push_back(std::move(voice));
    return measure;
}

int RunMeasurement() {
    SmuflFallbackEngraver engraver;
    if (!engraver.Initialize()) {
        return 1;
    }
    constexpr std::size_t kMeasureCount = 64U;
    constexpr std::size_t kRelayoutIterations = 1000U;
    std::array<Measure, kMeasureCount> measures {};
    std::array<LayoutResult, kMeasureCount> layouts {};
    for (std::size_t index = 0; index < kMeasureCount; ++index) {
        measures[index] = MeasurementMeasure(index);
        layouts[index] = engraver.LayoutMeasure(
            measures[index], index == 0U ? nullptr : &measures[index - 1U],
            index == 0U ? nullptr : &layouts[index - 1U]);
    }
    Measure changed = measures[32U];
    changed.voices[0].notes[2].pitch = {5, 5, Accidental::Sharp};
    const auto start = std::chrono::steady_clock::now();
    LayoutResult changedLayout;
    for (std::size_t iteration = 0; iteration < kRelayoutIterations; ++iteration) {
        changedLayout = engraver.LayoutMeasure(changed, &measures[31U], &layouts[31U]);
    }
    const auto finish = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(finish - start);
    const bool passed = changedLayout.valid && changedLayout.incremental &&
                        changedLayout.work.reusedPrecedingLayout &&
                        changedLayout.work.visitedNotes == changed.voices[0].notes.size() &&
                        changedLayout.totalWidth == layouts[31U].totalWidth + changed.staffWidth;
    std::printf("measurement.fixture=64 measures, 1 voice, 4 eighth notes, staffWidth=320\n");
    std::printf("measurement.incremental_iterations=%zu\n", kRelayoutIterations);
    std::printf("measurement.elapsed_us=%lld\n", static_cast<long long>(elapsed.count()));
    std::printf("measurement.changed_measure_work.visited_notes=%zu\n",
                changedLayout.work.visitedNotes);
    std::printf("measurement.changed_measure_work.emitted_elements=%zu\n",
                changedLayout.work.emittedElements);
    std::printf("measurement.width_adaptation=128..384 verified by evaluator fixture\n");
    std::printf("measurement.verdict=%s\n", passed ? "PASS" : "FAIL");
    engraver.Shutdown();
    return passed ? 0 : 1;
}

} // namespace
} // namespace engraving

int main(const int argc, char* const* const argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--measure") {
        return engraving::RunMeasurement();
    }
    if (argc != 1) {
        std::fprintf(stderr, "usage: engraving_spike_evaluator [--measure]\n");
        return 2;
    }
    engraving::EngravingEvaluator evaluator;
    evaluator.RegisterCandidate(std::make_unique<engraving::SmuflFallbackEngraver>());
    return evaluator.EvaluateAll() ? 0 : 1;
}
