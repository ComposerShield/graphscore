// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/midi_ownership.hpp>

#include <algorithm>

namespace graphscore {

PickdownTailSnapshotId MidiOwnershipTracker::begin_pickdown_tail(
    NodeId source_node) {
  const PickdownTailSnapshotId snapshot = PickdownTailSnapshotId::generate();
  snapshot_source_node_.emplace(snapshot, source_node);
  return snapshot;
}

void MidiOwnershipTracker::transfer_main_to_pickdown_tail(
    NodeId source_node, PickdownTailSnapshotId snapshot) {
  const MidiOwnerScope tail_scope = MidiOwnerScope::pickdown_tail(snapshot);

  for (auto& entry : note_owners_) {
    MidiOwnerScope& scope = entry.second.scope;
    if (scope.region() == MidiOwnerRegion::kMain &&
        scope.main_node() == source_node) {
      scope = tail_scope;
    }
  }

  for (auto& entry : active_pedal_spans_) {
    MidiOwnerScope& scope = entry.second;
    if (scope.region() == MidiOwnerRegion::kMain &&
        scope.main_node() == source_node) {
      scope = tail_scope;
    }
  }
}

std::optional<NodeId> MidiOwnershipTracker::pickdown_tail_source_node(
    PickdownTailSnapshotId snapshot) const {
  const auto it = snapshot_source_node_.find(snapshot);
  return it == snapshot_source_node_.end() ? std::nullopt
                                           : std::optional(it->second);
}

MidiNoteAttack MidiOwnershipTracker::attack_note(MidiChannel    channel,
                                                 MidiPitch      pitch,
                                                 MidiOwnerScope scope) {
  const MidiAttackId attack_id = MidiAttackId::generate();
  const NoteKey      key{channel, pitch};

  std::optional<MidiAttackId> superseded;
  const auto                  existing = note_owners_.find(key);
  if (existing != note_owners_.end()) {
    superseded = existing->second.attack_id;
    attack_lookup_.erase(existing->second.attack_id);
    existing->second = OwnedNote{attack_id, scope};
  } else {
    note_owners_.emplace(key, OwnedNote{attack_id, scope});
  }
  attack_lookup_.emplace(attack_id, key);

  return MidiNoteAttack{attack_id, superseded};
}

MidiNoteReleaseOutcome MidiOwnershipTracker::release_note(
    MidiAttackId attack_id) {
  const auto lookup_it = attack_lookup_.find(attack_id);
  if (lookup_it == attack_lookup_.end())
    return MidiNoteReleaseOutcome::kSuppressed;

  const NoteKey key      = lookup_it->second;
  const auto    owner_it = note_owners_.find(key);
  attack_lookup_.erase(lookup_it);

  if (owner_it == note_owners_.end() || owner_it->second.attack_id != attack_id)
    return MidiNoteReleaseOutcome::kSuppressed;

  note_owners_.erase(owner_it);
  return MidiNoteReleaseOutcome::kEmitsNoteOff;
}

std::optional<MidiAttackId> MidiOwnershipTracker::current_note_owner(
    MidiChannel channel, MidiPitch pitch) const {
  const auto it = note_owners_.find(NoteKey{channel, pitch});
  return it == note_owners_.end() ? std::nullopt
                                  : std::optional(it->second.attack_id);
}

MidiPedalTransition MidiOwnershipTracker::pedal_down(MidiChannel      channel,
                                                     NotationEntityId span_id,
                                                     MidiOwnerScope   scope) {
  const PedalKey key{channel, span_id};
  if (active_pedal_spans_.contains(key))
    return MidiPedalTransition::kNoChange;

  active_pedal_spans_.emplace(key, scope);
  std::size_t& count         = pedal_active_count_[channel.value()];
  const bool   became_active = (count == 0);
  ++count;
  return became_active ? MidiPedalTransition::kBecameActive
                       : MidiPedalTransition::kNoChange;
}

MidiPedalTransition MidiOwnershipTracker::pedal_up(MidiChannel      channel,
                                                   NotationEntityId span_id) {
  const auto it = active_pedal_spans_.find(PedalKey{channel, span_id});
  if (it == active_pedal_spans_.end())
    return MidiPedalTransition::kNoChange;

  active_pedal_spans_.erase(it);
  std::size_t& count = pedal_active_count_[channel.value()];
  --count;
  return count == 0 ? MidiPedalTransition::kBecameInactive
                    : MidiPedalTransition::kNoChange;
}

bool MidiOwnershipTracker::pedal_active(MidiChannel channel) const noexcept {
  return pedal_active_count_[channel.value()] > 0;
}

MidiOwnershipRelease MidiOwnershipTracker::vertical_jump(NodeId source_node) {
  return release_where(/*release_all=*/false, source_node, std::nullopt);
}

MidiOwnershipRelease MidiOwnershipTracker::clear_all() {
  MidiOwnershipRelease release =
      release_where(/*release_all=*/true, std::nullopt, std::nullopt);
  snapshot_source_node_.clear();
  return release;
}

MidiOwnershipRelease MidiOwnershipTracker::panic() {
  MidiOwnershipRelease release =
      release_where(/*release_all=*/true, std::nullopt, std::nullopt);
  snapshot_source_node_.clear();
  return release;
}

MidiOwnershipRelease MidiOwnershipTracker::retire_pickdown_tail_snapshot(
    PickdownTailSnapshotId snapshot) {
  MidiOwnershipRelease release =
      release_where(/*release_all=*/false, std::nullopt, snapshot);
  snapshot_source_node_.erase(snapshot);
  return release;
}

MidiOwnershipRelease MidiOwnershipTracker::release_where(
    bool release_all, std::optional<NodeId> main_node_filter,
    std::optional<PickdownTailSnapshotId> tail_snapshot_filter) {
  const auto matches_filter = [&](const MidiOwnerScope& scope) {
    return release_all ||
           (main_node_filter.has_value() &&
            scope.region() == MidiOwnerRegion::kMain &&
            scope.main_node() == main_node_filter) ||
           (tail_snapshot_filter.has_value() &&
            scope.region() == MidiOwnerRegion::kPickdownTail &&
            scope.tail_snapshot() == tail_snapshot_filter);
  };

  MidiOwnershipRelease release;

  for (auto it = note_owners_.begin(); it != note_owners_.end();) {
    if (!matches_filter(it->second.scope)) {
      ++it;
      continue;
    }

    release.notes.push_back(MidiReleasedNote{it->first.channel, it->first.pitch,
                                             it->second.attack_id});
    attack_lookup_.erase(it->second.attack_id);
    it = note_owners_.erase(it);
  }

  for (auto it = active_pedal_spans_.begin();
       it != active_pedal_spans_.end();) {
    if (!matches_filter(it->second)) {
      ++it;
      continue;
    }

    const MidiChannel channel = it->first.channel;
    std::size_t&      count   = pedal_active_count_[channel.value()];
    --count;
    if (count == 0)
      release.pedals.push_back(MidiReleasedPedal{channel});
    it = active_pedal_spans_.erase(it);
  }

  std::sort(release.notes.begin(), release.notes.end(),
            [](const MidiReleasedNote& lhs, const MidiReleasedNote& rhs) {
              if (lhs.channel.value() != rhs.channel.value())
                return lhs.channel.value() < rhs.channel.value();
              return lhs.pitch.value() < rhs.pitch.value();
            });
  std::sort(release.pedals.begin(), release.pedals.end(),
            [](const MidiReleasedPedal& lhs, const MidiReleasedPedal& rhs) {
              return lhs.channel.value() < rhs.channel.value();
            });

  return release;
}

}  // namespace graphscore
