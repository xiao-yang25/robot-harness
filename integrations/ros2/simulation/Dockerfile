FROM ros:humble-ros-base-jammy AS simulation
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
ENV DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_MIRROR
# A caller may select a reachable HTTPS Ubuntu mirror. Repository keys and
# package/version checks remain unchanged; never embed a host proxy here.
COPY integrations/ros2/simulation/configure_mirror.py /tmp/configure_mirror.py
RUN python3 /tmp/configure_mirror.py
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake g++ nlohmann-json3-dev python3-yaml \
    ros-humble-rosidl-default-generators \
    'ros-humble-navigation2=1.1.20-*' 'ros-humble-nav2-bringup=1.1.20-*' \
    'ros-humble-gazebo-plugins=3.9.0-*' 'ros-humble-turtlebot3-gazebo=2.3.8-*' \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY examples examples
COPY tests tests
COPY integrations/ros2/fixture_interfaces integrations/ros2/fixture_interfaces
COPY integrations/ros2/nav2_observation integrations/ros2/nav2_observation
COPY integrations/ros2/simulation/native_drive integrations/ros2/simulation/native_drive
COPY integrations/ros2/simulation/native_worker integrations/ros2/simulation/native_worker
COPY integrations/ros2/simulation/build.sh integrations/ros2/simulation/build.sh
RUN bash integrations/ros2/simulation/build.sh
COPY integrations/ros2/simulation /simulation
RUN dpkg-query -W > /simulation/packages.txt
ENV LIBGL_ALWAYS_SOFTWARE=1 QT_X11_NO_MITSHM=1 ROS_LOCALHOST_ONLY=1 ROS_DOMAIN_ID=74
ENV HOME=/tmp/simulation-home PYTHONUNBUFFERED=1
WORKDIR /simulation
ENTRYPOINT ["bash", "/simulation/entrypoint.sh"]

# Optional display tools stay out of the default headless target.
FROM simulation AS visual
RUN apt-get update && apt-get install -y --no-install-recommends \
    xvfb xauth ffmpeg ros-humble-rviz2 \
    && rm -rf /var/lib/apt/lists/*
RUN dpkg-query -W > /simulation/packages.txt
