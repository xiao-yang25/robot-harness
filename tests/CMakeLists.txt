add_executable(robot_harness_core_smoke
  core_smoke.cpp
)

target_compile_features(robot_harness_core_smoke PRIVATE cxx_std_17)
target_link_libraries(robot_harness_core_smoke PRIVATE robot_harness_core)

add_test(
  NAME robot_harness.core_smoke
  COMMAND robot_harness_core_smoke
)
