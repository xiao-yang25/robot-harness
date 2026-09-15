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

add_executable(robot_harness_m2a_failure_tests
  m2a_failure_tests.cpp
)

target_compile_features(robot_harness_m2a_failure_tests PRIVATE cxx_std_17)
target_link_libraries(robot_harness_m2a_failure_tests PRIVATE robot_harness_sample_fixture)

add_test(
  NAME robot_harness.m2a_failure
  COMMAND robot_harness_m2a_failure_tests
)

add_test(
  NAME robot_harness.normal_execution_example
  COMMAND robot_harness_normal_execution
)

add_test(
  NAME robot_harness.failure_execution_example
  COMMAND robot_harness_failure_execution
)

set_tests_properties(
  robot_harness.authority_gate
  robot_harness.sample_execution
  robot_harness.m2a_failure
  robot_harness.normal_execution_example
  robot_harness.failure_execution_example
  PROPERTIES TIMEOUT 10
)

add_executable(robot_harness_m2b_cancellation_tests m2b_cancellation_tests.cpp)
target_link_libraries(robot_harness_m2b_cancellation_tests PRIVATE robot_harness_sample_fixture)
add_test(NAME robot_harness.m2b_cancellation COMMAND robot_harness_m2b_cancellation_tests)
set_tests_properties(robot_harness.m2b_cancellation PROPERTIES TIMEOUT 10)

add_test(NAME robot_harness.cancellation_execution_example COMMAND robot_harness_cancellation_execution)
set_tests_properties(robot_harness.cancellation_execution_example PROPERTIES TIMEOUT 10)

add_executable(robot_harness_m2c_deadline_tests m2c_deadline_tests.cpp)
target_link_libraries(robot_harness_m2c_deadline_tests PRIVATE robot_harness_sample_fixture)
add_test(NAME robot_harness.m2c_deadline COMMAND robot_harness_m2c_deadline_tests)
add_test(NAME robot_harness.deadline_execution_example COMMAND robot_harness_deadline_execution)
set_tests_properties(robot_harness.m2c_deadline robot_harness.deadline_execution_example PROPERTIES TIMEOUT 10)

add_executable(robot_harness_compute_capability_tests compute_capability_tests.cpp)
target_link_libraries(robot_harness_compute_capability_tests PRIVATE robot_harness_core)
add_test(NAME robot_harness.compute_capabilities COMMAND robot_harness_compute_capability_tests)
set_tests_properties(robot_harness.compute_capabilities PROPERTIES TIMEOUT 10)

if(UNIX)
  set(compute_worker_mode 0)
  foreach(mode IN ITEMS ignore startup_delay malformed early_exit blocked_control delayed_exit unavailable_control backpressure eof_alive backwards)
    math(EXPR compute_worker_mode "${compute_worker_mode} + 1")
    add_executable(robot_harness_compute_test_worker_${mode} compute_test_worker.cpp)
    target_include_directories(robot_harness_compute_test_worker_${mode} PRIVATE ../src)
    target_compile_features(robot_harness_compute_test_worker_${mode} PRIVATE cxx_std_17)
    target_compile_definitions(robot_harness_compute_test_worker_${mode} PRIVATE
      ROBOT_HARNESS_TEST_WORKER_MODE=${compute_worker_mode})
  endforeach()
  add_executable(robot_harness_compute_execution_tests compute_execution_tests.cpp)
  target_link_libraries(robot_harness_compute_execution_tests PRIVATE robot_harness_compute)
  foreach(mode IN ITEMS normal cancel prelaunch cancel_after_exit host_isolation)
    add_test(NAME robot_harness.compute_${mode}
      COMMAND robot_harness_compute_execution_tests ${mode} $<TARGET_FILE:robot_harness_compute_worker>)
    set_tests_properties(robot_harness.compute_${mode} PROPERTIES TIMEOUT 15)
  endforeach()
  foreach(mode IN ITEMS shutdown deadline)
    add_test(NAME robot_harness.compute_${mode}
      COMMAND robot_harness_compute_execution_tests ${mode} $<TARGET_FILE:robot_harness_compute_test_worker_startup_delay>)
    set_tests_properties(robot_harness.compute_${mode} PROPERTIES TIMEOUT 15)
  endforeach()
  add_test(NAME robot_harness.compute_ignore COMMAND robot_harness_compute_execution_tests
    ignore $<TARGET_FILE:robot_harness_compute_test_worker_ignore>)
  add_test(NAME robot_harness.compute_backpressure COMMAND robot_harness_compute_execution_tests
    backpressure $<TARGET_FILE:robot_harness_compute_test_worker_backpressure>)
  set_tests_properties(robot_harness.compute_backpressure PROPERTIES TIMEOUT 15)
  add_test(NAME robot_harness.compute_delayed_exit COMMAND robot_harness_compute_execution_tests
    delayed_exit $<TARGET_FILE:robot_harness_compute_test_worker_delayed_exit>)
  foreach(mode IN ITEMS malformed early_exit)
    add_test(NAME robot_harness.compute_${mode} COMMAND robot_harness_compute_execution_tests
      fault $<TARGET_FILE:robot_harness_compute_test_worker_${mode}>)
    set_tests_properties(robot_harness.compute_${mode} PROPERTIES TIMEOUT 15)
  endforeach()
  foreach(mode IN ITEMS eof_alive backwards)
    add_test(NAME robot_harness.compute_${mode} COMMAND robot_harness_compute_execution_tests
      ${mode} $<TARGET_FILE:robot_harness_compute_test_worker_${mode}>)
    set_tests_properties(robot_harness.compute_${mode} PROPERTIES TIMEOUT 15)
  endforeach()
  add_test(NAME robot_harness.compute_execution_example COMMAND robot_harness_compute_execution
    $<TARGET_FILE:robot_harness_compute_worker>)
  set_tests_properties(robot_harness.compute_ignore robot_harness.compute_delayed_exit
    robot_harness.compute_execution_example PROPERTIES TIMEOUT 15)
endif()

if(UNIX)
  add_executable(robot_harness_compute_process_tests compute_process_tests.cpp)
  target_include_directories(robot_harness_compute_process_tests PRIVATE ../src)
  target_link_libraries(robot_harness_compute_process_tests PRIVATE robot_harness_compute)
  add_test(NAME robot_harness.compute_process COMMAND robot_harness_compute_process_tests
    $<TARGET_FILE:robot_harness_compute_worker>
    $<TARGET_FILE:robot_harness_compute_test_worker_ignore>
    $<TARGET_FILE:robot_harness_compute_test_worker_startup_delay>
    $<TARGET_FILE:robot_harness_compute_test_worker_malformed>
    $<TARGET_FILE:robot_harness_compute_test_worker_early_exit>
    $<TARGET_FILE:robot_harness_compute_test_worker_blocked_control>
    $<TARGET_FILE:robot_harness_compute_test_worker_delayed_exit>
    $<TARGET_FILE:robot_harness_compute_test_worker_unavailable_control>)
  set_tests_properties(robot_harness.compute_process PROPERTIES TIMEOUT 30)
endif()
