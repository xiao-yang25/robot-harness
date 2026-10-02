# Use Core from another CMake project

This directory is an independent C++ project. It links the installed Core through
`find_package(RobotHarness CONFIG REQUIRED)` and `RobotHarness::core`, without
adding the Harness source tree or private headers to its build. C++17 is a
transitive requirement of that target.

From the Harness repository root, build and install Core into a local prefix:

```sh
cmake -S . -B build-core -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/build-core/prefix"
cmake --build build-core --target robot_harness_core --parallel 2
cmake --install build-core
cmake -S examples/installed_core -B build-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/build-core/prefix"
cmake --build build-consumer --parallel 2
(cd build-consumer && ctest --output-on-failure)
```

With a multi-configuration generator, use `--config Release` when building and
installing, and `ctest -C Release`. On macOS, if the default SDK is incompatible
with your compiler, pass the same [SDK override](../../docs/TESTING.md#macos-sdk-selection)
to both configure commands.

The program prints:

```text
Installed Core: missing readiness blocks admission; no work dispatched
```

It creates a Core gate without backend observations and verifies that a request
gets no authority. This checks the installed library and its admission boundary;
it does not execute a robot action, supply synthetic readiness, or implement an
adapter. See the [backend integration path](../../docs/README.md#connect-a-backend)
for the responsibilities of a real host.

Copy this directory into your own project and point `CMAKE_PREFIX_PATH` at the
installation. You may move the installation before configuring a new consumer.
`CMAKE_INSTALL_LIBDIR` and `CMAKE_INSTALL_INCLUDEDIR` support relative custom
locations; for a nonstandard layout, set `RobotHarness_DIR` to the directory
containing `RobotHarnessConfig.cmake`. Use matching architecture and compatible
C++ toolchains for the library and consumer. Do not mix sanitizer-instrumented
libraries with an uninstrumented link command.

The install contains `authority_gate.hpp`, the static Core library, CMake package
files and the project license declaration/texts under `share/licenses/RobotHarness`
(or the configured CMake data directory). Compute, sample fixtures, workers,
recovery and ROS support remain
source-tree integrations. Preview version checks accept only the same version;
this is not an SDK/API/ABI stability promise or a release. See the
[MIT OR Apache-2.0 terms](../../README.md#license).

The [installation check](../../docs/TESTING.md#installed-core-consumer) builds a
fresh Core, relocates the prefix and copies this consumer outside the source tree.
