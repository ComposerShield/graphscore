// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::AuditionMixSettings;
using graphscore::EffectSlot;
using graphscore::PluginIdentity;
using graphscore::PluginStateBlob;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::StaffLayout;
using graphscore::TrackPluginChain;

namespace {

PluginIdentity make_identity(const char*  name   = "Test Plugin",
                             const char*  format = "VST3",
                             std::uint8_t seed   = 0) {
  std::array<std::uint8_t, 16> uid{};
  for (std::size_t i = 0; i < 16; ++i)
    uid[i] = static_cast<std::uint8_t>(seed + i);
  return PluginIdentity(name, format, uid);
}

PluginStateBlob make_blob(std::uint8_t seed = 0, std::size_t count = 8) {
  std::vector<std::uint8_t> data;
  data.reserve(count);
  for (std::size_t i = 0; i < count; ++i)
    data.push_back(static_cast<std::uint8_t>(seed + i));
  return PluginStateBlob(std::move(data));
}

Project make_project() {
  return Project(ProjectId::generate(), "Test Project");
}

}  // namespace

// --- PluginIdentity ------------------------------------------------------

TEST(PluginIdentityTest, DefaultConstructedHasEmptyNameAndFormat) {
  const PluginIdentity id;
  EXPECT_TRUE(id.name().empty());
  EXPECT_TRUE(id.format().empty());
}

TEST(PluginIdentityTest, ConstructedFieldsAreAccessible) {
  const auto uid = [] {
    std::array<std::uint8_t, 16> a{};
    a[0]  = 0xAB;
    a[15] = 0xCD;
    return a;
  }();
  const PluginIdentity id("Serum", "VST3", uid);
  EXPECT_EQ(id.name(), "Serum");
  EXPECT_EQ(id.format(), "VST3");
  EXPECT_EQ(id.uid(), uid);
}

TEST(PluginIdentityTest, EqualityConsidersAllFields) {
  const auto ident = make_identity();
  EXPECT_EQ(ident, ident);

  auto other = make_identity("Other Name", "VST3");
  EXPECT_NE(ident, other);

  other = make_identity("Test Plugin", "AU");
  EXPECT_NE(ident, other);

  other = make_identity("Test Plugin", "VST3", /*seed=*/1);
  EXPECT_NE(ident, other);
}

TEST(PluginIdentityTest, ThreeWayComparisonOrdersByNameThenFormatThenUid) {
  const auto a = PluginIdentity("Alpha", "VST3", {});
  const auto b = PluginIdentity("Beta", "VST3", {});
  EXPECT_LT(a, b);
  EXPECT_GT(b, a);

  const auto same_name_au = PluginIdentity("Alpha", "AU", {});
  EXPECT_LT(same_name_au, a);

  const auto c = PluginIdentity("Alpha", "VST3", {});
  EXPECT_EQ(a, c);
}

// --- PluginStateBlob -----------------------------------------------------

TEST(PluginStateBlobTest, DefaultConstructedIsEmpty) {
  const PluginStateBlob blob;
  EXPECT_TRUE(blob.empty());
  EXPECT_TRUE(blob.data().empty());
}

TEST(PluginStateBlobTest, ConstructedHoldsData) {
  const auto blob = make_blob(0, 4);
  EXPECT_FALSE(blob.empty());
  EXPECT_EQ(blob.data().size(), 4u);
  EXPECT_EQ(blob.data()[0], 0u);
  EXPECT_EQ(blob.data()[3], 3u);
}

TEST(PluginStateBlobTest, Equality) {
  const auto a = make_blob(0, 4);
  const auto b = make_blob(0, 4);
  EXPECT_EQ(a, b);

  const auto c = make_blob(1, 4);
  EXPECT_NE(a, c);

  const auto d = make_blob(0, 8);
  EXPECT_NE(a, d);
}

TEST(PluginStateBlobTest, EmptyBlobsAreEqual) {
  const PluginStateBlob a;
  const PluginStateBlob b;
  EXPECT_EQ(a, b);
}

// --- EffectSlot ----------------------------------------------------------

TEST(EffectSlotTest, ConstructedDefaultsBypassFalse) {
  const EffectSlot slot(make_identity("Reverb"), make_blob(10));
  EXPECT_FALSE(slot.bypassed());
  EXPECT_EQ(slot.identity().name(), "Reverb");
  EXPECT_EQ(slot.state().data().size(), 8u);
}

TEST(EffectSlotTest, ExplicitBypass) {
  const EffectSlot slot(make_identity("Delay"), make_blob(20), true);
  EXPECT_TRUE(slot.bypassed());
}

TEST(EffectSlotTest, SetBypassed) {
  EffectSlot slot(make_identity("EQ"), make_blob(30));
  EXPECT_FALSE(slot.bypassed());
  slot.set_bypassed(true);
  EXPECT_TRUE(slot.bypassed());
  slot.set_bypassed(false);
  EXPECT_FALSE(slot.bypassed());
}

TEST(EffectSlotTest, SetState) {
  EffectSlot slot(make_identity("Comp"), make_blob(40));
  EXPECT_EQ(slot.state().data().size(), 8u);

  const auto new_blob = make_blob(50, 16);
  slot.set_state(new_blob);
  EXPECT_EQ(slot.state(), new_blob);
}

TEST(EffectSlotTest, Equality) {
  const auto a = EffectSlot(make_identity("A"), make_blob(0));
  const auto b = EffectSlot(make_identity("A"), make_blob(0));
  EXPECT_EQ(a, b);

  const auto c = EffectSlot(make_identity("A"), make_blob(0), true);
  EXPECT_NE(a, c);

  const auto d = EffectSlot(make_identity("B"), make_blob(0));
  EXPECT_NE(a, d);
}

// --- TrackPluginChain (instrument) ---------------------------------------

TEST(TrackPluginChainTest, DefaultConstructedHasNoInstrument) {
  const TrackPluginChain chain;
  EXPECT_EQ(chain.instrument(), nullptr);
  EXPECT_EQ(chain.instrument_state(), nullptr);
  EXPECT_EQ(chain.effect_count(), 0u);
}

TEST(TrackPluginChainTest, SetAndGetInstrument) {
  TrackPluginChain chain;
  const auto       id = make_identity("Synth");
  const auto       st = make_blob(0, 32);

  chain.set_instrument(id, st);
  ASSERT_NE(chain.instrument(), nullptr);
  EXPECT_EQ(*chain.instrument(), id);
  ASSERT_NE(chain.instrument_state(), nullptr);
  EXPECT_EQ(*chain.instrument_state(), st);
}

TEST(TrackPluginChainTest, SetInstrumentReplaces) {
  TrackPluginChain chain;
  chain.set_instrument(make_identity("First"), make_blob(0));
  chain.set_instrument(make_identity("Second"), make_blob(100, 64));

  ASSERT_NE(chain.instrument(), nullptr);
  EXPECT_EQ(chain.instrument()->name(), "Second");
  ASSERT_NE(chain.instrument_state(), nullptr);
  EXPECT_EQ(chain.instrument_state()->data().size(), 64u);
}

TEST(TrackPluginChainTest, ClearInstrument) {
  TrackPluginChain chain;
  chain.set_instrument(make_identity("Synth"), make_blob(0));
  ASSERT_NE(chain.instrument(), nullptr);

  chain.clear_instrument();
  EXPECT_EQ(chain.instrument(), nullptr);
  EXPECT_EQ(chain.instrument_state(), nullptr);
}

TEST(TrackPluginChainTest, ClearInstrumentWhenEmptyIsNoop) {
  TrackPluginChain chain;
  chain.clear_instrument();
  EXPECT_EQ(chain.instrument(), nullptr);
}

// --- TrackPluginChain (effects) ------------------------------------------

TEST(TrackPluginChainTest, AddEffectAppends) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("EQ"), make_blob(0));
  chain.add_effect(make_identity("Comp"), make_blob(1));

  EXPECT_EQ(chain.effect_count(), 2u);
  EXPECT_EQ(chain.effect_at(0).identity().name(), "EQ");
  EXPECT_EQ(chain.effect_at(1).identity().name(), "Comp");
}

TEST(TrackPluginChainTest, AddEffectWithBypass) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("EQ"), make_blob(0), true);
  EXPECT_TRUE(chain.effect_at(0).bypassed());
}

TEST(TrackPluginChainTest, InsertEffectAtIndex) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("A"), make_blob(0));
  chain.add_effect(make_identity("C"), make_blob(2));
  chain.insert_effect(1, make_identity("B"), make_blob(1));

  EXPECT_EQ(chain.effect_count(), 3u);
  EXPECT_EQ(chain.effect_at(0).identity().name(), "A");
  EXPECT_EQ(chain.effect_at(1).identity().name(), "B");
  EXPECT_EQ(chain.effect_at(2).identity().name(), "C");
}

TEST(TrackPluginChainTest, InsertEffectAtEnd) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("A"), make_blob(0));
  chain.insert_effect(1, make_identity("B"), make_blob(1));

  EXPECT_EQ(chain.effect_count(), 2u);
  EXPECT_EQ(chain.effect_at(0).identity().name(), "A");
  EXPECT_EQ(chain.effect_at(1).identity().name(), "B");
}

TEST(TrackPluginChainTest, RemoveEffect) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("A"), make_blob(0));
  chain.add_effect(make_identity("B"), make_blob(1));
  chain.add_effect(make_identity("C"), make_blob(2));

  chain.remove_effect(1);
  EXPECT_EQ(chain.effect_count(), 2u);
  EXPECT_EQ(chain.effect_at(0).identity().name(), "A");
  EXPECT_EQ(chain.effect_at(1).identity().name(), "C");
}

TEST(TrackPluginChainTest, MoveEffectForward) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("A"), make_blob(0));
  chain.add_effect(make_identity("B"), make_blob(1));
  chain.add_effect(make_identity("C"), make_blob(2));

  chain.move_effect(0, 2);
  EXPECT_EQ(chain.effect_count(), 3u);
  EXPECT_EQ(chain.effect_at(0).identity().name(), "B");
  EXPECT_EQ(chain.effect_at(1).identity().name(), "C");
  EXPECT_EQ(chain.effect_at(2).identity().name(), "A");
}

TEST(TrackPluginChainTest, MoveEffectBackward) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("A"), make_blob(0));
  chain.add_effect(make_identity("B"), make_blob(1));
  chain.add_effect(make_identity("C"), make_blob(2));

  chain.move_effect(2, 0);
  EXPECT_EQ(chain.effect_at(0).identity().name(), "C");
  EXPECT_EQ(chain.effect_at(1).identity().name(), "A");
  EXPECT_EQ(chain.effect_at(2).identity().name(), "B");
}

TEST(TrackPluginChainTest, MoveEffectSameIndexIsNoop) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("A"), make_blob(0));
  chain.add_effect(make_identity("B"), make_blob(1));

  chain.move_effect(0, 0);
  EXPECT_EQ(chain.effect_count(), 2u);
  EXPECT_EQ(chain.effect_at(0).identity().name(), "A");
}

TEST(TrackPluginChainTest, SetEffectBypass) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("EQ"), make_blob(0));
  chain.set_effect_bypass(0, true);
  EXPECT_TRUE(chain.effect_at(0).bypassed());
}

TEST(TrackPluginChainTest, SetEffectState) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("EQ"), make_blob(0));
  const auto new_blob = make_blob(99, 64);
  chain.set_effect_state(0, new_blob);
  EXPECT_EQ(chain.effect_at(0).state(), new_blob);
}

TEST(TrackPluginChainTest, EffectsAccessorReturnsConstRef) {
  TrackPluginChain chain;
  chain.add_effect(make_identity("A"), make_blob(0));
  chain.add_effect(make_identity("B"), make_blob(1));

  const auto& fx = chain.effects();
  EXPECT_EQ(fx.size(), 2u);
  EXPECT_EQ(fx[0].identity().name(), "A");
}

TEST(TrackPluginChainTest, EqualityEmpty) {
  const TrackPluginChain a;
  const TrackPluginChain b;
  EXPECT_EQ(a, b);
}

TEST(TrackPluginChainTest, EqualityWithInstrument) {
  TrackPluginChain a;
  a.set_instrument(make_identity("Synth"), make_blob(0));

  TrackPluginChain b;
  b.set_instrument(make_identity("Synth"), make_blob(0));

  EXPECT_EQ(a, b);
}

TEST(TrackPluginChainTest, InequalityDifferentInstrument) {
  TrackPluginChain a;
  a.set_instrument(make_identity("Synth"), make_blob(0));

  TrackPluginChain b;
  b.set_instrument(make_identity("Sampler"), make_blob(0));

  EXPECT_NE(a, b);
}

TEST(TrackPluginChainTest, EqualityWithEffects) {
  TrackPluginChain a;
  a.add_effect(make_identity("EQ"), make_blob(0));
  a.add_effect(make_identity("Comp"), make_blob(1));

  TrackPluginChain b;
  b.add_effect(make_identity("EQ"), make_blob(0));
  b.add_effect(make_identity("Comp"), make_blob(1));

  EXPECT_EQ(a, b);
}

TEST(TrackPluginChainTest, InequalityEffectOrderChanged) {
  TrackPluginChain a;
  a.add_effect(make_identity("A"), make_blob(0));
  a.add_effect(make_identity("B"), make_blob(1));

  TrackPluginChain b;
  b.add_effect(make_identity("B"), make_blob(1));
  b.add_effect(make_identity("A"), make_blob(0));

  EXPECT_NE(a, b);
}

TEST(TrackPluginChainTest, ClearInstrumentLeavesEffectsIntact) {
  TrackPluginChain chain;
  chain.set_instrument(make_identity("Synth"), make_blob(0));
  chain.add_effect(make_identity("EQ"), make_blob(1));
  chain.add_effect(make_identity("Comp"), make_blob(2));

  chain.clear_instrument();
  EXPECT_EQ(chain.instrument(), nullptr);
  EXPECT_EQ(chain.effect_count(), 2u);
  EXPECT_EQ(chain.effect_at(0).identity().name(), "EQ");
  EXPECT_EQ(chain.effect_at(1).identity().name(), "Comp");
}

TEST(TrackPluginChainTest, CopyIsDeeplyIndependent) {
  TrackPluginChain chain;
  chain.set_instrument(make_identity("Synth"), make_blob(0, 64));
  chain.add_effect(make_identity("EQ"), make_blob(1));

  TrackPluginChain copy = chain;
  EXPECT_EQ(copy, chain);

  copy.set_instrument(make_identity("Sampler"), make_blob(99, 32));
  copy.set_effect_bypass(0, true);

  EXPECT_NE(copy, chain);
  ASSERT_NE(chain.instrument(), nullptr);
  EXPECT_EQ(chain.instrument()->name(), "Synth");
  EXPECT_EQ(chain.instrument_state()->data().size(), 64u);
  EXPECT_FALSE(chain.effect_at(0).bypassed());
}

// --- AuditionMixSettings -------------------------------------------------

TEST(AuditionMixSettingsTest, DefaultValues) {
  const AuditionMixSettings mix;
  EXPECT_FLOAT_EQ(mix.gain(), 0.8F);
  EXPECT_FLOAT_EQ(mix.pan(), 0.0F);
  EXPECT_FALSE(mix.mute());
  EXPECT_FALSE(mix.solo());
}

TEST(AuditionMixSettingsTest, SetGain) {
  AuditionMixSettings mix;
  mix.set_gain(0.5F);
  EXPECT_FLOAT_EQ(mix.gain(), 0.5F);
}

TEST(AuditionMixSettingsTest, SetPan) {
  AuditionMixSettings mix;
  mix.set_pan(-0.5F);
  EXPECT_FLOAT_EQ(mix.pan(), -0.5F);
  mix.set_pan(1.0F);
  EXPECT_FLOAT_EQ(mix.pan(), 1.0F);
}

TEST(AuditionMixSettingsTest, SetMute) {
  AuditionMixSettings mix;
  mix.set_mute(true);
  EXPECT_TRUE(mix.mute());
  mix.set_mute(false);
  EXPECT_FALSE(mix.mute());
}

TEST(AuditionMixSettingsTest, SetSolo) {
  AuditionMixSettings mix;
  mix.set_solo(true);
  EXPECT_TRUE(mix.solo());
  mix.set_solo(false);
  EXPECT_FALSE(mix.solo());
}

TEST(AuditionMixSettingsTest, Equality) {
  AuditionMixSettings a;
  AuditionMixSettings b;
  EXPECT_EQ(a, b);

  a.set_gain(0.2F);
  EXPECT_NE(a, b);

  b.set_gain(0.2F);
  EXPECT_EQ(a, b);

  a.set_mute(true);
  EXPECT_NE(a, b);
}

// --- Integration: Track holds plugin chain and mix settings --------------

TEST(WriterAuditionIntegrationTest, NewTrackHasEmptyPluginChain) {
  auto       project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *graphscore::MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  const auto* track = project.find_active_track(*track_id);
  ASSERT_NE(track, nullptr);
  EXPECT_EQ(track->plugin_chain().instrument(), nullptr);
  EXPECT_EQ(track->plugin_chain().effect_count(), 0u);
}

TEST(WriterAuditionIntegrationTest, NewTrackHasDefaultMixSettings) {
  auto       project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *graphscore::MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  const auto* track = project.find_active_track(*track_id);
  ASSERT_NE(track, nullptr);
  EXPECT_FLOAT_EQ(track->mix_settings().gain(), 0.8F);
  EXPECT_FLOAT_EQ(track->mix_settings().pan(), 0.0F);
  EXPECT_FALSE(track->mix_settings().mute());
  EXPECT_FALSE(track->mix_settings().solo());
}

TEST(WriterAuditionIntegrationTest, MutatePluginChainThroughTrack) {
  auto       project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *graphscore::MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  ASSERT_NE(track, nullptr);
  track->plugin_chain().set_instrument(make_identity("Synth", "VST3"),
                                       make_blob(0, 64));

  ASSERT_NE(track->plugin_chain().instrument(), nullptr);
  EXPECT_EQ(track->plugin_chain().instrument()->name(), "Synth");
  EXPECT_EQ(track->plugin_chain().instrument_state()->data().size(), 64u);
}

TEST(WriterAuditionIntegrationTest, MutateMixSettingsThroughTrack) {
  auto       project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *graphscore::MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  ASSERT_NE(track, nullptr);
  track->mix_settings().set_gain(0.3F);
  track->mix_settings().set_pan(1.0F);
  track->mix_settings().set_mute(true);

  EXPECT_FLOAT_EQ(track->mix_settings().gain(), 0.3F);
  EXPECT_FLOAT_EQ(track->mix_settings().pan(), 1.0F);
  EXPECT_TRUE(track->mix_settings().mute());
}

TEST(WriterAuditionIntegrationTest, ArchivedTrackPreservesPluginChain) {
  auto       project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *graphscore::MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  track->plugin_chain().set_instrument(make_identity("Synth"), make_blob(0));
  track->plugin_chain().add_effect(make_identity("EQ"), make_blob(1));
  track->mix_settings().set_gain(0.5F);

  const auto result = project.archive_track(*track_id);
  ASSERT_TRUE(result);

  const auto* archived = project.find_archived_track(*track_id);
  ASSERT_NE(archived, nullptr);
  ASSERT_NE(archived->plugin_chain().instrument(), nullptr);
  EXPECT_EQ(archived->plugin_chain().instrument()->name(), "Synth");
  EXPECT_EQ(archived->plugin_chain().effect_count(), 1u);
  EXPECT_FLOAT_EQ(archived->mix_settings().gain(), 0.5F);
}

TEST(WriterAuditionIntegrationTest, PluginChainSurvivesArchiveRestoreCycle) {
  auto       project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *graphscore::MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  track->plugin_chain().set_instrument(make_identity("Synth"), make_blob(0));
  track->plugin_chain().add_effect(make_identity("EQ"), make_blob(1));

  const auto arch = project.archive_track(*track_id);
  ASSERT_TRUE(arch);
  const auto rest = project.restore_track(*track_id);
  ASSERT_TRUE(rest);

  const auto* restored = project.find_active_track(*track_id);
  ASSERT_NE(restored, nullptr);
  ASSERT_NE(restored->plugin_chain().instrument(), nullptr);
  EXPECT_EQ(restored->plugin_chain().instrument()->name(), "Synth");
  EXPECT_EQ(restored->plugin_chain().effect_count(), 1u);
  EXPECT_EQ(restored->plugin_chain().effect_at(0).identity().name(), "EQ");
}

TEST(WriterAuditionIntegrationTest, MultipleTracksHaveIndependentChains) {
  auto       project = make_project();
  const auto a       = project.add_track("A", StaffLayout::single_staff(),
                                         *graphscore::MidiChannel::create(0));
  const auto b       = project.add_track("B", StaffLayout::single_staff(),
                                         *graphscore::MidiChannel::create(1));
  ASSERT_TRUE(a.has_value());
  ASSERT_TRUE(b.has_value());

  auto* track_a = project.find_active_track(*a);
  auto* track_b = project.find_active_track(*b);

  track_a->plugin_chain().set_instrument(make_identity("SynthA"), make_blob(0));
  track_b->plugin_chain().set_instrument(make_identity("SynthB"), make_blob(1));

  ASSERT_NE(track_a->plugin_chain().instrument(), nullptr);
  ASSERT_NE(track_b->plugin_chain().instrument(), nullptr);
  EXPECT_EQ(track_a->plugin_chain().instrument()->name(), "SynthA");
  EXPECT_EQ(track_b->plugin_chain().instrument()->name(), "SynthB");
}

TEST(WriterAuditionIntegrationTest, NonConstFindActiveTrackReturnsMutable) {
  auto       project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *graphscore::MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  ASSERT_NE(track, nullptr);
  track->mix_settings().set_solo(true);
  EXPECT_TRUE(project.find_active_track(*track_id)->mix_settings().solo());
}

TEST(WriterAuditionIntegrationTest, FindActiveTrackUnknownReturnsNull) {
  auto project = make_project();
  EXPECT_EQ(project.find_active_track(graphscore::TrackId::generate()),
            nullptr);
}
