// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <graphscore/notation/notation_types.hpp>

namespace graphscore {

inline constexpr char32_t         kTimeZero                = U'\uE080';
inline constexpr std::string_view kHitSuffixNotehead       = "notehead";
inline constexpr std::string_view kHitSuffixNoteheadColumn = "notehead-column";
inline constexpr std::string_view kHitSuffixGraceNotehead  = "grace-notehead";
inline constexpr std::string_view kHitSuffixStem           = "stem";
inline constexpr std::string_view kHitSuffixRest           = "rest";
inline constexpr std::string_view kHitRoleArticulation     = "articulation";
inline constexpr std::string_view kHitRoleTie              = "tie";
inline constexpr std::string_view kHitRoleTupletDigit      = "tuplet/digit";
inline constexpr std::string_view kHitRoleStaffMeasure     = "staff-measure";
inline constexpr int              kHitPrioritySystem       = 0;
inline constexpr int              kHitPriorityMeasure      = 1;
inline constexpr int              kHitPriorityStaff        = 2;
inline constexpr int              kHitPriorityVoice        = 3;
inline constexpr int              kHitPriorityStaffMeasure = 4;
inline constexpr int              kHitPriorityNoteheadColumn = 5;
inline constexpr int              kHitPriorityGlyph          = 6;
inline constexpr int              kHitPrioritySpanSegment    = 7;
inline constexpr int              kHitPriorityNotehead       = 8;

[[nodiscard]] inline NotationId make_id(const std::string& root,
                                        const std::string& role) {
  return NotationId{root + "/" + role};
}

template <typename Tag>
[[nodiscard]] NotationId make_id(const StrongId<Tag>& id,
                                 const std::string&   role) {
  return make_id(id.to_string(), role);
}

[[nodiscard]] inline NotationId staff_measure_semantic_id(
    const NotationId& staff_id, std::size_t ordinal) {
  return make_id(staff_id.value, std::string(kHitRoleStaffMeasure) + "/" +
                                     std::to_string(ordinal));
}

}  // namespace graphscore
