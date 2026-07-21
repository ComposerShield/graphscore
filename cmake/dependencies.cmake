# SPDX-License-Identifier: Apache-2.0
#
# FetchContent declarations, one dependency per block, each pinned to an
# immutable 40-character commit SHA (ADR 0001). Every dependency here must
# already be POLICY-CLEARED in ADR 0002/docs/NOTICES.md before it is added.

include(FetchContent)

# --- GoogleTest (ADR 0002 §11) --------------------------------------------

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
if (WIN32)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG 6910c9d9165801d8827d628cb72eb7ea9dd538c5 # release-1.16.0
  OVERRIDE_FIND_PACKAGE
  SYSTEM
)
FetchContent_MakeAvailable(googletest)
