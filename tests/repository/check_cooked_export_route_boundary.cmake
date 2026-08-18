#[[
SPDX-License-Identifier: Apache-2.0

RouteGeometry is editable-project canvas state. The cooked schema and the
domain-to-cooked compiler must not name its type, include its header, or read
OutputConnector::route(); doing so would make route geometry available to the
export path even though it has no runtime meaning.
]]

if(NOT DEFINED GRAPHSCORE_SOURCE_DIR)
  message(FATAL_ERROR "GRAPHSCORE_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE export_boundary_files
  LIST_DIRECTORIES false
  "${GRAPHSCORE_SOURCE_DIR}/include/graphscore/compiler/*"
  "${GRAPHSCORE_SOURCE_DIR}/include/graphscore/cooked_format/*"
  "${GRAPHSCORE_SOURCE_DIR}/src/compiler/*"
  "${GRAPHSCORE_SOURCE_DIR}/src/cooked_format/*"
)

set(forbidden_patterns
  "graphscore/domain/route_geometry[.]hpp"
  "(^|[^A-Za-z0-9_])RouteGeometry([^A-Za-z0-9_]|$)"
  "(^|[^A-Za-z0-9_])RoutePoint([^A-Za-z0-9_]|$)"
  "[.]route[ \t\r\n]*[(][ \t\r\n]*[)]"
)

foreach(candidate IN LISTS export_boundary_files)
  file(READ "${candidate}" contents)
  foreach(pattern IN LISTS forbidden_patterns)
    if(contents MATCHES "${pattern}")
      file(RELATIVE_PATH relative_candidate "${GRAPHSCORE_SOURCE_DIR}"
        "${candidate}")
      message(FATAL_ERROR
        "Writer-only route geometry crossed the cooked-export boundary in "
        "${relative_candidate}")
    endif()
  endforeach()
endforeach()
