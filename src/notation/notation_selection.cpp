// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_selection.hpp>

#include "hit_resolution_internal.hpp"
#include "notation_geometry.hpp"
#include "notation_ids.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

std::optional<Selection> resolve_selection_at(const Project&          project,
                                              const NotationLayout&   layout,
                                              const NotePaletteState& palette,
                                              NotationPoint           point) {
  if (!finite_point(point)) {
    return std::nullopt;
  }

  const std::optional<HitResult> hit = layout.hit_test(point);
  if (hit.has_value()) {
    if (hit->role == HitRole::kMarking) {
      return resolve_marking_selection(project, layout, *hit);
    }
    if (hit->role == HitRole::kNotehead || hit->role == HitRole::kEvent) {
      const std::optional<ResolvedVoiceEntity> resolved =
          resolve_hit_entity(project, layout, *hit);
      if (!resolved.has_value()) {
        return std::nullopt;
      }
      const ResolvedEntity& entity = resolved->entity;

      // When two stemless chords in different voices share the same
      // onset, they emit overlapping notehead-column regions.
      // hit_test breaks the tie by priority first, then area, then
      // semantic_id order -- and that last tie-break depends on UUID
      // ordering, which is not deterministic across IDs.
      //
      // Armed-voice preference applies only among candidates that are
      // truly tied at the UUID-order stage: same priority, equal area
      // (exact equality, matching hit_test's own area comparison), same
      // staff/system, and same musical onset.  When priority or area
      // distinguishes the candidates, hit_test's own order is
      // deterministically correct regardless of UUID values, and this
      // function preserves the winning column as-is.  Direct
      // notehead/glyph hits are also unaffected: they resolve through
      // kNotehead or kMarking, not through this kEvent column branch.
      if (hit->role == HitRole::kEvent &&
          hit_id_ends_with(hit->id, kHitSuffixNoteheadColumn) &&
          entity.kind == ResolvedEntityKind::kChord) {
        // Find the winning region to recover its priority and area.
        const HitRegion* winner_region = nullptr;
        for (const HitRegion& r : layout.hit_regions) {
          if (r.id == hit->id) {
            winner_region = &r;
            break;
          }
        }
        if (winner_region != nullptr) {
          // Validate the winner's owner metadata via constrained
          // resolution: find the actual SystemLayout/
          // StaffSystemLayout named by owner_system_id/owner_staff_id,
          // resolve the entity within that staff only, and verify the
          // entity's onset belongs to that system's measure range.
          // Rejects absent/nonexistent/inconsistent owner metadata
          // before the column-disambiguation scan ever reads it.
          const std::optional<ResolvedVoiceEntity> winner_constrained =
              resolve_entity_constrained(project, layout, *winner_region);
          if (!winner_constrained.has_value()) {
            return std::nullopt;
          }

          const auto area_fn = [](const HitRegion& r) {
            return r.bounds.width * r.bounds.height;
          };
          const double winner_area = area_fn(*winner_region);

          for (const HitRegion& region : layout.hit_regions) {
            if (region.role != HitRole::kEvent ||
                !hit_id_ends_with(region.id, kHitSuffixNoteheadColumn) ||
                region.id == hit->id) {
              continue;
            }
            // Only override when the alternative is genuinely tied:
            // same priority, equal area, and the column covers the
            // click point.
            if (region.priority != winner_region->priority) {
              continue;
            }
            const double alt_area = area_fn(region);
            // Area comparison matches hit_test's own exact equality;
            // a tolerance-based guard would admit an alternative whose
            // area differs from the winner (but stays within the
            // tolerance), overriding a higher-priority hit_test result
            // that was correctly determined by priority/area alone.
            if (winner_area != alt_area) {
              continue;
            }
            if (!region.bounds.contains(point)) {
              continue;
            }

            // Owner-constrained column resolution: given the
            // alternative HitRegion's owner_system_id/owner_staff_id,
            // locate the actual SystemLayout and StaffSystemLayout in
            // the layout tree, resolve the semantic entity only within
            // the named staff's own voices, and verify the entity's
            // onset falls within the named system's actual measure
            // range.  Rejects stale/synthetic/forged regions whose
            // owner metadata disagrees with the actual layout.
            const std::optional<ResolvedVoiceEntity> alt_resolved =
                resolve_entity_constrained(project, layout, region);
            if (!alt_resolved.has_value()) {
              continue;
            }
            // Same system and staff verified by constrained resolution
            // having located both in the actual layout tree pointed at
            // by each region's owner_system_id/owner_staff_id.
            // resolve_entity_constrained validates owner metadata by
            // confirming the entity is actually present in the named
            // staff's voice content and that its onset falls within the
            // named system's measure range.  Because system measure
            // ranges are non-overlapping, equal global onset (checked
            // below) necessarily implies the entities belong to the
            // same system for any valid layout.  The explicit owner-ID
            // equality comparison below is therefore defense-in-depth:
            // it rejects stale or synthetic regions whose owner
            // metadata disagrees with reality (e.g. a forged region
            // that copies the winner's owner IDs but names an entity
            // on a different staff/system), and guards against future
            // emitter drift in owner-metadata assignment, but it is not
            // independently observable as a cross-system guard for
            // valid layouts with equal-onset entities.
            if (alt_resolved->system != winner_constrained->system ||
                alt_resolved->staff != winner_constrained->staff) {
              continue;
            }
            if (alt_resolved->entity.kind != ResolvedEntityKind::kChord) {
              continue;
            }
            // Verify equal musical onset: locate both events in
            // their owning voice's event list and compare the
            // running onset.
            if (alt_resolved->content == nullptr ||
                winner_constrained->content == nullptr) {
              continue;
            }
            const auto find_onset =
                [](const VoiceContent& content,
                   const VoiceEvent*   target) -> std::optional<Rational> {
              Rational onset;
              for (const VoiceEvent& ev : content.events()) {
                if (&ev == target) {
                  return onset;
                }
                onset = onset + event_duration(ev).resolved();
              }
              return std::nullopt;
            };
            const std::optional<Rational> winner_onset = find_onset(
                *winner_constrained->content, winner_constrained->entity.event);
            const std::optional<Rational> alt_onset =
                find_onset(*alt_resolved->content, alt_resolved->entity.event);
            if (!winner_onset.has_value() || !alt_onset.has_value() ||
                *winner_onset != *alt_onset) {
              continue;
            }

            if (alt_resolved->voice == palette.voice()) {
              // The armed voice owns a genuinely tied column at this
              // point; use its chord instead of the one hit_test
              // returned by UUID ordering.
              const NodeId            alt_node  = layout.node_id;
              const TrackId           alt_track = alt_resolved->staff->track_id;
              const StaveId           alt_stave = alt_resolved->staff->stave_id;
              const Voice             alt_voice = alt_resolved->voice;
              std::optional<ChordSet> set       = ChordSet::create(
                  {ChordItem{alt_node, alt_track, alt_stave, alt_voice,
                             alt_resolved->entity.id}});
              if (!set.has_value()) {
                return std::nullopt;
              }
              return Selection{*std::move(set)};
            }
          }
        }
      }

      // Defends against a future emitter silently drifting from the
      // resolution logic above: each of these checks fails safe (returns
      // std::nullopt) rather than trusting a hit whose id's own naming
      // convention disagrees with what it resolved to, e.g. a kNotehead-role
      // region whose id was not actually built from one of the two
      // notehead-emitting suffixes, or a "stem" region that somehow
      // resolved to something other than the Note/Chord a stem is drawn
      // for. This is deliberately a defensive `if`, not `assert`: an
      // NDEBUG-only caller would otherwise make hit_id_ends_with itself
      // dead code (and so, an unused-function warning) in a release build.
      if (hit->role == HitRole::kNotehead &&
          !hit_id_ends_with(hit->id, kHitSuffixNotehead) &&
          !hit_id_ends_with(hit->id, kHitSuffixGraceNotehead)) {
        return std::nullopt;
      }
      if (hit_id_ends_with(hit->id, kHitSuffixStem) &&
          entity.kind != ResolvedEntityKind::kNote &&
          entity.kind != ResolvedEntityKind::kChord) {
        return std::nullopt;
      }
      if (hit_id_ends_with(hit->id, kHitSuffixRest) &&
          entity.kind != ResolvedEntityKind::kRest) {
        return std::nullopt;
      }

      const NodeId  node  = layout.node_id;
      const TrackId track = resolved->staff->track_id;
      const StaveId stave = resolved->staff->stave_id;
      const Voice   voice = resolved->voice;

      switch (entity.kind) {
        case ResolvedEntityKind::kNote:
        case ResolvedEntityKind::kChordNote:
        case ResolvedEntityKind::kGraceNote: {
          // A single top-level Note has no notehead identity distinct from
          // its own id, so its stem/dot/accidental (kEvent, not kNotehead)
          // selects the same one notehead a direct kNotehead hit on it
          // would. A ChordNote's own stem/dot/accidental (the chord's, or
          // that one notehead's) likewise selects just that notehead --
          // Chord-level markings belong to the ChordSet arm below instead.
          std::optional<NoteheadSet> set = NoteheadSet::create(
              {NoteheadItem{node, track, stave, voice, entity.id}});
          if (!set.has_value()) {
            return std::nullopt;
          }
          return Selection{*std::move(set)};
        }
        case ResolvedEntityKind::kChord: {
          std::optional<ChordSet> set = ChordSet::create(
              {ChordItem{node, track, stave, voice, entity.id}});
          if (!set.has_value()) {
            return std::nullopt;
          }
          return Selection{*std::move(set)};
        }
        case ResolvedEntityKind::kRest: {
          std::optional<RestSet> set =
              RestSet::create({RestItem{node, track, stave, voice, entity.id}});
          if (!set.has_value()) {
            return std::nullopt;
          }
          return Selection{*std::move(set)};
        }
        case ResolvedEntityKind::kNone:
          return std::nullopt;
      }
    }
    // Every ResolvedEntityKind enumerator the switch above covers already
    // returns, so only a hit whose role is none of kNotehead/kEvent/
    // kMarking (kSystem, kMeasure, kStaff, kVoice, kStaffMeasure today)
    // reaches here and falls through to insertion-caret resolution below,
    // using the point the hit occurred at.
  }

  const std::optional<ResolvedInsertionSite> site =
      resolve_insertion_site(project, layout, palette.voice(), point);
  if (!site.has_value()) {
    return std::nullopt;
  }
  // The caret's legality is the domain's own rule
  // (validate_insertion_caret_set, graphscore/domain/selection.cpp): position
  // 0, TrackLane::total_length(), or an existing event boundary in the
  // *armed* voice. resolve_insertion_site's onset is not automatically any
  // of these -- when the armed voice is empty, the onset it snaps to comes
  // from a hypothetical measure-aligned rest fill (see that function's own
  // comment), which is a real onset for a preview but not necessarily one
  // the domain accepts as a caret before that fill is actually materialized.
  // Rejecting an illegal onset here, rather than trusting it, is what keeps
  // every Selection this function returns satisfying
  // validate_selection(...).empty().
  const Rational lane_end = site->lane->total_length();
  if (site->resolved_onset != Rational(0) && site->resolved_onset != lane_end &&
      !site->voice_content->find_event_index_at(site->resolved_onset)
           .has_value()) {
    return std::nullopt;
  }
  std::optional<InsertionCaretSet> set =
      InsertionCaretSet::create({InsertionCaretItem{
          layout.node_id, site->staff->track_id, site->staff->stave_id,
          palette.voice(), site->resolved_onset}});
  if (!set.has_value()) {
    return std::nullopt;
  }
  return Selection{*std::move(set)};
}

}  // namespace graphscore
