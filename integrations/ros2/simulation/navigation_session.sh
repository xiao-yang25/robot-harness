#!/usr/bin/env bash
set -eo pipefail
[[ -f /.dockerenv && ${M6_NATIVE_ISOLATED:-} == 1 ]] || exit 2
mkdir /output/session-started
mkdir -p "$HOME"
source /opt/ros/humble/setup.bash
source /drive/share/m4_drive_probe/local_setup.bash
export PYTHONPATH="/installed/lib/robot-harness/python${PYTHONPATH:+:$PYTHONPATH}"
export M4_ISOLATED_SIMULATION=1
export M4_WORK_SCOPE_CASE=normal
export M4_NATIVE_TAIL_MS=2500
export GAZEBO_PLUGIN_PATH="/drive/lib:${GAZEBO_PLUGIN_PATH:-}"
export LD_LIBRARY_PATH="/native:/drive/lib:${LD_LIBRARY_PATH:-}"
export TURTLEBOT3_MODEL=waffle
export GAZEBO_MODEL_PATH=/opt/ros/humble/share/turtlebot3_gazebo/models:/usr/share/gazebo-11/models
export GAZEBO_MODEL_DATABASE_URI=''
dpkg-query -W > /output/packages.txt
python3 - <<'PY'
from pathlib import Path
import json
import uuid
import yaml
import xml.etree.ElementTree as ET
from robot_harness_nav2._profile import PROFILE_ID, configure_controller
scopes = dict(a_scope=uuid.uuid4().hex, b_scope=uuid.uuid4().hex)
Path('/output/producer-context.json').write_text(json.dumps(scopes))
params = yaml.safe_load(Path('/opt/ros/humble/share/nav2_bringup/params/nav2_params.yaml').read_text())
configure_controller(params)
Path('/output/navigation-profile.json').write_text(json.dumps(dict(profile=PROFILE_ID,
    controller=params['controller_server']['ros__parameters']['FollowPath'])))
amcl = params['amcl']['ros__parameters']
amcl['set_initial_pose'] = True
amcl['initial_pose'] = dict(x=-2.0, y=-0.5, z=0.0, yaw=0.0)
nav = params['bt_navigator']['ros__parameters']
nav['plugin_lib_names'].extend(['scoped_follow_path', 'scoped_compute_path'])
for stage, scope in zip(('A', 'B'), scopes.values()):
    path = '/output/request-'+scope+'.xml'
    Path(path).write_text('<root main_tree_to_execute="MainTree"><BehaviorTree ID="MainTree"><Sequence>'
        '<ScopedComputePathToPose goal="{goal}" path="{path}" planner_id="GridBased" scope_id="'+scope+'"/>'
        '<ScopedFollowPath path="{path}" controller_id="FollowPath" scope_id="'+scope+'"/>'
        '</Sequence></BehaviorTree></root>')
    nav['default_nav_to_pose_bt_xml'] = path
    Path('/output/'+('native' if stage == 'A' else 'b')+'-params.yaml').write_text(yaml.safe_dump(params))
model = ET.parse('/opt/ros/humble/share/nav2_bringup/worlds/waffle.model')
plugins = [p for p in model.getroot().iter('plugin') if p.get('filename') == 'libgazebo_ros_diff_drive.so']
if len(plugins) != 1:
    raise RuntimeError('expected one drive outlet')
plugins[0].set('filename', 'libscoped_diff_drive.so')
ET.SubElement(plugins[0], 'generation_mode').text = 'true'
model.write('/output/scoped-waffle.model', encoding='unicode')
PY
# The launcher owns the scene and separate example process. The installed owner
# creates only its native B context and its private local request endpoint.
owner_pid='' client_pid='' launch_pid='' bridge_pid=''
cleanup() {
  local status=$?
  trap - EXIT
  for pid in "$owner_pid" "$client_pid" "$launch_pid" "$bridge_pid"; do
    [[ -n $pid ]] || continue
    kill -INT "$pid" 2>/dev/null || true
  done
  local until=$((SECONDS+4)) live
  while (( SECONDS < until )); do
    live=0
    for pid in "$owner_pid" "$client_pid" "$launch_pid" "$bridge_pid"; do
      [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null && live=1
    done
    (( live )) || break
    sleep .1
  done
  for pid in "$owner_pid" "$client_pid" "$launch_pid" "$bridge_pid"; do
    [[ -n $pid ]] || continue
    kill -KILL "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  done
  exit "$status"
}
trap cleanup EXIT
rviz_options=(use_rviz:=False)
if [[ ${M6_SHOW_VIEW:-0} == 1 ]]; then
  [[ -n ${DISPLAY:-} ]] || exit 2
  python3 /simulation/configure_view.py
  python3 - <<'VIEW'
from pathlib import Path
import yaml
p = Path('/output/live-view.rviz')
v = yaml.safe_load(p.read_text())
v['Visualization Manager']['Views']['Current'].update(Scale=250, X=-.7, Y=-.5)
p.write_text(yaml.safe_dump(v, sort_keys=False))
VIEW
  rviz_options=(use_rviz:=True rviz_config_file:=/output/live-view.rviz)
fi
ros2 launch /simulation/native_worker/producer_scope.launch.py headless:=True "${rviz_options[@]}" \
  use_sim_time:=True use_respawn:=False use_composition:=False \
  params_file:=/output/native-params.yaml robot_sdf:=/output/scoped-waffle.model \
  > /output/launch.log 2>&1 &
launch_pid=$!
python3 /simulation/producer_bridge.py > /output/producer-bridge.jsonl 2>&1 &
bridge_pid=$!
owner_options=()
if [[ -n ${M6_CALLER_WAIT_SECONDS:-} ]]; then
  owner_options=(--caller-wait-seconds "$M6_CALLER_WAIT_SECONDS")
fi
python3 -m robot_harness_nav2 "${owner_options[@]}" > /output/caller.log 2>&1 &
owner_pid=$!
# Readiness only exposes the owner endpoint; it cannot create Core authority.
until=$((SECONDS+160))
while [[ ! -f /output/navigation-endpoint.json ]]; do
  kill -0 "$owner_pid" "$launch_pid" "$bridge_pid"
  (( SECONDS < until )) || exit 1
  sleep .1
 done
endpoint=$(python3 -c 'import json; print(json.load(open("/output/navigation-endpoint.json"))["endpoint"])')
python3 /navigation-example.py "$endpoint" > /output/request-client.log 2>&1 &
client_pid=$!
wait "$client_pid"
python3 - <<'PYCLIENT'
import json, time
with open('/output/caller.jsonl', 'a') as stream:
    stream.write(json.dumps(dict(event='request_client_exit', returncode=0, steady=time.monotonic()))+'\n')
PYCLIENT
wait "$owner_pid"
kill -0 "$launch_pid" "$bridge_pid"
python3 - <<'PYSTATUS'
# Process completion only. Physical/Core qualification is outside the launcher.
import json
from pathlib import Path
Path('/output/verification.json').write_text(json.dumps(dict(passed=True, case='installed-nav2-session')))
PYSTATUS
