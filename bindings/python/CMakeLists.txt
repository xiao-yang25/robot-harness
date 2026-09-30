Python3_add_library(_core MODULE WITH_SOABI core_module.cpp)
target_link_libraries(_core PRIVATE RobotHarness::core)
set(robot_harness_python_output "${CMAKE_BINARY_DIR}/python/robot_harness")
set_target_properties(_core PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${robot_harness_python_output}")

file(GLOB robot_harness_python_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/robot_harness/*.py")
foreach(robot_harness_python_source IN LISTS robot_harness_python_sources)
  get_filename_component(robot_harness_python_name "${robot_harness_python_source}" NAME)
  configure_file("${robot_harness_python_source}"
    "${robot_harness_python_output}/${robot_harness_python_name}" COPYONLY)
endforeach()

set(robot_harness_python_install "${CMAKE_INSTALL_LIBDIR}/robot-harness/python/robot_harness")
install(TARGETS _core LIBRARY DESTINATION "${robot_harness_python_install}")
install(FILES ${robot_harness_python_sources} DESTINATION "${robot_harness_python_install}")

# Source-only optional backend. Dependencies are installed by the consumer.
set(robot_harness_mujoco_source "${PROJECT_SOURCE_DIR}/integrations/mujoco/robot_harness_mujoco/__init__.py")
configure_file("${robot_harness_mujoco_source}"
  "${CMAKE_BINARY_DIR}/python/robot_harness_mujoco/__init__.py" COPYONLY)
install(FILES "${robot_harness_mujoco_source}"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/robot-harness/python/robot_harness_mujoco")

# MCP is source-only and opt-in at execution; the Core/Session import needs no SDK.
file(GLOB robot_harness_mcp_sources CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/integrations/mcp/robot_harness_mcp/*.py")
foreach(robot_harness_mcp_source IN LISTS robot_harness_mcp_sources)
  get_filename_component(robot_harness_mcp_name "${robot_harness_mcp_source}" NAME)
  configure_file("${robot_harness_mcp_source}"
    "${CMAKE_BINARY_DIR}/python/robot_harness_mcp/${robot_harness_mcp_name}" COPYONLY)
endforeach()
install(FILES ${robot_harness_mcp_sources}
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/robot-harness/python/robot_harness_mcp")
