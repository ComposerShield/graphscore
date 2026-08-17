// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <graphscore/accessibility/graphscore_accessibility.hpp>

namespace graphscore {
namespace {

struct Fixture {
  Project          project{ProjectId::generate(), "Project"};
  TrackId          track_id;
  StaveId          stave_id;
  NodeId           node_id;
  NotationEntityId note_id;
  NotationEntityId rest_id;
  NotationEntityId dynamic_id;
  NotationLayout   layout;

  explicit Fixture(bool duplicate_articulation = false) {
    track_id   = *project.add_track("Piano", StaffLayout::single_staff(),
                                    *MidiChannel::create(1));
    stave_id   = project.active_tracks().front().layout().staves().front().id;
    node_id    = project.add_node("Verse");
    Node* node = project.find_node(node_id);
    node->set_timeline(*NodeTimeline::create(
        {Measure{*TimeSignature::create(4, 4), KeySignature{}}},
        project.active_tracks().front().layout().staves()));
    TrackLane* lane = node->lane(track_id);
    lane->ensure_stave(stave_id);
    VoiceContent& voice = lane->stave(stave_id)->voice(*Voice::create(1));
    Note          note =
        make_note(*SpelledPitch::create(Letter::kC, 4, Accidental::kNatural),
                  *Duration::create(NoteValue::kQuarter, 0));
    if (duplicate_articulation) {
      note.articulations = {Articulation::kAccent, Articulation::kAccent};
    }
    note_id = note.id;
    EXPECT_TRUE(voice.append(note));
    Rest rest = make_rest(*Duration::create(NoteValue::kHalf, 1));
    rest_id   = rest.id;
    EXPECT_TRUE(voice.append(rest));
    DynamicMarking dynamic = make_dynamic_marking(note_id, Dynamic::kMf);
    dynamic_id             = dynamic.id;
    EXPECT_TRUE(voice.add_dynamic(dynamic));

    layout.node_id = node_id;
    layout.bounds  = {0.0, 0.0, 400.0, 200.0};
    SystemLayout system;
    system.first_measure = 0;
    system.id            = NotationId{node_id.to_string() + "/system/0"};
    system.bounds        = layout.bounds;
    system.measures.push_back(
        {0, NotationId{node_id.to_string() + "/measure/0"}, layout.bounds});
    StaffSystemLayout staff;
    staff.track_id = track_id;
    staff.stave_id = stave_id;
    staff.id       = NotationId{stave_id.to_string() + "/system/0"};
    staff.bounds   = {10.0, 20.0, 380.0, 80.0};
    staff.voices.push_back(
        {*Voice::create(1), NotationId{stave_id.to_string() + "/voice/1"}, 2});
    system.staves.push_back(staff);
    layout.systems.push_back(system);
    layout.hit_regions = {
        {NotationId{"measure-hit"}, layout.systems[0].measures[0].id,
         HitRole::kMeasure, layout.bounds, 1},
        {NotationId{"staff-hit"}, NotationId{stave_id.to_string()},
         HitRole::kStaff, staff.bounds, 2},
        {NotationId{"voice-hit"}, NotationId{stave_id.to_string() + "/voice/1"},
         HitRole::kVoice, staff.bounds, 3},
        {NotationId{"note-glyph-hit"},
         NotationId{note_id.to_string()},
         HitRole::kNotehead,
         {50.0, 40.0, 12.0, 8.0},
         8},
        {NotationId{"rest-glyph-hit"},
         NotationId{rest_id.to_string()},
         HitRole::kEvent,
         {100.0, 40.0, 12.0, 8.0},
         6},
        {NotationId{"dynamic-glyph-hit"},
         NotationId{dynamic_id.to_string()},
         HitRole::kMarking,
         {50.0, 80.0, 18.0, 8.0},
         6},
    };
    if (duplicate_articulation) {
      layout.hit_regions.push_back(
          {NotationId{note_id.to_string() + "/articulation/0/glyph/hit"},
           NotationId{note_id.to_string()},
           HitRole::kMarking,
           {60.0, 20.0, 8.0, 8.0},
           6});
      layout.hit_regions.push_back(
          {NotationId{note_id.to_string() + "/articulation/1/glyph/hit"},
           NotationId{note_id.to_string()},
           HitRole::kMarking,
           {80.0, 20.0, 8.0, 8.0},
           6});
    }
    layout.commands.emplace_back(GlyphCommand{
        NotationId{"purely-visual-glyph"}, U'X', {1.0, 2.0}, 10.0});
  }
};

[[nodiscard]] std::size_t count_role(const AccessibilityTree& tree,
                                     AccessibilityRole        role) {
  return static_cast<std::size_t>(
      std::ranges::count(tree.nodes(), role, &AccessibilityNode::role));
}

TEST(AccessibilityTreeTest, ExposesMusicalHierarchyWithoutGlyphPrimitives) {
  Fixture    fixture;
  const auto selection =
      NoteheadSet::create({{fixture.node_id, fixture.track_id, fixture.stave_id,
                            *Voice::create(1), fixture.note_id}});
  ASSERT_TRUE(selection.has_value());
  const Selection selected = *selection;

  const AccessibilityBuildResult result = build_notation_accessibility_tree(
      fixture.project, fixture.node_id, fixture.layout, NotePaletteState(),
      &selected);

  ASSERT_TRUE(result);
  const AccessibilityTree& tree = *result.tree;
  ASSERT_TRUE(tree.root().has_value());
  EXPECT_EQ(tree.nodes()[*tree.root()].role, AccessibilityRole::kNode);
  EXPECT_EQ(tree.nodes()[*tree.root()].name, "Verse");
  EXPECT_EQ(count_role(tree, AccessibilityRole::kTrack), 1U);
  EXPECT_EQ(count_role(tree, AccessibilityRole::kStaff), 1U);
  EXPECT_EQ(count_role(tree, AccessibilityRole::kMeasure), 1U);
  EXPECT_EQ(count_role(tree, AccessibilityRole::kVoice), 4U);
  EXPECT_EQ(count_role(tree, AccessibilityRole::kNote), 1U);
  EXPECT_EQ(count_role(tree, AccessibilityRole::kRest), 1U);
  EXPECT_EQ(count_role(tree, AccessibilityRole::kMarking), 1U);
  EXPECT_EQ(count_role(tree, AccessibilityRole::kPalette), 1U);
  EXPECT_EQ(count_role(tree, AccessibilityRole::kSelection), 1U);
  EXPECT_EQ(std::ranges::count(tree.nodes(), std::string{"purely-visual-glyph"},
                               &AccessibilityNode::id),
            0);

  const auto selected_note =
      std::ranges::find_if(tree.nodes(), [](const AccessibilityNode& node) {
        return node.role == AccessibilityRole::kNote &&
               has_state(node.states, AccessibilityState::kSelected);
      });
  ASSERT_NE(selected_note, tree.nodes().end());
  const auto selection_node = std::ranges::find(
      tree.nodes(), AccessibilityRole::kSelection, &AccessibilityNode::role);
  ASSERT_NE(selection_node, tree.nodes().end());
  EXPECT_EQ(selection_node->related_ids,
            std::vector<std::string>{selected_note->id});

  std::set<std::string> ids;
  for (std::size_t index = 0; index < tree.nodes().size(); ++index) {
    const AccessibilityNode& node = tree.nodes()[index];
    EXPECT_TRUE(ids.insert(node.id).second);
    for (const std::size_t child : node.children) {
      ASSERT_LT(child, tree.nodes().size());
      EXPECT_EQ(tree.nodes()[child].parent, index);
    }
  }
  EXPECT_FALSE(tree.nodes()[*tree.root()].parent.has_value());
  EXPECT_TRUE(selected_note->bounds.has_value());
}

TEST(AccessibilityTreeTest, KeepsStableSemanticIdsAcrossGlyphChanges) {
  Fixture                        fixture;
  const AccessibilityBuildResult first = build_notation_accessibility_tree(
      fixture.project, fixture.node_id, fixture.layout, NotePaletteState());
  ASSERT_TRUE(first);
  fixture.layout.commands.clear();
  fixture.layout.commands.emplace_back(GlyphCommand{
      NotationId{"replacement-glyph"}, U'Y', {200.0, 100.0}, 30.0});
  const AccessibilityBuildResult second = build_notation_accessibility_tree(
      fixture.project, fixture.node_id, fixture.layout, NotePaletteState());
  ASSERT_TRUE(second);

  std::vector<std::string> first_ids;
  std::vector<std::string> second_ids;
  std::ranges::transform(first.tree->nodes(), std::back_inserter(first_ids),
                         &AccessibilityNode::id);
  std::ranges::transform(second.tree->nodes(), std::back_inserter(second_ids),
                         &AccessibilityNode::id);
  EXPECT_EQ(first_ids, second_ids);
}

TEST(AccessibilityTreeTest, RejectsMismatchedLayoutWithoutPartialTree) {
  Fixture fixture;
  fixture.layout.node_id = NodeId::generate();

  const AccessibilityBuildResult result = build_notation_accessibility_tree(
      fixture.project, fixture.node_id, fixture.layout, NotePaletteState());

  EXPECT_FALSE(result);
  EXPECT_EQ(result.error, AccessibilityBuildError::kLayoutNodeMismatch);
  EXPECT_FALSE(result.tree.has_value());
}

TEST(AccessibilityTreeTest, RejectsAStaleSelectionWithoutDanglingRelations) {
  Fixture    fixture;
  const auto selection =
      NoteheadSet::create({{fixture.node_id, fixture.track_id, fixture.stave_id,
                            *Voice::create(1), NotationEntityId::generate()}});
  ASSERT_TRUE(selection.has_value());
  const Selection selected = *selection;

  const AccessibilityBuildResult result = build_notation_accessibility_tree(
      fixture.project, fixture.node_id, fixture.layout, NotePaletteState(),
      &selected);

  EXPECT_FALSE(result);
  EXPECT_EQ(result.error, AccessibilityBuildError::kSelectionInvalid);
  EXPECT_FALSE(result.tree.has_value());
}

TEST(AccessibilityTreeTest, PreservesStaffScopeForMeasureSelection) {
  Fixture    fixture;
  const auto selection = FullMeasureSet::create(
      {{fixture.node_id, fixture.track_id, fixture.stave_id, 0, 1}});
  ASSERT_TRUE(selection.has_value());
  const Selection selected = *selection;

  const AccessibilityBuildResult result = build_notation_accessibility_tree(
      fixture.project, fixture.node_id, fixture.layout, NotePaletteState(),
      &selected);

  ASSERT_TRUE(result);
  const auto selection_node =
      std::ranges::find(result.tree->nodes(), AccessibilityRole::kSelection,
                        &AccessibilityNode::role);
  ASSERT_NE(selection_node, result.tree->nodes().end());
  ASSERT_EQ(selection_node->related_ids.size(), 2U);
  const AccessibilityNode* staff =
      result.tree->find(selection_node->related_ids[0]);
  const AccessibilityNode* measure =
      result.tree->find(selection_node->related_ids[1]);
  ASSERT_NE(staff, nullptr);
  ASSERT_NE(measure, nullptr);
  EXPECT_EQ(staff->role, AccessibilityRole::kStaff);
  EXPECT_EQ(measure->role, AccessibilityRole::kMeasure);
  EXPECT_TRUE(has_state(staff->states, AccessibilityState::kSelected));
  EXPECT_TRUE(has_state(measure->states, AccessibilityState::kSelected));
}

TEST(AccessibilityTreeTest, RejectsDuplicateSemanticIdsAtomically) {
  Fixture fixture(/*duplicate_articulation=*/true);

  const AccessibilityBuildResult result = build_notation_accessibility_tree(
      fixture.project, fixture.node_id, fixture.layout, NotePaletteState());

  EXPECT_FALSE(result);
  EXPECT_EQ(result.error, AccessibilityBuildError::kDuplicateSemanticId);
  EXPECT_FALSE(result.tree.has_value());
}

}  // namespace
}  // namespace graphscore
