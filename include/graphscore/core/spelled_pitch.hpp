// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

#include <graphscore/core/accidental.hpp>
#include <graphscore/core/midi_pitch.hpp>

namespace graphscore {

// Diatonic letter name, A through G.
enum class Letter : std::uint8_t {
  kA = 0,
  kB,
  kC,
  kD,
  kE,
  kF,
  kG,
};

// A fully spelled note: diatonic letter, octave in scientific pitch
// notation (octave 4 contains middle C, MidiPitch 60), and an Accidental
// from double-flat through double-sharp. SpelledPitch is the notation-
// facing representation musicians read; MidiPitch is the sounding
// representation. The conversion between them is explicit and validated,
// never implicit, because distinct spellings (e.g. C-sharp and D-flat)
// intentionally collapse to the same MidiPitch.
class SpelledPitch {
 public:
  static constexpr std::int8_t kMinOctave = -1;
  static constexpr std::int8_t kMaxOctave = 9;

  constexpr SpelledPitch() = default;

  [[nodiscard]] static constexpr std::optional<SpelledPitch> create(
      Letter letter, std::int8_t octave,
      Accidental accidental = Accidental::kNatural) noexcept {
    if (octave < kMinOctave || octave > kMaxOctave)
      return std::nullopt;
    return SpelledPitch(letter, octave, accidental);
  }

  [[nodiscard]] constexpr Letter letter() const noexcept { return letter_; }

  [[nodiscard]] constexpr std::int8_t octave() const noexcept {
    return octave_;
  }

  [[nodiscard]] constexpr Accidental accidental() const noexcept {
    return accidental_;
  }

  // Explicit, validated conversion to the sounding MIDI pitch under
  // standard 12-TET. Fails if the spelling resolves outside [0, 127], e.g.
  // an extreme octave pushed out of range by a double-sharp/double-flat.
  [[nodiscard]] constexpr std::optional<MidiPitch> to_midi_pitch()
      const noexcept {
    const int semitone = (static_cast<int>(octave_) + 1) * 12 +
                         letter_semitone(letter_) +
                         to_semitone_offset(accidental_);
    if (semitone < 0 || semitone > static_cast<int>(MidiPitch::kMax))
      return std::nullopt;
    return MidiPitch::create(static_cast<std::uint8_t>(semitone));
  }

  [[nodiscard]] constexpr bool operator==(const SpelledPitch&) const = default;

 private:
  constexpr SpelledPitch(Letter letter, std::int8_t octave,
                         Accidental accidental) noexcept
      : letter_(letter), octave_(octave), accidental_(accidental) {}

  // Semitone offset from C for the natural form of `letter`.
  [[nodiscard]] static constexpr std::uint8_t letter_semitone(
      Letter letter) noexcept {
    constexpr std::uint8_t kSemitoneFromC[] = {9, 11, 0, 2, 4, 5, 7};
    return kSemitoneFromC[static_cast<std::uint8_t>(letter)];
  }

  Letter      letter_     = Letter::kC;
  std::int8_t octave_     = 4;
  Accidental  accidental_ = Accidental::kNatural;
};

}  // namespace graphscore
