// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace graphscore {

// Writer-only per-track mix settings for audition playback.  These values
// are stripped from cooked runtime data and exist only in the editable
// project bundle.
//
// Defaults reflect a nominally audible mix: 80 % gain (linear), centre
// pan, unmuted, unsoloed.
//
// Gain and pan legal ranges are defined by the M08 writer mixer (the domain
// model stores the value as-is without validation); the mixer is
// responsible for clamping and for mapping to its internal representation.
class AuditionMixSettings {
 public:
  AuditionMixSettings() = default;

  [[nodiscard]] float gain() const noexcept { return gain_; }

  void set_gain(float value) noexcept { gain_ = value; }

  [[nodiscard]] float pan() const noexcept { return pan_; }

  void set_pan(float value) noexcept { pan_ = value; }

  [[nodiscard]] bool mute() const noexcept { return mute_; }

  void set_mute(bool value) noexcept { mute_ = value; }

  [[nodiscard]] bool solo() const noexcept { return solo_; }

  void set_solo(bool value) noexcept { solo_ = value; }

  [[nodiscard]] bool operator==(const AuditionMixSettings&) const = default;

 private:
  float gain_ = 0.8F;
  float pan_  = 0.0F;
  bool  mute_ = false;
  bool  solo_ = false;
};

}  // namespace graphscore
