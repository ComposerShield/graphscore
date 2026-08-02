// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/duplicate_nodes_command.hpp>

#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/event_listener.hpp>
#include <graphscore/domain/graph_position.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/track.hpp>
#include <graphscore/domain/voice_content.hpp>
#include "clipboard_command_helpers.hpp"

namespace graphscore {

namespace {

struct ConnectorIdMaps {
  std::unordered_map<ConnectorId, ConnectorId> inputs;
  std::unordered_map<ConnectorId, ConnectorId> outputs;
};

// The outcome of a multi-node publish/unpublish step: the terminal Result to
// return, plus whether an attempted rollback of an earlier partial failure
// itself failed. `faulted` true means the caller must move the command to
// State::kFaulted -- the project may be left in a state this command can no
// longer account for.
struct PublishOutcome {
  Result result;
  bool   faulted = false;
};

Result duplicate_voice_content(const VoiceContent& source, VoiceContent* out) {
  VoiceContent                content;
  internal::RegeneratedEvents regenerated =
      internal::regenerate_events(source.events());
  for (VoiceEvent& event : regenerated.events) {
    const Result append_result = content.append(std::move(event));
    if (!append_result.ok())
      return append_result;
  }

  const Result marking_result =
      internal::add_regenerated_markings(content, source, regenerated.id_map);
  if (!marking_result.ok())
    return marking_result;

  const Result validate_result = content.validate();
  if (!validate_result.ok())
    return validate_result;

  *out = std::move(content);
  return Result();
}

// Deep-copies `source` onto a fresh TrackLane: every stave key is preserved
// (StaveId is project-owned, never duplicated -- N5), every voice's notation
// entities are regenerated (N3), and every pedal span is copied with a
// freshly minted id at its original start/end (no offset -- this is not a
// clipping operation).
Result duplicate_track_lane(const TrackLane& source, TrackLane* out) {
  TrackLane lane;
  for (const StaveId stave_id : source.stave_ids()) {
    lane.ensure_stave(stave_id);
    const StaveVoices* src_stave = source.stave(stave_id);
    StaveVoices*       dst_stave = lane.stave(stave_id);
    if (src_stave == nullptr || dst_stave == nullptr)
      return Result(ResultCode::kInternalError);

    for (std::uint8_t v = Voice::kMin; v <= Voice::kMax; ++v) {
      const std::optional<Voice> voice = Voice::create(v);
      if (!voice.has_value())
        continue;
      VoiceContent content;
      const Result result =
          duplicate_voice_content(src_stave->voice(*voice), &content);
      if (!result.ok())
        return result;
      dst_stave->voice(*voice) = std::move(content);
    }

    const std::vector<PedalSpan>* spans = source.pedal_spans(stave_id);
    if (spans != nullptr) {
      for (const PedalSpan& span : *spans) {
        const Result add_result = lane.add_pedal_span(
            stave_id, make_pedal_span(span.start, span.end));
        if (!add_result.ok())
          return add_result;
      }
    }
  }
  *out = std::move(lane);
  return Result();
}

// Deep-copies every lane `source` holds for a track in `tracks` onto `dup`.
// Called once for the project's active tracks and once for its archived
// tracks -- a node's lanes can reference either, since Project::archive_track
// leaves a node's already-recorded lane data untouched (project.hpp).
Result duplicate_lanes_for(const std::vector<Track>& tracks, const Node& source,
                           Node& dup) {
  for (const Track& track : tracks) {
    if (!source.has_lane(track.id()))
      continue;
    const TrackLane* src_lane = source.lane(track.id());
    if (src_lane == nullptr)
      return Result(ResultCode::kInternalError);

    TrackLane    new_lane;
    const Result lane_result = duplicate_track_lane(*src_lane, &new_lane);
    if (!lane_result.ok())
      return lane_result;

    dup.ensure_lane(track.id());
    TrackLane* live_lane = dup.lane(track.id());
    if (live_lane == nullptr)
      return Result(ResultCode::kInternalError);
    *live_lane = std::move(new_lane);
  }
  return Result();
}

// Builds one duplicate Node: fresh id, verbatim name/color/notes, the
// offset position, fresh input/output connectors (recorded into `maps` for
// the later edge-remapping pass), preserved event bindings and listener
// configuration, deep-copied lanes, and a verbatim-copied timeline. Does not
// wire any destination -- see wire_duplicate_edges.
std::optional<Node> build_duplicate_node(const Project& project,
                                         const Node& source, NodeId new_id,
                                         GraphPosition    offset,
                                         ConnectorIdMaps& maps) {
  Node dup(new_id, source.name());
  dup.set_color(source.color());
  dup.set_notes(source.notes());

  GraphPosition position = source.position();
  position.x += offset.x;
  position.y += offset.y;
  dup.set_position(position);

  for (const InputConnector& input : source.inputs())
    maps.inputs.emplace(input.id(), dup.add_input(input.name()));

  for (const OutputConnector& output : source.outputs()) {
    const ConnectorId new_output_id =
        dup.add_output(output.name(), output.type());
    maps.outputs.emplace(output.id(), new_output_id);

    OutputConnector* new_output = dup.find_output(new_output_id);
    if (new_output == nullptr)
      return std::nullopt;
    new_output->set_priority(output.priority());
    if (!new_output->set_weight(output.weight()).ok())
      return std::nullopt;
    new_output->set_export_enabled(output.export_enabled());

    if (const std::optional<EventId> event = output.event_binding();
        event.has_value()) {
      if (!dup.bind_output_event(new_output_id, event).ok())
        return std::nullopt;
      if (const EventListener* listener = source.find_listener(*event);
          listener != nullptr) {
        if (!dup.set_listener_policy(*event, listener->policy(),
                                     listener->capacity())
                 .ok())
          return std::nullopt;
      }
    }
  }

  if (!duplicate_lanes_for(project.active_tracks(), source, dup).ok())
    return std::nullopt;
  if (!duplicate_lanes_for(project.archived_tracks(), source, dup).ok())
    return std::nullopt;

  // Guards against silent data loss: duplicate_lanes_for can only find a
  // lane whose TrackId appears in active_tracks() or archived_tracks(). A
  // lane keyed to a track in neither set (which should be unreachable --
  // every TrackId a node holds a lane for comes from one of those two
  // lists) would otherwise be dropped without any signal.
  if (dup.lane_count() != source.lane_count())
    return std::nullopt;

  if (source.has_timeline())
    dup.set_timeline(*source.timeline());

  return dup;
}

// Second pass: remaps every intra-selection edge (source and destination
// both duplicated) onto the corresponding duplicate endpoints, including
// self-loops. An edge whose destination is not in the selection is left
// unwired (dropped -- N7); no explicit route handling is needed here
// because build_duplicate_node's add_output already minted the duplicate's
// output with a fresh, automatic RouteGeometry, and never copies the
// source's (N8).
void wire_duplicate_edges(
    const Project& project, const std::vector<NodeId>& order,
    const std::unordered_map<NodeId, NodeId>&          node_id_map,
    const std::unordered_map<NodeId, ConnectorIdMaps>& connector_maps,
    std::vector<Node>&                                 duplicates) {
  for (std::size_t i = 0; i < order.size(); ++i) {
    const Node* source = project.find_node(order[i]);
    if (source == nullptr)
      continue;
    const auto maps_it = connector_maps.find(order[i]);
    if (maps_it == connector_maps.end())
      continue;

    for (const OutputConnector& output : source->outputs()) {
      const std::optional<ConnectorDestination>& destination =
          output.destination();
      if (!destination.has_value())
        continue;

      const auto dest_node_it = node_id_map.find(destination->node);
      if (dest_node_it == node_id_map.end())
        continue;

      const auto dest_maps_it = connector_maps.find(destination->node);
      if (dest_maps_it == connector_maps.end())
        continue;
      const auto dest_input_it =
          dest_maps_it->second.inputs.find(destination->connector);
      if (dest_input_it == dest_maps_it->second.inputs.end())
        continue;

      const auto new_output_it = maps_it->second.outputs.find(output.id());
      if (new_output_it == maps_it->second.outputs.end())
        continue;

      OutputConnector* new_output =
          duplicates[i].find_output(new_output_it->second);
      if (new_output == nullptr)
        continue;
      new_output->set_destination(
          ConnectorDestination{dest_node_it->second, dest_input_it->second});
    }
  }
}

// Builds every duplicate named by `selection`, in selection order, with
// fresh identity throughout and intra-selection edges remapped. Returns
// std::nullopt on any failure -- including the defensive (practically
// unreachable) check that no freshly minted NodeId already exists in the
// project -- with nothing yet published to `project`.
std::optional<std::vector<Node>> build_duplicates(const Project& project,
                                                  const NodeSet& selection,
                                                  GraphPosition  offset) {
  std::vector<NodeId> order;
  order.reserve(selection.items().size());
  for (const NodeItem& item : selection.items())
    order.push_back(item.node);

  std::unordered_map<NodeId, NodeId> node_id_map;
  node_id_map.reserve(order.size());
  for (const NodeId id : order)
    node_id_map.emplace(id, NodeId::generate());

  std::vector<Node> duplicates;
  duplicates.reserve(order.size());
  std::unordered_map<NodeId, ConnectorIdMaps> connector_maps;
  connector_maps.reserve(order.size());

  for (const NodeId source_id : order) {
    const Node* source = project.find_node(source_id);
    if (source == nullptr)
      return std::nullopt;

    ConnectorIdMaps     maps;
    std::optional<Node> dup = build_duplicate_node(
        project, *source, node_id_map.at(source_id), offset, maps);
    if (!dup.has_value())
      return std::nullopt;

    connector_maps.emplace(source_id, std::move(maps));
    duplicates.push_back(std::move(*dup));
  }

  wire_duplicate_edges(project, order, node_id_map, connector_maps, duplicates);

  for (const Node& dup : duplicates) {
    if (project.find_node(dup.id()) != nullptr)
      return std::nullopt;
  }

  return duplicates;
}

// Moves every node in `nodes` into `project` via Project::restore_node,
// preserving each node's exact id. If a later insertion fails after earlier
// ones succeeded, every already-published node is removed again so the
// project ends up unchanged; if that rollback itself cannot complete,
// `faulted` reports it so the caller can move the command to
// State::kFaulted rather than claim a state it can no longer verify.
PublishOutcome publish_nodes(Project&           project,
                             std::vector<Node>& nodes) noexcept {
  // Pre-sized (never appended-to) so that recording a successful publish is
  // a plain, non-throwing indexed assignment -- no operation that can throw
  // ever runs outside a try block below.
  std::vector<NodeId> published;
  try {
    published.assign(nodes.size(), NodeId{});
  } catch (const std::bad_alloc&) {
    return PublishOutcome{Result(ResultCode::kOutOfMemory), false};
  } catch (const std::length_error&) {
    return PublishOutcome{Result(ResultCode::kOutOfMemory), false};
  }
  std::size_t published_count = 0;

  for (Node& node : nodes) {
    const NodeId id = node.id();
    Result       insert_result;
    try {
      insert_result = project.restore_node(std::move(node));
    } catch (const std::bad_alloc&) {
      insert_result = Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      insert_result = Result(ResultCode::kOutOfMemory);
    } catch (...) {
      insert_result = Result(ResultCode::kInternalError);
    }
    if (insert_result.ok()) {
      published[published_count] = id;
      ++published_count;
      continue;
    }

    bool rollback_ok = true;
    for (std::size_t i = 0; i < published_count; ++i) {
      Result remove_result;
      try {
        remove_result = project.remove_node(published[i]);
      } catch (...) {
        remove_result = Result(ResultCode::kInternalError);
      }
      if (!remove_result.ok())
        rollback_ok = false;
    }
    return PublishOutcome{insert_result, !rollback_ok};
  }
  return PublishOutcome{Result(), false};
}

// Removes every node in `created` from `project` by id, after first
// verifying every one is still present (Node has no operator== to compare
// full snapshots by value, so this is an existence guard, not a deep
// stale-context comparison; CommandHistory's linear undo/redo guarantee is
// what actually keeps a duplicate untouched between execute and undo, the
// same reliance RemoveNodeCommand's undo documents).
//
// If every removal succeeds, the project ends up with none of `created`
// present. If a later removal fails after earlier ones succeeded, the
// removal is rolled back: every node in `created` still present in the
// project is removed first -- this reaches both the survivors never
// reached by the failed pass, and any survivor left cascade-damaged by an
// earlier removal in that same pass (Project::remove_node clears every
// other node's destination that targeted the removed node's inputs, and a
// duplicate only ever receives an edge from another duplicate, never from
// outside the selection, so this can only ever touch a sibling in
// `created`) -- and then every node in `created` is restored verbatim from
// its own snapshot. Order does not matter for that final restore because
// each snapshot's destinations are self-contained data, not references
// resolved against the live project, so the project ends up with every
// created node reinstated exactly as it was right after execute. If any
// step of that rollback itself cannot complete, `faulted` reports it so
// the caller can move the command to State::kFaulted rather than claim a
// state it can no longer verify.
PublishOutcome unpublish_nodes(Project&                 project,
                               const std::vector<Node>& created) noexcept {
  for (const Node& node : created) {
    if (project.find_node(node.id()) == nullptr)
      return PublishOutcome{Result(ResultCode::kInvalidArgument), false};
  }

  Result failure;
  for (const Node& node : created) {
    Result remove_result;
    try {
      remove_result = project.remove_node(node.id());
    } catch (const std::bad_alloc&) {
      remove_result = Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      remove_result = Result(ResultCode::kOutOfMemory);
    } catch (...) {
      remove_result = Result(ResultCode::kInternalError);
    }
    if (!remove_result.ok()) {
      failure = remove_result;
      break;
    }
  }
  if (failure.ok())
    return PublishOutcome{Result(), false};

  bool rollback_ok = true;
  for (const Node& node : created) {
    if (project.find_node(node.id()) == nullptr)
      continue;
    Result remove_result;
    try {
      remove_result = project.remove_node(node.id());
    } catch (const std::bad_alloc&) {
      remove_result = Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      remove_result = Result(ResultCode::kOutOfMemory);
    } catch (...) {
      remove_result = Result(ResultCode::kInternalError);
    }
    if (!remove_result.ok())
      rollback_ok = false;
  }
  for (const Node& node : created) {
    Result restore_result;
    try {
      restore_result = project.restore_node(node);
    } catch (const std::bad_alloc&) {
      restore_result = Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      restore_result = Result(ResultCode::kOutOfMemory);
    } catch (...) {
      restore_result = Result(ResultCode::kInternalError);
    }
    if (!restore_result.ok())
      rollback_ok = false;
  }

  return PublishOutcome{failure, !rollback_ok};
}

}  // namespace

Result DuplicateNodesCommand::execute(Project& project) noexcept {
  if (state_ == State::kFaulted)
    return Result(ResultCode::kCommandFaulted);
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  bool selection_ok = false;
  try {
    selection_ok = validate_selection(project, Selection(selection_)).empty();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    return Result(ResultCode::kInternalError);
  }
  if (!selection_ok)
    return Result(ResultCode::kInvalidArgument);

  std::optional<std::vector<Node>> built;
  std::vector<Node>                snapshot;
  try {
    built = build_duplicates(project, selection_, offset_);
    if (built.has_value())
      snapshot = *built;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    return Result(ResultCode::kInternalError);
  }
  if (!built.has_value())
    return Result(ResultCode::kInvalidArgument);

  const PublishOutcome outcome = publish_nodes(project, *built);
  if (!outcome.result.ok()) {
    if (outcome.faulted)
      state_ = State::kFaulted;
    return outcome.result;
  }

  // Nothing below this boundary can fail: publish_nodes has already
  // committed every duplicate to `project`, and both remaining writes are
  // plain non-throwing move/scalar assignments.
  static_assert(std::is_nothrow_move_assignable_v<decltype(created_)>);
  static_assert(std::is_nothrow_assignable_v<decltype(state_)&, State>);
  created_ = std::move(snapshot);
  state_   = State::kDone;
  return Result();
}

Result DuplicateNodesCommand::undo(Project& project) noexcept {
  if (state_ == State::kFaulted)
    return Result(ResultCode::kCommandFaulted);
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (created_.empty())
    return Result(ResultCode::kInternalError);

  const PublishOutcome outcome = unpublish_nodes(project, created_);
  if (!outcome.result.ok()) {
    if (outcome.faulted)
      state_ = State::kFaulted;
    return outcome.result;
  }

  static_assert(std::is_nothrow_assignable_v<decltype(state_)&, State>);
  state_ = State::kUndone;
  return Result();
}

Result DuplicateNodesCommand::redo(Project& project) noexcept {
  if (state_ == State::kFaulted)
    return Result(ResultCode::kCommandFaulted);
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (created_.empty())
    return Result(ResultCode::kInternalError);

  std::vector<Node> to_publish;
  try {
    to_publish = created_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  for (const Node& node : to_publish) {
    if (project.find_node(node.id()) != nullptr)
      return Result(ResultCode::kInvalidArgument);
  }

  const PublishOutcome outcome = publish_nodes(project, to_publish);
  if (!outcome.result.ok()) {
    if (outcome.faulted)
      state_ = State::kFaulted;
    return outcome.result;
  }

  static_assert(std::is_nothrow_assignable_v<decltype(state_)&, State>);
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
