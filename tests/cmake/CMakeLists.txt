# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

project(GraphScoreConsumerTest
  DESCRIPTION "External consumer project verifying runtime isolation"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(graphscore_consumer_test
  consumer_test.cpp
)

target_include_directories(graphscore_consumer_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/../include
)

target_link_libraries(graphscore_consumer_test
  PRIVATE
    graphscore_runtime
)

add_test(NAME graphscore_consumer_test
  COMMAND graphscore_consumer_test)
