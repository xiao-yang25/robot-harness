cmake_minimum_required(VERSION 3.16)
project(m4_drive_probe LANGUAGES C CXX)
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(geometry_msgs REQUIRED)
# Message-only counterpart of the trusted simulation fixture. No drive or server.
rosidl_generate_interfaces(${PROJECT_NAME}
  "srv/CloseMotionScope.srv" "srv/OpenMotionScope.srv" "srv/CloseScopedMotion.srv"
  "msg/ScopedTwist.msg" DEPENDENCIES geometry_msgs)
ament_export_dependencies(rosidl_default_runtime)
ament_package()
