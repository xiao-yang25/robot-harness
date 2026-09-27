cmake_minimum_required(VERSION 3.16)

# A separate build keeps sanitizer flags and source-tree dependencies out of the consumer.
if(NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "Set SOURCE_DIR and TEST_ROOT to absolute paths")
endif()
if(NOT IS_ABSOLUTE "${SOURCE_DIR}" OR NOT IS_ABSOLUTE "${TEST_ROOT}")
  message(FATAL_ERROR "SOURCE_DIR and TEST_ROOT must be absolute")
endif()
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef run_id)
set(work "${TEST_ROOT}/${run_id}")
file(MAKE_DIRECTORY "${work}")

function(run)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Command failed (${result}): ${ARGV}; files retained in ${work}")
  endif()
endfunction()

set(toolchain_args)
foreach(setting CMAKE_CXX_COMPILER CMAKE_OSX_SYSROOT CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ${setting})
    list(APPEND toolchain_args "-D${setting}=${${setting}}")
  endif()
endforeach()
run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${work}/producer"
  -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release ${toolchain_args}
  "-DCMAKE_INSTALL_PREFIX=${work}/original" -DCMAKE_INSTALL_LIBDIR=lib)
run("${CMAKE_COMMAND}" --build "${work}/producer" --target robot_harness_core
  --config Release --parallel 2)
run("${CMAKE_COMMAND}" --install "${work}/producer" --config Release)
file(RENAME "${work}/original" "${work}/relocated")
file(COPY "${SOURCE_DIR}/examples/installed_core/" DESTINATION "${work}/consumer")
run("${CMAKE_COMMAND}" -S "${work}/consumer" -B "${work}/consumer-build"
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON ${toolchain_args}
  "-DCMAKE_PREFIX_PATH=${work}/relocated"
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF)
run("${CMAKE_COMMAND}" --build "${work}/consumer-build" --config Release --parallel 2)
find_program(ctest_command NAMES ctest)
if(NOT ctest_command)
  message(FATAL_ERROR "CTest is required to run the consumer")
endif()
run("${CMAKE_COMMAND}" -E chdir "${work}/consumer-build"
  "${ctest_command}" -C Release --output-on-failure)
message(STATUS "Installed Core consumer passed after relocation; files: ${work}")
