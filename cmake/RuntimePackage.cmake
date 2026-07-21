# SPDX-License-Identifier: Apache-2.0
#
# Install and export rules for GraphScore Runtime.
#
# The acceptance criterion is that a runtime consumer — a game engine, a
# plain C host — needs only the public C header and the shared library. This
# module is what makes that mechanically true: it installs exactly the
# runtime link closure (ADR 0003 §3.1) and the C ABI header, and nothing
# else. No writer target, no writer header, no writer dependency is
# installable.
#
# Consumers use:
#
#   find_package(GraphScoreRuntime REQUIRED)
#   target_link_libraries(my_app PRIVATE graphscore::runtime)
#
# tests/cmake proves this against a real install tree.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# The runtime closure. graphscore_runtime links graphscore_runtime_impl
# PRIVATE, but a static PRIVATE dependency of a shared library still appears
# in its INTERFACE_LINK_LIBRARIES as $<LINK_ONLY:...>, so every target in the
# closure must belong to the export set for the export to be well-formed.
#
# This list is exactly ADR 0003 §3.1's runtime closure. A target appearing
# here that is not in that closure would mean the runtime had grown a
# dependency the architecture audit should already have rejected.
set(GRAPHSCORE_RUNTIME_PACKAGE_TARGETS
  graphscore_core
  graphscore_c_abi
  graphscore_cooked_format
  graphscore_scheduler
  graphscore_loader
  graphscore_runtime_impl
  graphscore_runtime
)

# Export as graphscore::runtime rather than graphscore::graphscore_runtime.
# The internal target names carry the prefix because CMake target names are
# global; the namespace already supplies it for consumers.
foreach (gs_exported IN LISTS GRAPHSCORE_RUNTIME_PACKAGE_TARGETS)
  string(REGEX REPLACE "^graphscore_" "" gs_export_name "${gs_exported}")
  set_target_properties(${gs_exported} PROPERTIES EXPORT_NAME ${gs_export_name})
endforeach()
unset(gs_exported)
unset(gs_export_name)

set_target_properties(graphscore_warnings PROPERTIES EXPORT_NAME warnings)

install(TARGETS ${GRAPHSCORE_RUNTIME_PACKAGE_TARGETS}
  EXPORT GraphScoreRuntimeTargets
  RUNTIME       DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# graphscore::warnings carries only compile options, but it is a link-time
# usage requirement of every owned target, so the export set needs it to
# resolve. It installs no files.
install(TARGETS graphscore_warnings EXPORT GraphScoreRuntimeTargets)

# Only the C ABI header is installed. The C++ headers of the closure's
# internal targets are deliberately absent: a consumer that could include
# them would have a second, unstable interface to the runtime.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/graphscore/c_abi/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/graphscore/c_abi"
  FILES_MATCHING PATTERN "*.h"
)

install(EXPORT GraphScoreRuntimeTargets
  FILE GraphScoreRuntimeTargets.cmake
  NAMESPACE graphscore::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GraphScoreRuntime"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/GraphScoreRuntimeConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/cmake/GraphScoreRuntimeConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/GraphScoreRuntimeConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GraphScoreRuntime"
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/GraphScoreRuntimeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/GraphScoreRuntimeConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GraphScoreRuntime"
)

install(FILES
  "${CMAKE_SOURCE_DIR}/LICENSE"
  "${CMAKE_SOURCE_DIR}/NOTICE"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/GraphScore"
)
