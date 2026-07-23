// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace graphscore {

// An opaque toolkit-independent identifier for a plugin type discovered by
// host-level scanning.  The domain layer treats every field as opaque — no
// VST3 SDK types appear here — while the VST3 host (Milestone 08) populates
// the 16-byte uid from a plugin's FUID and maps between PluginIdentity values
// and concrete plugin instances.
//
// Equality and ordering consider every field; two identities with the same
// uid and format but different human-readable names are considered distinct,
// matching the principle that the name is a stable scanned attribute, not a
// transient display string.
class PluginIdentity {
 public:
  PluginIdentity() = default;

  PluginIdentity(std::string name, std::string format,
                 std::array<std::uint8_t, 16> uid)
      : name_(std::move(name)), format_(std::move(format)), uid_(uid) {}

  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  [[nodiscard]] const std::string& format() const noexcept { return format_; }

  [[nodiscard]] const std::array<std::uint8_t, 16>& uid() const noexcept {
    return uid_;
  }

  [[nodiscard]] bool operator==(const PluginIdentity&) const = default;

  [[nodiscard]] auto operator<=>(const PluginIdentity&) const = default;

 private:
  std::string                  name_;
  std::string                  format_;
  std::array<std::uint8_t, 16> uid_{};
};

// Opaque serialized plugin-processor state stored as raw bytes.  The
// domain layer does not interpret or validate the contents; the VST3
// host is responsible for serialising and deserialising state to/from
// a concrete plugin instance.
class PluginStateBlob {
 public:
  PluginStateBlob() = default;

  explicit PluginStateBlob(std::vector<std::uint8_t> data)
      : data_(std::move(data)) {}

  [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept {
    return data_;
  }

  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

  [[nodiscard]] bool operator==(const PluginStateBlob&) const = default;

 private:
  std::vector<std::uint8_t> data_;
};

// One ordered effect slot in a track's plugin chain.  Owns a stable
// PluginIdentity, a PluginStateBlob holding the effect's saved processor
// state, and a bypass flag.  Order is determined by position in the
// containing TrackPluginChain::effects vector, not by a field on the
// slot itself.
class EffectSlot {
 public:
  EffectSlot(PluginIdentity identity, PluginStateBlob state,
             bool bypassed = false)
      : identity_(std::move(identity)),
        state_(std::move(state)),
        bypassed_(bypassed) {}

  [[nodiscard]] const PluginIdentity& identity() const noexcept {
    return identity_;
  }

  [[nodiscard]] const PluginStateBlob& state() const noexcept { return state_; }

  [[nodiscard]] bool bypassed() const noexcept { return bypassed_; }

  void set_bypassed(bool bypassed) noexcept { bypassed_ = bypassed; }

  void set_state(PluginStateBlob state) { state_ = std::move(state); }

  [[nodiscard]] bool operator==(const EffectSlot&) const = default;

 private:
  PluginIdentity  identity_;
  PluginStateBlob state_;
  bool            bypassed_ = false;
};

// The ordered plugin chain attached to a single Track (writer-only).
//
// Every track has exactly zero or one instrument slot; after the
// instrument, zero or more ordered effect slots each hold a plugin
// identity, state blob, and bypass flag.
//
// All public mutators operate on the chain directly with no side
// effects on other tracks or nodes — the writer-layer audio engine is
// responsible for wiring plugin instances and propagating state changes.
//
// The type is default-constructible (empty chain) and copyable;
// writer-layer persistence (Milestone 03) round-trips the entire chain
// including state blobs.
class TrackPluginChain {
 public:
  TrackPluginChain() = default;

  // --- instrument --------------------------------------------------------

  void set_instrument(PluginIdentity identity, PluginStateBlob state) {
    instrument_       = std::move(identity);
    instrument_state_ = std::move(state);
  }

  void clear_instrument() noexcept {
    instrument_.reset();
    instrument_state_.reset();
  }

  [[nodiscard]] const PluginIdentity* instrument() const noexcept {
    return instrument_ ? &*instrument_ : nullptr;
  }

  [[nodiscard]] const PluginStateBlob* instrument_state() const noexcept {
    return instrument_state_ ? &*instrument_state_ : nullptr;
  }

  // --- effects -----------------------------------------------------------
  //
  // Every effect-indexing method has a documented precondition on `index`.
  // The domain layer does not throw; callers are responsible for staying
  // within bounds, and Phase 9 validation can diagnose violations.

  [[nodiscard]] std::size_t effect_count() const noexcept {
    return effects_.size();
  }

  // Precondition: index < effect_count().
  [[nodiscard]] const EffectSlot& effect_at(std::size_t index) const {
    return effects_[index];
  }

  void add_effect(PluginIdentity identity, PluginStateBlob state,
                  bool bypassed = false) {
    effects_.emplace_back(std::move(identity), std::move(state), bypassed);
  }

  // Precondition: index <= effect_count() (index == effect_count() appends).
  void insert_effect(std::size_t index, PluginIdentity identity,
                     PluginStateBlob state, bool bypassed = false) {
    effects_.insert(
        effects_.begin() + static_cast<std::ptrdiff_t>(index),
        EffectSlot(std::move(identity), std::move(state), bypassed));
  }

  // Precondition: index < effect_count().
  void remove_effect(std::size_t index) {
    effects_.erase(effects_.begin() + static_cast<std::ptrdiff_t>(index));
  }

  // to_index is the slot's final position after removal; both indices must
  // be < effect_count().  Same-index is a no-op.
  void move_effect(std::size_t from_index, std::size_t to_index) {
    if (from_index == to_index)
      return;
    const auto from =
        effects_.begin() + static_cast<std::ptrdiff_t>(from_index);
    auto slot = std::move(*from);
    effects_.erase(from);
    effects_.insert(effects_.begin() + static_cast<std::ptrdiff_t>(to_index),
                    std::move(slot));
  }

  // Precondition: index < effect_count().
  void set_effect_bypass(std::size_t index, bool bypassed) {
    effects_[index].set_bypassed(bypassed);
  }

  // Precondition: index < effect_count().
  void set_effect_state(std::size_t index, PluginStateBlob state) {
    effects_[index].set_state(std::move(state));
  }

  [[nodiscard]] const std::vector<EffectSlot>& effects() const noexcept {
    return effects_;
  }

  [[nodiscard]] bool operator==(const TrackPluginChain&) const = default;

 private:
  std::optional<PluginIdentity>  instrument_;
  std::optional<PluginStateBlob> instrument_state_;
  std::vector<EffectSlot>        effects_;
};

}  // namespace graphscore
