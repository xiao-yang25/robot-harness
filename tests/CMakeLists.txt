add_executable(robot_harness_core_tests
  authority_gate_tests.cpp
)

target_compile_features(robot_harness_core_tests PRIVATE cxx_std_17)
target_link_libraries(robot_harness_core_tests PRIVATE robot_harness_core)

add_test(
  NAME robot_harness.authority_gate
  COMMAND robot_harness_core_tests
)

add_executable(robot_harness_sample_execution_tests
  sample_execution_tests.cpp
)

target_compile_features(robot_harness_sample_execution_tests PRIVATE cxx_std_17)
target_link_libraries(robot_harness_sample_execution_tests PRIVATE robot_harness_sample_fixture)

add_test(
  NAME robot_harness.sample_execution
  COMMAND robot_harness_sample_execution_tests
)

add_test(
  NAME robot_harness.normal_execution_example
  COMMAND robot_harness_normal_execution
)

set_tests_properties(
  robot_harness.authority_gate
  robot_harness.sample_execution
  robot_harness.normal_execution_example
  PROPERTIES TIMEOUT 10
)
