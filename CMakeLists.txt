cmake_minimum_required(VERSION 3.16)

project(robot_harness VERSION 0.1.0 LANGUAGES CXX)

add_library(robot_harness_core STATIC
  src/core.cpp
)

target_compile_features(robot_harness_core PUBLIC cxx_std_17)
target_include_directories(robot_harness_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(robot_harness_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
elseif(MSVC)
  target_compile_options(robot_harness_core PRIVATE /W4)
endif()

add_library(robot_harness_sample_fixture STATIC
  src/sample_execution.cpp
)

target_compile_features(robot_harness_sample_fixture PUBLIC cxx_std_17)
target_include_directories(robot_harness_sample_fixture PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(robot_harness_sample_fixture PUBLIC robot_harness_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(robot_harness_sample_fixture PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
elseif(MSVC)
  target_compile_options(robot_harness_sample_fixture PRIVATE /W4)
endif()

include(CTest)

add_executable(robot_harness_normal_execution
  examples/normal_execution.cpp
)

target_compile_features(robot_harness_normal_execution PRIVATE cxx_std_17)
target_link_libraries(robot_harness_normal_execution PRIVATE robot_harness_sample_fixture)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
