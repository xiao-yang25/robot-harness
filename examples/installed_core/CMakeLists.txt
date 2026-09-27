cmake_minimum_required(VERSION 3.16)
project(robot_harness_consumer LANGUAGES CXX)

find_package(RobotHarness CONFIG REQUIRED)
add_executable(core_consumer main.cpp)
target_link_libraries(core_consumer PRIVATE RobotHarness::core)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME installed_core_runs COMMAND core_consumer)
endif()
