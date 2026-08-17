// SPDX-License-Identifier: Apache-2.0

#include <graphscore/accessibility/graphscore_accessibility.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {
namespace {

[[nodiscard]] std::string append_path(const std::string& parent,
                                      const std::string& component) {
  return parent + "/" + component;
}

[[nodiscard]] std::string node_path(NodeId node) {
  return "node/" + node.to_string();
}

[[nodiscard]] std::string track_path(NodeId node, TrackId track) {
  return append_path(node_path(node), "track/" + track.to_string());
}

[[nodiscard]] std::string staff_path(NodeId node, TrackId track,
                                     StaveId stave) {
  return append_path(track_path(node, track), "staff/" + stave.to_string());
}

[[nodiscard]] std::string voice_path(NodeId node, TrackId track, StaveId stave,
                                     Voice voice) {
  return append_path(staff_path(node, track, stave),
                     "voice/" + std::to_string(voice.index()));
}

[[nodiscard]] std::string entity_path(const std::string& voice,
                                      const std::string& kind,
                                      NotationEntityId   id) {
  return append_path(voice, kind + "/" + id.to_string());
}

[[nodiscard]] std::string measure_path(NodeId node, std::size_t ordinal) {
  return append_path(node_path(node), "measure/" + std::to_string(ordinal));
}

[[nodiscard]] std::string pitch_name(const SpelledPitch& pitch) {
  constexpr const char* kLetters[]     = {"A", "B", "C", "D", "E", "F", "G"};
  constexpr const char* kAccidentals[] = {"double flat", "flat", "natural",
                                          "sharp", "double sharp"};
  const std::size_t     accidental_index =
      static_cast<std::size_t>(to_semitone_offset(pitch.accidental()) + 2);
  return std::string(kLetters[static_cast<std::uint8_t>(pitch.letter())]) +
         " " + kAccidentals[accidental_index] + " " +
         std::to_string(pitch.octave());
}

[[nodiscard]] std::string duration_name(const Duration& duration) {
  constexpr const char* kNoteValues[] = {
      "whole note",        "half note",      "quarter note",
      "eighth note",       "sixteenth note", "thirty-second note",
      "sixty-fourth note",
  };
  std::string result;
  if (duration.dots() == 1U)
    result = "dotted ";
  else if (duration.dots() == 2U)
    result = "double-dotted ";
  result += kNoteValues[static_cast<std::uint8_t>(duration.base())];
  if (duration.tuplet().has_value()) {
    result += ", " + std::to_string(duration.tuplet()->played()) + ":" +
              std::to_string(duration.tuplet()->normal()) + " tuplet";
  }
  return result;
}

[[nodiscard]] std::string rational_ordinal(Rational value) {
  const std::int64_t whole     = value.numerator() / value.denominator();
  const std::int64_t remainder = value.numerator() % value.denominator();
  if (remainder == 0)
    return std::to_string(whole);
  if (whole == 0) {
    return std::to_string(remainder) + "/" +
           std::to_string(value.denominator());
  }
  return std::to_string(whole) + " " + std::to_string(remainder) + "/" +
         std::to_string(value.denominator());
}

[[nodiscard]] std::string position_name(const NodeTimeline& timeline,
                                        Rational            position) {
  const MeasureMap& measures      = timeline.measures();
  std::size_t       measure_index = measures.measure_count();
  Rational          measure_start = timeline.boundary_position();
  TimeSignature     meter =
      measures.measure(measures.measure_count() - 1U).time_signature;
  if (const auto main_measure = measures.measure_index_at(position)) {
    measure_index = *main_measure;
    measure_start = measures.measure_start(measure_index);
    meter         = measures.measure(measure_index).time_signature;
  }
  const Rational beat =
      (position - measure_start) * Rational(meter.denominator()) + Rational(1);
  return "bar " + std::to_string(measure_index + 1U) + ", beat " +
         rational_ordinal(beat);
}

[[nodiscard]] std::string event_value(const std::optional<SpelledPitch>& pitch,
                                      const Duration& duration, Voice voice,
                                      Rational            position,
                                      const NodeTimeline& timeline) {
  std::string result;
  if (pitch.has_value()) {
    const auto sounding = pitch->to_midi_pitch();
    result              = sounding.has_value()
                              ? "Sounding pitch " +
                       std::to_string(static_cast<unsigned>(sounding->value()))
                              : "Sounding pitch unavailable";
    result += "; ";
  }
  result += duration_name(duration) + "; voice " +
            std::to_string(voice.index()) + "; " +
            position_name(timeline, position);
  return result;
}

[[nodiscard]] std::optional<NotationRect> union_rects(
    const std::optional<NotationRect>& left,
    const std::optional<NotationRect>& right) {
  if (!left.has_value())
    return right;
  if (!right.has_value())
    return left;
  const double min_x = std::min(left->x, right->x);
  const double min_y = std::min(left->y, right->y);
  const double max_x = std::max(left->x + left->width, right->x + right->width);
  const double max_y =
      std::max(left->y + left->height, right->y + right->height);
  return NotationRect{min_x, min_y, max_x - min_x, max_y - min_y};
}

template <typename Predicate>
[[nodiscard]] std::optional<NotationRect> semantic_bounds(
    const NotationLayout& layout, const std::string& semantic_id,
    Predicate include) {
  std::optional<NotationRect> result;
  for (const HitRegion& hit : layout.hit_regions) {
    if (hit.semantic_id.value == semantic_id && include(hit.role))
      result = union_rects(result, hit.bounds);
  }
  return result;
}

[[nodiscard]] std::optional<NotationRect> container_bounds(
    const NotationLayout& layout, const std::string& semantic_id,
    HitRole role) {
  return semantic_bounds(layout, semantic_id, [role](HitRole candidate) {
    return candidate == role;
  });
}

[[nodiscard]] std::optional<NotationRect> event_bounds(
    const NotationLayout& layout, NotationEntityId id) {
  return semantic_bounds(layout, id.to_string(), [](HitRole role) {
    return role == HitRole::kEvent || role == HitRole::kNotehead;
  });
}

[[nodiscard]] std::optional<NotationRect> marking_bounds(
    const NotationLayout& layout, NotationEntityId id) {
  return semantic_bounds(layout, id.to_string(), [](HitRole role) {
    return role == HitRole::kMarking;
  });
}

[[nodiscard]] std::optional<NotationRect> embedded_marking_bounds(
    const NotationLayout& layout, NotationEntityId id,
    const std::string& id_component) {
  std::optional<NotationRect> result;
  for (const HitRegion& hit : layout.hit_regions) {
    if (hit.semantic_id.value == id.to_string() &&
        hit.role == HitRole::kMarking &&
        hit.id.value.find(id_component) != std::string::npos) {
      result = union_rects(result, hit.bounds);
    }
  }
  return result;
}

[[nodiscard]] AccessibilityState layout_state(
    const std::optional<NotationRect>& bounds) noexcept {
  return bounds.has_value() ? AccessibilityState::kNone
                            : AccessibilityState::kOffscreen;
}

class TreeBuilder {
 public:
  std::size_t add(std::string id, AccessibilityRole role, std::string name,
                  std::optional<NotationRect> bounds,
                  std::optional<std::size_t>  parent,
                  AccessibilityState states = AccessibilityState::kNone) {
    if (!ids_.insert(id).second)
      duplicate_id_ = true;
    const std::size_t index = nodes_.size();
    nodes_.push_back(AccessibilityNode{std::move(id),
                                       role,
                                       std::move(name),
                                       {},
                                       bounds,
                                       states,
                                       {},
                                       parent,
                                       {},
                                       {}});
    if (parent.has_value())
      nodes_[*parent].children.push_back(index);
    return index;
  }

  void select_related(
      const std::vector<std::string>&            related,
      std::span<const AccessibilityNode::Action> available_actions) {
    for (AccessibilityNode& node : nodes_) {
      if (std::ranges::find(related, node.id) != related.end()) {
        node.states = node.states | AccessibilityState::kSelected;
        node.actions.assign(available_actions.begin(), available_actions.end());
      }
    }
  }

  [[nodiscard]] std::optional<std::size_t> focus(
      std::string_view requested, std::span<const std::string> saved_ancestors,
      std::size_t root) {
    if (requested.empty())
      return std::nullopt;
    const auto mark_focused = [&](const std::string& id) {
      const auto found = std::ranges::find(nodes_, id, &AccessibilityNode::id);
      if (found == nodes_.end())
        return std::optional<std::size_t>{};
      found->states = found->states | AccessibilityState::kFocused;
      return std::optional<std::size_t>{
          static_cast<std::size_t>(found - nodes_.begin())};
    };
    std::string candidate(requested);
    if (const auto exact = mark_focused(candidate))
      return exact;
    for (const std::string& ancestor : saved_ancestors) {
      if (const auto fallback = mark_focused(ancestor))
        return fallback;
    }
    while (!candidate.empty()) {
      const std::size_t separator = candidate.rfind('/');
      if (separator == std::string::npos)
        break;
      candidate.resize(separator);
      if (const auto fallback = mark_focused(candidate))
        return fallback;
    }
    nodes_[root].states = nodes_[root].states | AccessibilityState::kFocused;
    return root;
  }

  void set_value(std::size_t node, std::string value) {
    nodes_[node].value = std::move(value);
  }

  void set_actions(
      std::size_t                                node,
      std::span<const AccessibilityNode::Action> available_actions) {
    nodes_[node].actions.assign(available_actions.begin(),
                                available_actions.end());
  }

  void set_related(std::size_t node, std::vector<std::string> related) {
    nodes_[node].related_ids = std::move(related);
  }

  [[nodiscard]] bool contains(const std::string& id) const {
    return ids_.contains(id);
  }

  [[nodiscard]] bool has_duplicate_id() const noexcept { return duplicate_id_; }

  [[nodiscard]] std::vector<AccessibilityNode> take_nodes() && {
    return std::move(nodes_);
  }

 private:
  std::vector<AccessibilityNode>  nodes_;
  std::unordered_set<std::string> ids_;
  bool                            duplicate_id_ = false;
};

[[nodiscard]] std::string marking_path(const std::string& parent,
                                       const std::string& kind,
                                       NotationEntityId   id) {
  return append_path(parent, "marking/" + kind + "/" + id.to_string());
}

void add_record_markings(TreeBuilder& builder, const NotationLayout& layout,
                         const VoiceContent& content,
                         const std::string& voice_id, std::size_t voice_node) {
  const auto add_records = [&](const auto& records, const std::string& kind,
                               const std::string& name) {
    for (const auto& record : records) {
      const auto bounds = marking_bounds(layout, record.id);
      builder.add(marking_path(voice_id, kind, record.id),
                  AccessibilityRole::kMarking, name, bounds, voice_node,
                  bounds.has_value() ? AccessibilityState::kNone
                                     : AccessibilityState::kOffscreen);
    }
  };
  add_records(content.dynamics(), "dynamic", "Dynamic");
  add_records(content.hairpins(), "hairpin", "Hairpin");
  add_records(content.slurs(), "slur", "Slur");
}

void add_event_semantics(TreeBuilder& builder, const NotationLayout& layout,
                         const VoiceContent& content,
                         const std::string& voice_id, std::size_t voice_node,
                         Voice voice, const NodeTimeline& timeline) {
  std::set<std::string> emitted_tuplets;
  Rational              position;
  for (const VoiceEvent& event : content.events()) {
    const NotationEntityId event_entity       = event_id(event);
    const auto             add_event_markings = [&](const auto& sounding) {
      for (std::size_t index = 0; index < sounding.articulations.size();
           ++index) {
        const Articulation articulation = sounding.articulations[index];
        const std::string  id = append_path(
            marking_path(voice_id, "articulation", event_entity),
            std::to_string(static_cast<std::uint8_t>(articulation)));
        const auto bounds = embedded_marking_bounds(
            layout, event_entity,
            "/articulation/" + std::to_string(index) + "/");
        const AccessibilityState state = layout_state(bounds);
        builder.add(id, AccessibilityRole::kMarking, "Articulation", bounds,
                                voice_node, state);
      }
    };

    std::visit(
        [&](const auto& concrete) {
          using Event = std::decay_t<decltype(concrete)>;
          if constexpr (std::is_same_v<Event, Note>) {
            const auto        bounds    = event_bounds(layout, concrete.id);
            const std::size_t note_node = builder.add(
                entity_path(voice_id, "note", concrete.id),
                AccessibilityRole::kNote, pitch_name(concrete.pitch), bounds,
                voice_node,
                bounds.has_value() ? AccessibilityState::kNone
                                   : AccessibilityState::kOffscreen);
            builder.set_value(
                note_node, event_value(concrete.pitch, concrete.duration, voice,
                                       position, timeline));
            if (concrete.tied_to_next) {
              const auto tie_bounds =
                  embedded_marking_bounds(layout, concrete.id, "/tie/");
              builder.add(
                  marking_path(voice_id, "tie", concrete.id),
                  AccessibilityRole::kMarking, "Tie", tie_bounds, voice_node,
                  tie_bounds.has_value() ? AccessibilityState::kNone
                                         : AccessibilityState::kOffscreen);
            }
            add_event_markings(concrete);
          } else if constexpr (std::is_same_v<Event, Chord>) {
            const auto        chord_bounds = event_bounds(layout, concrete.id);
            const std::size_t chord_node   = builder.add(
                entity_path(voice_id, "chord", concrete.id),
                AccessibilityRole::kChord, "Chord", chord_bounds, voice_node,
                chord_bounds.has_value() ? AccessibilityState::kNone
                                           : AccessibilityState::kOffscreen);
            builder.set_value(
                chord_node, event_value(std::nullopt, concrete.duration, voice,
                                        position, timeline));
            for (const ChordNote& note : concrete.notes) {
              const auto        bounds    = event_bounds(layout, note.id);
              const std::size_t note_node = builder.add(
                  entity_path(voice_id, "note", note.id),
                  AccessibilityRole::kNote, pitch_name(note.pitch), bounds,
                  chord_node,
                  bounds.has_value() ? AccessibilityState::kNone
                                     : AccessibilityState::kOffscreen);
              builder.set_value(
                  note_node, event_value(note.pitch, concrete.duration, voice,
                                         position, timeline));
              if (note.tied_to_next) {
                const auto tie_bounds =
                    embedded_marking_bounds(layout, note.id, "/tie/");
                builder.add(
                    marking_path(voice_id, "tie", note.id),
                    AccessibilityRole::kMarking, "Tie", tie_bounds, voice_node,
                    tie_bounds.has_value() ? AccessibilityState::kNone
                                           : AccessibilityState::kOffscreen);
              }
            }
            add_event_markings(concrete);
          } else {
            const auto        bounds    = event_bounds(layout, concrete.id);
            const std::size_t rest_node = builder.add(
                entity_path(voice_id, "rest", concrete.id),
                AccessibilityRole::kRest, "Rest", bounds, voice_node,
                bounds.has_value() ? AccessibilityState::kNone
                                   : AccessibilityState::kOffscreen);
            builder.set_value(
                rest_node, event_value(std::nullopt, concrete.duration, voice,
                                       position, timeline));
          }
        },
        event);

    const auto& tuplet = event_tuplet_group(event);
    if (tuplet.has_value() &&
        emitted_tuplets.insert(tuplet->to_string()).second) {
      std::optional<NotationRect> bounds;
      for (const VoiceEvent& member : content.events()) {
        if (event_tuplet_group(member) == tuplet) {
          bounds = union_rects(
              bounds,
              embedded_marking_bounds(layout, event_id(member), "/tuplet/"));
        }
      }
      builder.add(marking_path(voice_id, "tuplet", event_entity),
                  AccessibilityRole::kMarking, "Tuplet", bounds, voice_node,
                  bounds.has_value() ? AccessibilityState::kNone
                                     : AccessibilityState::kOffscreen);
    }
    position = position + event_duration(event).resolved();
  }

  for (const GraceGroup& group : content.grace_groups()) {
    for (const GraceNote& note : group.notes) {
      const auto        bounds    = event_bounds(layout, note.id);
      const std::size_t note_node = builder.add(
          entity_path(voice_id, "note", note.id), AccessibilityRole::kNote,
          "Grace " + pitch_name(note.pitch), bounds, voice_node,
          bounds.has_value() ? AccessibilityState::kNone
                             : AccessibilityState::kOffscreen);
      const auto grace_position = content.position_of_event(note.id);
      if (grace_position.has_value()) {
        builder.set_value(note_node,
                          event_value(note.pitch, note.duration, voice,
                                      *grace_position, timeline));
      }
    }
  }
  add_record_markings(builder, layout, content, voice_id, voice_node);
}

[[nodiscard]] std::string selected_marking_path(const MarkingItem& item) {
  const std::string parent =
      item.voice.has_value()
          ? voice_path(item.node, item.track, item.stave, *item.voice)
          : staff_path(item.node, item.track, item.stave);
  switch (item.kind) {
    case MarkingKind::kDynamic:
      return marking_path(parent, "dynamic", item.anchor);
    case MarkingKind::kHairpin:
      return marking_path(parent, "hairpin", item.anchor);
    case MarkingKind::kSlur:
      return marking_path(parent, "slur", item.anchor);
    case MarkingKind::kPedalSpan:
      return marking_path(parent, "pedal", item.anchor);
    case MarkingKind::kArticulation:
      return append_path(
          marking_path(parent, "articulation", item.anchor),
          std::to_string(static_cast<std::uint8_t>(*item.articulation)));
    case MarkingKind::kTie:
      return marking_path(parent, "tie", item.anchor);
    case MarkingKind::kTuplet:
      return marking_path(parent, "tuplet", item.anchor);
  }
  return {};
}

[[nodiscard]] std::vector<std::string> selection_relations(
    const Selection* selection) {
  if (selection == nullptr)
    return {};
  return std::visit(
      [](const auto& selected) {
        using Set                 = std::decay_t<decltype(selected)>;
        constexpr bool kRangeLike = std::is_same_v<Set, ArbitraryRangeSet> ||
                                    std::is_same_v<Set, InsertionCaretSet>;
        std::vector<std::string> result;
        result.reserve(selected.items().size());
        for (const auto& item : selected.items()) {
          if constexpr (std::is_same_v<Set, NoteheadSet>) {
            result.push_back(entity_path(
                voice_path(item.node, item.track, item.stave, item.voice),
                "note", item.entity));
          } else if constexpr (std::is_same_v<Set, ChordSet>) {
            result.push_back(entity_path(
                voice_path(item.node, item.track, item.stave, item.voice),
                "chord", item.entity));
          } else if constexpr (std::is_same_v<Set, RestSet>) {
            result.push_back(entity_path(
                voice_path(item.node, item.track, item.stave, item.voice),
                "rest", item.entity));
          } else if constexpr (std::is_same_v<Set, MarkingSet>) {
            result.push_back(selected_marking_path(item));
          } else if constexpr (std::is_same_v<Set, FullMeasureSet>) {
            result.push_back(staff_path(item.node, item.track, item.stave));
            for (std::size_t offset = 0; offset < item.measure_count;
                 ++offset) {
              result.push_back(
                  measure_path(item.node, item.measure_index + offset));
            }
          } else if constexpr (std::is_same_v<Set, NodeSet>) {
            result.push_back(node_path(item.node));
          } else if constexpr (kRangeLike) {
            result.push_back(
                voice_path(item.node, item.track, item.stave, item.voice));
          }
        }
        std::vector<std::string> unique;
        unique.reserve(result.size());
        for (std::string& id : result) {
          if (std::ranges::find(unique, id) == unique.end())
            unique.push_back(std::move(id));
        }
        return unique;
      },
      *selection);
}

[[nodiscard]] bool selection_is_in_node(const Selection& selection,
                                        NodeId           node_id) {
  return std::visit(
      [node_id](const auto& selected) {
        return std::ranges::all_of(selected.items(), [&](const auto& item) {
          return item.node == node_id;
        });
      },
      selection);
}

}  // namespace

AccessibilityTree::AccessibilityTree(
    std::vector<AccessibilityNode> nodes, std::optional<std::size_t> root,
    std::optional<std::size_t> focused) noexcept
    : nodes_(std::move(nodes)), root_(root), focused_(focused) {}

const AccessibilityNode* AccessibilityTree::find(const std::string& id) const {
  const auto found = std::ranges::find(nodes_, id, &AccessibilityNode::id);
  return found == nodes_.end() ? nullptr : &*found;
}

AccessibilityBuildResult build_notation_accessibility_tree(
    const Project& project, NodeId node_id, const NotationLayout& layout,
    const NotePaletteState& palette, const Selection* selection,
    std::span<const AccessibilityNode::Action> available_actions,
    std::span<const AccessibilityNode::Action> palette_actions,
    std::string_view focused_id, std::span<const std::string> focus_ancestors) {
  const Node* node = project.find_node(node_id);
  if (node == nullptr)
    return {AccessibilityBuildError::kNodeNotFound, std::nullopt};
  if (node->timeline() == nullptr)
    return {AccessibilityBuildError::kTimelineMissing, std::nullopt};
  if (layout.node_id != node_id)
    return {AccessibilityBuildError::kLayoutNodeMismatch, std::nullopt};
  if (selection != nullptr && !validate_selection(project, *selection).empty())
    return {AccessibilityBuildError::kSelectionInvalid, std::nullopt};
  if (selection != nullptr && !selection_is_in_node(*selection, node_id))
    return {AccessibilityBuildError::kSelectionOutsideNode, std::nullopt};

  TreeBuilder       builder;
  const std::size_t root =
      builder.add(node_path(node_id), AccessibilityRole::kNode,
                  node->name().empty() ? "Untitled node" : node->name(),
                  layout.bounds, std::nullopt);

  for (std::size_t ordinal = 0;
       ordinal < node->timeline()->measures().measure_count(); ++ordinal) {
    std::optional<NotationRect> bounds;
    for (const SystemLayout& system : layout.systems) {
      const auto measure =
          std::ranges::find(system.measures, ordinal, &MeasureLayout::ordinal);
      if (measure != system.measures.end()) {
        bounds = container_bounds(layout, measure->id.value, HitRole::kMeasure);
        break;
      }
    }
    builder.add(measure_path(node_id, ordinal), AccessibilityRole::kMeasure,
                "Measure " + std::to_string(ordinal + 1U), bounds, root,
                bounds.has_value() ? AccessibilityState::kNone
                                   : AccessibilityState::kOffscreen);
  }

  for (const Track& track : project.active_tracks()) {
    const TrackLane* lane = node->lane(track.id());
    if (lane == nullptr)
      return {AccessibilityBuildError::kLaneMissing, std::nullopt};
    const std::size_t track_node =
        builder.add(track_path(node_id, track.id()), AccessibilityRole::kTrack,
                    track.name().empty() ? "Untitled track" : track.name(),
                    std::nullopt, root);
    std::size_t staff_ordinal = 0;
    for (const StaveDefinition& stave : track.layout().staves()) {
      const std::size_t staff_node = builder.add(
          staff_path(node_id, track.id(), stave.id), AccessibilityRole::kStaff,
          "Staff " + std::to_string(++staff_ordinal),
          container_bounds(layout, stave.id.to_string(), HitRole::kStaff),
          track_node);
      static const StaveVoices kEmptyVoices;
      const StaveVoices*       voices = lane->stave(stave.id);
      if (voices == nullptr)
        voices = &kEmptyVoices;
      for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
        const Voice       voice = *Voice::create(index);
        const std::string voice_id =
            voice_path(node_id, track.id(), stave.id, voice);
        const NotationId  layout_voice_id{stave.id.to_string() + "/voice/" +
                                         std::to_string(index)};
        const std::size_t voice_node = builder.add(
            voice_id, AccessibilityRole::kVoice,
            "Voice " + std::to_string(index),
            container_bounds(layout, layout_voice_id.value, HitRole::kVoice),
            staff_node);
        add_event_semantics(builder, layout, voices->voice(voice), voice_id,
                            voice_node, voice, *node->timeline());
      }
      if (const auto* spans = lane->pedal_spans(stave.id)) {
        for (const PedalSpan& span : *spans) {
          const auto bounds = marking_bounds(layout, span.id);
          builder.add(marking_path(staff_path(node_id, track.id(), stave.id),
                                   "pedal", span.id),
                      AccessibilityRole::kMarking, "Pedal", bounds, staff_node,
                      bounds.has_value() ? AccessibilityState::kNone
                                         : AccessibilityState::kOffscreen);
        }
      }
    }
  }

  const std::size_t palette_node = builder.add(
      append_path(node_path(node_id), "palette"), AccessibilityRole::kPalette,
      palette.entry_kind() == NotePaletteEntryKind::kNote ? "Note palette"
                                                          : "Rest palette",
      std::nullopt, root);
  builder.set_actions(palette_node, palette_actions);
  const std::size_t selection_node = builder.add(
      append_path(node_path(node_id), "selection"),
      AccessibilityRole::kSelection, "Selection", std::nullopt, root);
  std::vector<std::string> related = selection_relations(selection);
  if (!std::ranges::all_of(related, [&](const std::string& id) {
        return builder.contains(id);
      })) {
    return {AccessibilityBuildError::kSelectionTargetNotExposed, std::nullopt};
  }
  if (builder.has_duplicate_id())
    return {AccessibilityBuildError::kDuplicateSemanticId, std::nullopt};
  builder.select_related(related, available_actions);
  builder.set_related(selection_node, std::move(related));
  builder.set_actions(selection_node, available_actions);
  const std::optional<std::size_t> focused =
      builder.focus(focused_id, focus_ancestors, root);
  return {AccessibilityBuildError::kNone,
          AccessibilityTree(std::move(builder).take_nodes(), root, focused)};
}

}  // namespace graphscore
