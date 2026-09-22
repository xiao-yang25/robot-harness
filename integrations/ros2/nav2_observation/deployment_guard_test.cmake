execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=M4_ISOLATED_SIMULATION
          --unset=M4_FRESH_NAV2_RUN "${NAV2_BINARY}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
if(NOT result STREQUAL "2")
  message(FATAL_ERROR "Missing deployment premise did not fail closed: ${result}; ${output}; ${error}")
endif()
