# SPDX-License-Identifier: Apache-2.0
# GraphScore M0 engraving-engine spike. This is not part of the production DAG.

cmake_minimum_required(VERSION 3.20)
project(m0_engraving_engine VERSION 0.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SPIKE_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 6910c9d9165801d8827d628cb72eb7ea9dd538c5
    GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(SPIKE_WARNING_FLAGS)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    list(APPEND SPIKE_WARNING_FLAGS
        -Weverything
        -Werror
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-padded
        -Wno-weak-vtables
        -Wno-exit-time-destructors
        -Wno-global-constructors
        -Wno-ctad-maybe-unsupported
        -Wno-switch-enum
        -Wno-covered-switch-default
        -Wno-float-equal
        -Wno-double-promotion
        -Wno-implicit-int-conversion
        -Wno-sign-conversion
        -Wno-shorten-64-to-32
        -Wno-unused-macros
        -Wno-reserved-macro-identifier
        -Wno-date-time
        -Wno-zero-as-null-pointer-constant
        -Wno-unused-parameter
        -Wno-poison-system-directories
        -Wno-documentation-unknown-command
        -Wno-deprecated-copy-with-user-provided-dtor
        -Wno-switch-default
        -Wno-unused-private-field
        -Wno-unused-variable
        -Wno-missing-field-initializers
    )
endif()

add_library(engraving_spike_lib STATIC
    src/engraving_evaluator.cpp
    src/smuf_fallback.cpp
)
target_include_directories(engraving_spike_lib PUBLIC src)
target_compile_options(engraving_spike_lib PRIVATE ${SPIKE_WARNING_FLAGS})

add_executable(engraving_spike_test tests/test_layout.cpp)
target_link_libraries(engraving_spike_test PRIVATE engraving_spike_lib GTest::gtest_main)
target_include_directories(engraving_spike_test SYSTEM PRIVATE
    ${googletest_SOURCE_DIR}/googletest/include
)
target_compile_options(engraving_spike_test PRIVATE ${SPIKE_WARNING_FLAGS})

add_executable(engraving_spike_evaluator src/main.cpp)
target_link_libraries(engraving_spike_evaluator PRIVATE engraving_spike_lib)
target_compile_options(engraving_spike_evaluator PRIVATE ${SPIKE_WARNING_FLAGS})

if(SPIKE_ENABLE_ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    set(SPIKE_SANITIZER_FLAGS
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
    )
    foreach(target_name IN ITEMS engraving_spike_lib engraving_spike_test engraving_spike_evaluator)
        target_compile_options(${target_name} PRIVATE ${SPIKE_SANITIZER_FLAGS})
        target_link_options(${target_name} PRIVATE ${SPIKE_SANITIZER_FLAGS})
    endforeach()
endif()

enable_testing()
add_test(NAME engraving_layout_evaluation COMMAND engraving_spike_test)

message(STATUS "GraphScore engraving spike: owned code is strict; GTest is isolated")
