# DISPOSABLE — M0 spike, not production code
cmake_minimum_required(VERSION 3.25)
# The SDK's fdebug.h #errors out unless a configuration is selected, so an empty
# CMAKE_BUILD_TYPE (the Ninja single-config default) is not viable.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

project(m0_vst3_spike CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- VST3 SDK options (must be set before the SDK's CMake runs) ---------------
# The SDK hard-fails unless `xcodebuild -version` works, which requires a full
# Xcode install. This machine has Command Line Tools only, so the version check
# is short-circuited via the environment variable the SDK honours.
# Both forms are needed: the env var suppresses the `xcodebuild` probe, and the
# regular variable is what the SDK's version comparison actually reads.
if(APPLE AND NOT DEFINED ENV{XCODE_VERSION})
  set(ENV{XCODE_VERSION} "16.0")
  set(XCODE_VERSION "16.0")
endif()

set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VSTGUI OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_TARGET_VARS_LOG OFF CACHE BOOL "" FORCE)
set(SMTG_USE_STDATOMIC_H OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  vst3sdk
  GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
  GIT_TAG 9fad9770f2ae8542ab1a548a68c1ad1ac690abe0
  GIT_SUBMODULES base cmake pluginterfaces public.sdk
  GIT_SUBMODULES_RECURSE ON
  GIT_PROGRESS ON
)
FetchContent_MakeAvailable(vst3sdk)

# --- the spike host ----------------------------------------------------------
# FRICTION: the `sdk_hosting` target does NOT contain the platform module loader
# (module_mac.mm / module_win32.cpp / module_linux.cpp) or plugprovider.cpp.
# Every host in the SDK compiles those two into its own target, and so must we.
add_executable(m0_vst3_spike
  src/main.cpp
  ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
)
target_link_libraries(m0_vst3_spike PRIVATE sdk_hosting base)
target_include_directories(m0_vst3_spike PRIVATE ${vst3sdk_SOURCE_DIR})

if(APPLE)
  target_sources(m0_vst3_spike PRIVATE
    src/editor_mac.mm
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
  )
  set_source_files_properties(
    src/editor_mac.mm
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(m0_vst3_spike PRIVATE "-framework Cocoa" "-framework CoreFoundation")
endif()

add_dependencies(m0_vst3_spike adelay mda-vst3)

