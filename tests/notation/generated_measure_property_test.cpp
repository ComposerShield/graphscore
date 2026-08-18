// SPDX-License-Identifier: Apache-2.0

#include "note_entry_test_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using note_entry_test::Fixture;
using note_entry_test::measure;

class DeterministicGenerator {
 public:
  explicit DeterministicGenerator(std::uint32_t seed) : state_(seed) {}

  [[nodiscard]] std::uint32_t next() {
    state_ = state_ * 1664525U + 1013904223U;
    return state_;
  }

  [[nodiscard]] std::size_t index(std::size_t count) {
    return static_cast<std::size_t>(next()) % count;
  }

 private:
  std::uint32_t state_;
};

[[nodiscard]] std::vector<Duration> duration_choices(Rational remaining) {
  constexpr std::array<NoteValue, 7> kValues = {
      NoteValue::kWhole,       NoteValue::kHalf,      NoteValue::kQuarter,
      NoteValue::kEighth,      NoteValue::kSixteenth, NoteValue::kThirtySecond,
      NoteValue::kSixtyFourth,
  };

  std::vector<Duration> choices;
  for (const NoteValue value : kValues) {
    for (std::uint8_t dots = 0; dots <= Duration::kMaxDots; ++dots) {
      const Duration duration = *Duration::create(value, dots);
      const Rational resolved = duration.resolved();
      const Rational after    = remaining - resolved;
      if (resolved <= remaining && after >= Rational(0) &&
          64 % after.denominator() == 0) {
        choices.push_back(duration);
      }
    }
  }
  return choices;
}

[[nodiscard]] SpelledPitch generated_pitch(DeterministicGenerator& generator) {
  constexpr std::array<Letter, 7> kLetters = {
      Letter::kC, Letter::kD, Letter::kE, Letter::kF,
      Letter::kG, Letter::kA, Letter::kB,
  };
  return *SpelledPitch::create(kLetters[generator.index(kLetters.size())], 4);
}

void append_generated_measure(VoiceContent& voice, Rational measure_duration,
                              DeterministicGenerator& generator) {
  Rational position(0);
  while (position < measure_duration) {
    const std::vector<Duration> choices =
        duration_choices(measure_duration - position);
    ASSERT_FALSE(choices.empty());
    const Duration& duration = choices[generator.index(choices.size())];

    const VoiceEvent event =
        generator.next() % 2U == 0U
            ? VoiceEvent{make_note(generated_pitch(generator), duration)}
            : VoiceEvent{make_rest(duration)};
    ASSERT_TRUE(voice.append(event).ok());
    position = position + duration.resolved();
  }
}

void expect_non_overlapping_complete_voice(const VoiceContent& voice,
                                           Rational measure_duration) {
  Rational previous_end(0);
  for (const VoiceEvent& event : voice.events()) {
    const auto onset = voice.position_of_event(event_id(event));
    ASSERT_TRUE(onset.has_value());
    EXPECT_GE(*onset, previous_end);
    EXPECT_EQ(*onset, previous_end);
    previous_end = *onset + event_duration(event).resolved();
  }

  EXPECT_EQ(previous_end, measure_duration);
  EXPECT_EQ(voice.total_length(), measure_duration);
  EXPECT_TRUE(voice.check_complete(measure_duration).ok());
  EXPECT_TRUE(voice.validate().ok());
}

TEST(GeneratedMeasurePropertyTest,
     ValidMeasuresHaveNonOverlappingCompleteVoices) {
  struct Meter {
    std::uint8_t  numerator;
    std::uint16_t denominator;
  };

  constexpr std::array<Meter, 8> kMeters = {
      Meter{2, 2}, Meter{3, 4}, Meter{4, 4}, Meter{5, 4},
      Meter{6, 8}, Meter{7, 8}, Meter{9, 8}, Meter{5, 16},
  };
  constexpr std::uint32_t kSeedCount = 32;

  for (const Meter meter : kMeters) {
    for (std::uint32_t seed = 0; seed < kSeedCount; ++seed) {
      Fixture fixture({measure(meter.numerator, meter.denominator)});
      for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
           ++voice_index) {
        DeterministicGenerator generator(
            seed * 31U + static_cast<std::uint32_t>(voice_index));
        VoiceContent& voice = fixture.voice(voice_index);
        append_generated_measure(voice, fixture.node_end(), generator);
        expect_non_overlapping_complete_voice(voice, fixture.node_end());
      }
    }
  }
}

}  // namespace
