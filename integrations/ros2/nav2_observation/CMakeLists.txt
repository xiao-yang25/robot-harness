cmake_minimum_required(VERSION 3.16)
project(robot_harness_nav2_observation LANGUAGES CXX)

# An explicit, source-tree-only build; the normal Core build has no ROS dependency.
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(rosgraph_msgs REQUIRED)
find_package(lifecycle_msgs REQUIRED)
include(CTest)
add_subdirectory(../../.. core EXCLUDE_FROM_ALL)
add_executable(robot_harness_nav2_observation navigation_observation.cpp)
target_compile_features(robot_harness_nav2_observation PRIVATE cxx_std_17)
target_compile_options(robot_harness_nav2_observation PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(robot_harness_nav2_observation robot_harness_core)
ament_target_dependencies(robot_harness_nav2_observation rclcpp rclcpp_action nav2_msgs
  nav_msgs geometry_msgs rosgraph_msgs lifecycle_msgs)

if(BUILD_TESTING)
  add_executable(nav2_motion_window_test motion_window_test.cpp)
  target_compile_features(nav2_motion_window_test PRIVATE cxx_std_17)
  add_test(NAME nav2_motion_window COMMAND nav2_motion_window_test)
  add_test(NAME nav2_requires_fresh_deployment
    COMMAND ${CMAKE_COMMAND}
      -DNAV2_BINARY=$<TARGET_FILE:robot_harness_nav2_observation>
      -P ${CMAKE_CURRENT_SOURCE_DIR}/deployment_guard_test.cmake)
endif()
