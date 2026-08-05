# SPDX-License-Identifier: Apache-2.0
#
# Bravura SMuFL font acquisition (ADR 0002 §A4). Bravura is a shipped
# resource, not a CMake link dependency: no CMake target is created here.
# graphscore_rendering consumes the resulting font path only through the
# runtime-computed path graphscore_writer_shell passes in (ADR 0002 §A4),
# never a build-time hardcoded path.
#
# Writer-only: include()d from the root CMakeLists.txt inside the same
# `if (GRAPHSCORE_BUILD_WRITER)` block that gates SDL3/ThorVG/FreeType/
# HarfBuzz, so `-DGRAPHSCORE_BUILD_WRITER=OFF` downloads nothing here
# (ADR 0002 §A7.1).
#
# Not a FetchContent dependency (a single asset file, not a source tree), so
# the FETCHCONTENT_SOURCE_DIR_<NAME>/graphscore_offline_dependencies
# machinery does not apply. BRAVURA_FONT_SRC is the offline override here,
# exactly as ADR 0004 §6 already established for the spike-only Noto Sans
# font.

set(GRAPHSCORE_BRAVURA_GIT_TAG "02e8ed29a29115df35007d1178cebaeee26c20e1")
set(GRAPHSCORE_BRAVURA_SHA256
  "dca2d90c88437a701b1c2e71fa54e76f9fa41d7deee935d74dc871ea66ecfdd2")
set(GRAPHSCORE_BRAVURA_URL
  "https://raw.githubusercontent.com/steinbergmedia/bravura/${GRAPHSCORE_BRAVURA_GIT_TAG}/redist/otf/Bravura.otf")
set(GRAPHSCORE_BRAVURA_DEST "${CMAKE_BINARY_DIR}/bravura_font/Bravura.otf")

set(BRAVURA_FONT_SRC "" CACHE FILEPATH
  "Path to a pre-acquired Bravura.otf (pinned SHA ${GRAPHSCORE_BRAVURA_GIT_TAG}), for offline/air-gapped builds. Overrides the file(DOWNLOAD) acquisition below. SHA-256 verified regardless of source (ADR 0002 §A4); a mismatch is a configure failure, never a silent substitution.")

# Every acquisition path verifies the pinned SHA-256, not only the fresh
# download path (ADR 0002 §A4: "Every acquisition path ... verifies the
# SHA-256 above at CMake configure time and fails with FATAL_ERROR on any
# mismatch").
function(graphscore_verify_bravura_sha256 file_path)
  file(SHA256 "${file_path}" actual_sha256)
  if (NOT actual_sha256 STREQUAL GRAPHSCORE_BRAVURA_SHA256)
    message(FATAL_ERROR
      "Bravura font at '${file_path}' has SHA-256 '${actual_sha256}', "
      "expected '${GRAPHSCORE_BRAVURA_SHA256}' (ADR 0002 §A4, pinned commit "
      "${GRAPHSCORE_BRAVURA_GIT_TAG}). No system-font fallback is "
      "permitted: a mismatched or corrupted font artifact is a configure "
      "failure, not a silent substitution.")
  endif()
endfunction()

if (BRAVURA_FONT_SRC)
  if (NOT EXISTS "${BRAVURA_FONT_SRC}")
    message(FATAL_ERROR
      "BRAVURA_FONT_SRC='${BRAVURA_FONT_SRC}' does not exist.")
  endif()
  graphscore_verify_bravura_sha256("${BRAVURA_FONT_SRC}")
  set(GRAPHSCORE_BRAVURA_FONT_PATH "${BRAVURA_FONT_SRC}")
elseif (EXISTS "${GRAPHSCORE_BRAVURA_DEST}")
  # A prior configure of this same build tree already downloaded the font;
  # re-verify rather than re-download so a corrupted cache entry cannot
  # silently persist across configures.
  graphscore_verify_bravura_sha256("${GRAPHSCORE_BRAVURA_DEST}")
  set(GRAPHSCORE_BRAVURA_FONT_PATH "${GRAPHSCORE_BRAVURA_DEST}")
else()
  file(DOWNLOAD "${GRAPHSCORE_BRAVURA_URL}" "${GRAPHSCORE_BRAVURA_DEST}"
    EXPECTED_HASH SHA256=${GRAPHSCORE_BRAVURA_SHA256}
    STATUS GRAPHSCORE_BRAVURA_DOWNLOAD_STATUS
  )
  list(GET GRAPHSCORE_BRAVURA_DOWNLOAD_STATUS 0 GRAPHSCORE_BRAVURA_DOWNLOAD_CODE)
  if (NOT GRAPHSCORE_BRAVURA_DOWNLOAD_CODE EQUAL 0)
    list(GET GRAPHSCORE_BRAVURA_DOWNLOAD_STATUS 1 GRAPHSCORE_BRAVURA_DOWNLOAD_MESSAGE)
    message(FATAL_ERROR
      "Failed to download Bravura.otf from ${GRAPHSCORE_BRAVURA_URL}: "
      "${GRAPHSCORE_BRAVURA_DOWNLOAD_MESSAGE}. Set BRAVURA_FONT_SRC to a "
      "pre-acquired copy for offline/air-gapped builds (ADR 0002 §A4).")
  endif()
  # file(DOWNLOAD ... EXPECTED_HASH ...) already fails configure on a hash
  # mismatch on its own; this call makes that guarantee explicit and
  # uniform across all three acquisition paths rather than relying on it
  # implicitly for only this one.
  graphscore_verify_bravura_sha256("${GRAPHSCORE_BRAVURA_DEST}")
  set(GRAPHSCORE_BRAVURA_FONT_PATH "${GRAPHSCORE_BRAVURA_DEST}")
endif()

set(GRAPHSCORE_BRAVURA_FONT_PATH "${GRAPHSCORE_BRAVURA_FONT_PATH}"
  CACHE INTERNAL "Path to the SHA-256-verified Bravura.otf (ADR 0002 §A4)")

message(STATUS
  "Bravura: ${GRAPHSCORE_BRAVURA_GIT_TAG}, SHA-256 verified at "
  "${GRAPHSCORE_BRAVURA_FONT_PATH}")
