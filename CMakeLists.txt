cmake_minimum_required(VERSION 3.16)

project(robot_harness VERSION 0.1.0 LANGUAGES CXX)

add_library(robot_harness_core STATIC
  src/core/authority_gate.cpp
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
  src/sample/sample_execution.cpp
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

add_executable(robot_harness_failure_execution
  examples/failure_execution.cpp
)

target_compile_features(robot_harness_normal_execution PRIVATE cxx_std_17)
target_link_libraries(robot_harness_normal_execution PRIVATE robot_harness_sample_fixture)
target_compile_features(robot_harness_failure_execution PRIVATE cxx_std_17)
target_link_libraries(robot_harness_failure_execution PRIVATE robot_harness_sample_fixture)

add_executable(robot_harness_cancellation_execution examples/cancellation_execution.cpp)
target_link_libraries(robot_harness_cancellation_execution PRIVATE robot_harness_sample_fixture)

add_executable(robot_harness_deadline_execution examples/deadline_execution.cpp)
target_link_libraries(robot_harness_deadline_execution PRIVATE robot_harness_sample_fixture)

add_executable(robot_harness_replacement_execution examples/replacement_execution.cpp)
target_link_libraries(robot_harness_replacement_execution PRIVATE robot_harness_sample_fixture)

add_executable(robot_harness_rebinding_execution examples/rebinding_execution.cpp)
target_link_libraries(robot_harness_rebinding_execution PRIVATE robot_harness_sample_fixture)

if(UNIX)
  add_library(robot_harness_compute STATIC
    src/compute/compute_execution.cpp
    src/compute/compute_process.cpp
  )
  target_compile_features(robot_harness_compute PUBLIC cxx_std_17)
  target_link_libraries(robot_harness_compute PUBLIC robot_harness_core)
  add_executable(robot_harness_compute_worker src/compute/compute_worker.cpp)
  target_compile_features(robot_harness_compute_worker PRIVATE cxx_std_17)
  add_executable(robot_harness_compute_execution examples/compute_execution.cpp)
  target_link_libraries(robot_harness_compute_execution PRIVATE robot_harness_compute)
  add_executable(robot_harness_compute_rebinding_execution examples/compute_rebinding_execution.cpp)
  target_link_libraries(robot_harness_compute_rebinding_execution PRIVATE robot_harness_compute)
  # Application example support; not installed or part of the Harness public API.
  add_library(robot_harness_task_example STATIC examples/finite_compute_task.cpp)
  target_link_libraries(robot_harness_task_example PUBLIC robot_harness_compute)
  target_include_directories(robot_harness_task_example PUBLIC examples)
  add_executable(robot_harness_two_step_compute examples/two_step_compute.cpp)
  target_link_libraries(robot_harness_two_step_compute PRIVATE robot_harness_task_example)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(robot_harness_compute PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(robot_harness_compute_worker PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(robot_harness_task_example PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(robot_harness_two_step_compute PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

# Private prototype: Linux local owner/session transport, not an installed API.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(robot_harness_recovery_prototype STATIC
    src/compute/recovery_owner.cpp src/compute/recovery_client.cpp)
  target_compile_features(robot_harness_recovery_prototype PUBLIC cxx_std_17)
  target_link_libraries(robot_harness_recovery_prototype PUBLIC robot_harness_compute)
  target_compile_options(robot_harness_recovery_prototype PRIVATE -Wall -Wextra -Wpedantic)
  add_library(robot_harness_recovery_task_example STATIC examples/recovery_task_execution.cpp)
  target_include_directories(robot_harness_recovery_task_example PRIVATE src/compute)
  target_link_libraries(robot_harness_recovery_task_example PUBLIC robot_harness_task_example
    robot_harness_recovery_prototype)
  target_compile_options(robot_harness_recovery_task_example PRIVATE -Wall -Wextra -Wpedantic)
  add_executable(robot_harness_recovery_execution examples/recovery_execution.cpp)
  target_include_directories(robot_harness_recovery_execution PRIVATE src/compute)
  target_link_libraries(robot_harness_recovery_execution PRIVATE robot_harness_recovery_task_example)
  target_compile_options(robot_harness_recovery_execution PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
