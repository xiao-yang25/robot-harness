#!/usr/bin/env bash
set -eo pipefail
[[ ${M4_ISOLATED_SIMULATION:-} == 1 && -n ${M4_FRESH_NAV2_RUN:-} && -f /.dockerenv ]] || exit 2
case ${M4_CONTEXT_CASE:-} in normal|obstacle-wait|obstacle-wait-frozen-scan|missing-controller|missing-map|withhold-feedback|withhold-planner-ack|withhold-drive-ack|withhold-odometry|cancel-moving|cancel-moving-withhold-planner-ack|cancel-moving-withhold-drive-ack|cancel-moving-withhold-odometry|replace-moving|replace-moving-withhold-planner-ack|replace-moving-withhold-drive-ack|replace-moving-withhold-odometry|replace-moving-missing-map|replace-moving-missing-controller|runtime-odometry-loss|runtime-odometry-replay|runtime-odometry-resume|runtime-clock-loss|runtime-stop-unreachable|runtime-stop-unreachable-withhold-drive-ack) ;; *) exit 2 ;; esac
# Container exit is the final process boundary (including a SIGSTOP fault).
# Supervise long-lived helpers while the Owner and checker run; early helper
# termination, even exit 0, is a failed fixture rather than a valid scenario.
helpers=()
diagnostic_pid=''
cleanup() {
  local status=$?
  trap - EXIT
  if [[ -n $diagnostic_pid ]]; then kill -TERM "$diagnostic_pid" 2>/dev/null || true; fi
  for pid in "${helpers[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
  exit "$status"
}
trap cleanup EXIT
supervise() {
  local task=$1
  while kill -0 "$task" 2>/dev/null; do
    for pid in "${helpers[@]}"; do
      if ! kill -0 "$pid" 2>/dev/null; then
        echo "required simulation helper exited: $pid" >&2
        kill -TERM "$task" 2>/dev/null || true
        return 125
      fi
    done
    sleep 0.2
  done
  for pid in "${helpers[@]}"; do
    if ! kill -0 "$pid" 2>/dev/null; then
      echo "required simulation helper exited: $pid" >&2
      return 125
    fi
  done
  wait "$task"
}
export M4_WORK_SCOPE_CASE=normal
source /opt/ros/humble/setup.bash
source /drive/share/m4_drive_probe/local_setup.bash
export GAZEBO_PLUGIN_PATH="/drive/lib:${GAZEBO_PLUGIN_PATH:-}"
export LD_LIBRARY_PATH="/drive/lib:${LD_LIBRARY_PATH:-}"
export TURTLEBOT3_MODEL=waffle

# Best-effort read-only observer; never affects the Owner or scenario verdict.
python3 /simulation/simulation_diagnostics.py > /output/diagnostics.log 2>&1 &
diagnostic_pid=$!
export GAZEBO_MODEL_PATH="/opt/ros/humble/share/turtlebot3_gazebo/models:/usr/share/gazebo-11/models:${GAZEBO_MODEL_PATH:-}"
export GAZEBO_MODEL_DATABASE_URI=''
export M4_NATIVE_TAIL_MS=2500
export LD_LIBRARY_PATH="/native:${LD_LIBRARY_PATH:-}"
python3 - <<'PY'
import yaml
import json
import secrets
import xml.etree.ElementTree as ET
from pathlib import Path
from ament_index_python.packages import get_package_share_directory
params = yaml.safe_load((Path(get_package_share_directory('nav2_bringup'))/'params/nav2_params.yaml').read_text())
# Exercise digit-leading identities: ROS topic segments still start with s_.
contexts = dict(a_scope="0" + secrets.token_hex(16)[1:], b_scope="9" + secrets.token_hex(16)[1:])
Path('/output/producer-context.json').write_text(json.dumps(contexts))
nav = params['bt_navigator']['ros__parameters']
nav['default_nav_to_pose_bt_xml'] = '/simulation/native_worker/single_path.xml'
nav['plugin_lib_names'].extend(['scoped_follow_path', 'scoped_compute_path'])
Path('/output/native-params.yaml').write_text(yaml.safe_dump(params, sort_keys=False))
model = ET.parse(Path(get_package_share_directory('nav2_bringup'))/'worlds/waffle.model')
plugins = [p for p in model.getroot().iter('plugin') if p.get('filename') == 'libgazebo_ros_diff_drive.so']
if len(plugins) != 1:
    raise RuntimeError('expected one native drive plugin')
plugins[0].set('filename', 'libscoped_diff_drive.so')
ET.SubElement(plugins[0], 'generation_mode').text = 'true'
model.write('/output/scoped-waffle.model', encoding='unicode')
PY
if [[ ${M4_VISUAL:-0} == 1 ]]; then
  python3 /simulation/configure_view.py
  LP_NUM_THREADS=1 rviz2 -d /output/live-view.rviz --ros-args -p use_sim_time:=true > /output/rviz.log 2>&1 &
  helpers+=($!)
  ffmpeg -nostdin -y -loglevel error -f x11grab -video_size 1600x900 \
    -framerate 5 -i "$DISPLAY" -threads 1 -pix_fmt yuvj444p \
    -q:v 2 -f image2 -update 1 -atomic_writing 1 /output/live.jpg \
    > /output/capture.log 2>&1 &
  helpers+=($!)
fi
python3 /simulation/producer_bridge.py > /output/producer-bridge.jsonl 2>&1 &
helpers+=($!)
ros2 launch /simulation/native_worker/producer_scope.launch.py headless:=True use_rviz:=False \
  use_sim_time:=True use_respawn:=False use_composition:=False \
  params_file:=/output/native-params.yaml robot_name:=turtlebot3_waffle \
  x_pose:=-2.0 y_pose:=-0.5 robot_sdf:=/output/scoped-waffle.model > /output/launch.log 2>&1 &
helpers+=($!)
python3 /simulation/owner_context_launcher.py > /output/context-launcher.log 2>&1 &
helpers+=($!)
if [[ $M4_CONTEXT_CASE == runtime-* ]]; then
  python3 /simulation/runtime_observation_relay.py > /output/runtime-relay.jsonl 2>&1 &
  helpers+=($!)
fi
if [[ $M4_CONTEXT_CASE == obstacle-wait* ]]; then
  python3 /simulation/obstacle_fixture.py > /output/obstacle.jsonl 2>&1 &
  helpers+=($!)
fi
injector=''
if [[ $M4_CONTEXT_CASE == replace-moving ]]; then
  python3 /simulation/inject_old_producer.py > /output/old-producer-injection.jsonl 2>&1 &
  injector=$!
fi
owner_exit=0
/harness/robot_harness_nav2_settlement "$M4_CONTEXT_CASE" > /output/owner.jsonl 2>&1 &
supervise "$!" || owner_exit=$?
if [[ $M4_CONTEXT_CASE == withhold-feedback ]]; then
  [[ $owner_exit == 1 ]] || exit 1
else
  [[ $owner_exit == 0 ]] || exit "$owner_exit"
fi

if [[ -n $injector ]]; then wait "$injector"; fi

case $M4_CONTEXT_CASE in
  obstacle-wait*) checker=check_obstacle_wait.py ;;
  runtime-*) checker=check_runtime_observation_loss.py ;;
  cancel-moving*) checker=check_owner_cancellation.py ;;
  *) checker=check_owner_handoff.py ;;
esac
python3 "/simulation/$checker" > /output/verification.json 2>&1 &
supervise "$!"
