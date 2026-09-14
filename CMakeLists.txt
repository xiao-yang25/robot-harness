cmake_minimum_required(VERSION 3.16)

project(robot_harness VERSION 0.1.0 LANGUAGES CXX)

add_library(robot_harness_core STATIC
  src/core.cpp
)

target_compile_features(robot_harness_core PUBLIC cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(robot_harness_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
  )
elseif(MSVC)
  target_compile_options(robot_harness_core PRIVATE /W4)
endif()

include(CTest)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
