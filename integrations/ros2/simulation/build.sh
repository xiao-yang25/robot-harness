#!/usr/bin/env bash
set -eo pipefail
source /opt/ros/humble/setup.bash
cmake -S integrations/ros2/fixture_interfaces -B /build/interfaces -DCMAKE_INSTALL_PREFIX=/drive
cmake --build /build/interfaces --parallel 2
cmake --install /build/interfaces
source /drive/share/m4_drive_probe/local_setup.bash
cmake -S integrations/ros2/simulation/native_drive -B /build/drive -DCMAKE_PREFIX_PATH=/drive -DCMAKE_INSTALL_PREFIX=/drive -DCMAKE_BUILD_TYPE=Debug
cmake --build /build/drive --parallel 2
cmake --install /build/drive
cmake -S integrations/ros2/simulation/native_worker -B /native -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /native --parallel 2
ctest --test-dir /native --output-on-failure --no-tests=error
cmake -S integrations/ros2/nav2_observation -B /harness -DCMAKE_PREFIX_PATH=/drive -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DROBOT_HARNESS_NAV2_SETTLEMENT=ON
cmake --build /harness --parallel 2 --target robot_harness_nav2_observation robot_harness_nav2_settlement nav2_motion_window_test nav2_closure_observations_test nav2_cancel_response_test nav2_runtime_observation_watch_test
ctest --test-dir /harness -R '^nav2_' --output-on-failure --no-tests=error
