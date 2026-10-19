#!/usr/bin/env bash
set -eo pipefail
[[ -f /.dockerenv && ${M6_NATIVE_ISOLATED:-} == 1 ]] || exit 2
mkdir /output/session-started
mkdir -p "$HOME"
source /opt/ros/humble/setup.bash
source /drive/share/m4_drive_probe/local_setup.bash
source /simulation/navigation_startup.sh
source /simulation/navigation_cleanup.sh
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
import os
import uuid
import yaml
import xml.etree.ElementTree as ET
from robot_harness_nav2._profile import PROFILE_ID, PERMISSION_PROFILE_ID, FAILURE_PROFILE_ID, PROFILES, configure_controller
profile = os.environ.get("M6_NAVIGATION_PROFILE", PROFILE_ID)
if profile not in PROFILES:
    raise RuntimeError("unknown navigation profile")
scopes = dict(a_scope=uuid.uuid4().hex, b_scope=uuid.uuid4().hex)
Path('/output/producer-context.json').write_text(json.dumps(scopes))
params = yaml.safe_load(Path('/opt/ros/humble/share/nav2_bringup/params/nav2_params.yaml').read_text())
configure_controller(params)
Path('/output/navigation-profile.json').write_text(json.dumps(dict(profile=profile,
    controller=params['controller_server']['ros__parameters']['FollowPath'])))
amcl = params['amcl']['ros__parameters']
amcl['set_initial_pose'] = True
amcl['initial_pose'] = dict(x=-2.0, y=-0.5, z=0.0, yaw=0.0)
nav = params['bt_navigator']['ros__parameters']
nav['plugin_lib_names'].extend(['scoped_follow_path', 'scoped_compute_path'])
failure_proof = profile == FAILURE_PROFILE_ID
if failure_proof:
    nav['plugin_lib_names'].extend(['failure_follow_path', 'failure_compute_path'])
    nav['failure_proof_enabled'] = True
    params['controller_server']['ros__parameters']['failure_proof_enabled'] = True
for stage, scope in zip(('A', 'B'), scopes.values()):
    if failure_proof:
        params['controller_server']['ros__parameters'].update(failure_scope_id=scope,
            failure_generation=1 if stage == 'A' else 2)
    path = '/output/request-'+scope+'.xml'
    planner = 'FailureComputePathToPose' if failure_proof else 'ScopedComputePathToPose'
    controller = 'FailureFollowPath' if failure_proof else 'ScopedFollowPath'
    Path(path).write_text('<root main_tree_to_execute="MainTree"><BehaviorTree ID="MainTree"><Sequence>'
        '<'+planner+' goal="{goal}" path="{path}" planner_id="GridBased" scope_id="'+scope+'"/>'
        '<'+controller+' path="{path}" controller_id="FollowPath" scope_id="'+scope+'"/>'
        '</Sequence></BehaviorTree></root>')
    nav['default_nav_to_pose_bt_xml'] = path
    Path('/output/'+('native' if stage == 'A' else 'b')+'-params.yaml').write_text(yaml.safe_dump(params))
model = ET.parse('/opt/ros/humble/share/nav2_bringup/worlds/waffle.model')
plugins = [p for p in model.getroot().iter('plugin') if p.get('filename') == 'libgazebo_ros_diff_drive.so']
if len(plugins) != 1:
    raise RuntimeError('expected one drive outlet')
plugins[0].set('filename', 'libscoped_diff_drive.so')
ET.SubElement(plugins[0], 'generation_mode').text = 'true'
if profile == PERMISSION_PROFILE_ID:
    ET.SubElement(plugins[0], 'owner_permission_mode').text = 'true'
model.write('/output/scoped-waffle.model', encoding='unicode')
PY
# The launcher owns the scene and separate example process. The installed owner
# creates only its native B context and its private local request endpoint.
owner_pid='' client_pid='' launch_pid='' bridge_pid='' collector_pid='' display_pid=''
session_completed=0
cleanup() {
  local status=$?
  trap - EXIT
  trap '' INT TERM
  # Preserve scene/bridge until Owner finishes: abort 2s, terminal query <=10s,
  # context-process reap <=6s, plus 1s supervisor margin. No stop guarantee.
  [[ -z $client_pid ]] || kill -INT "$client_pid" 2>/dev/null || true
  stop_navigation_owner "$owner_pid" 19 /output/owner-process.json
  local deadline failed=0
  deadline=$(python3 -c 'import time; print(time.monotonic()+4)')
  if [[ -n $collector_pid ]]; then
    # Observation can finish only after Owner's native cleanup window.
    python3 /simulation/navigation_groups.py /output/collector-process.json "$$" "$deadline" \
      "collector=$collector_pid" || failed=1
    local code=0 joined=0
    if ! kill -0 "$collector_pid" 2>/dev/null; then
      wait "$collector_pid" || code=$?
      joined=1
    else
      failed=1
    fi
    python3 - "$code" "$joined" <<'COLLECTOR_EXIT' || failed=1
import json, sys
from pathlib import Path
path = Path('/output/collector-process.json')
result = json.loads(path.read_text())
fact = result['groups'][0]
fact.update(returncode=int(sys.argv[1]), group_forced=fact['forced'],
            reaped=sys.argv[2] == '1', errors=result['errors'])
path.write_text(json.dumps(fact, indent=2) + '\n')
COLLECTOR_EXIT
  fi
  # B inherits Owner's group. Reap all known groups even after their leader exits.
  local groups=() reaped=() role pid code joined
  for role in client launch bridge owner; do
    case $role in client) pid=$client_pid;; launch) pid=$launch_pid;;
      bridge) pid=$bridge_pid;; owner) pid=$owner_pid;; esac
    [[ -n $pid ]] && groups+=("$role=$pid")
  done
  python3 /simulation/navigation_groups.py /output/launcher-processes.json "$$" "$deadline" \
    "${groups[@]}" || failed=1
  for pair in "${groups[@]}"; do
    pid=${pair#*=} code=0 joined=0
    if ! kill -0 "$pid" 2>/dev/null; then
      wait "$pid" 2>/dev/null || code=$?
      joined=1
    else
      failed=1
    fi
    reaped+=("${pair%%=*}:$code:$joined")
  done
  python3 - "${reaped[@]}" <<'GROUP_EXIT' || failed=1
import json, sys
from pathlib import Path
path = Path('/output/launcher-processes.json')
result = json.loads(path.read_text())
exits = {role: dict(returncode=int(code), reaped=joined == '1')
         for role, code, joined in (arg.split(':') for arg in sys.argv[1:])}
for fact in result['groups']:
    fact.update(exits[fact['role']])
result['clean'] = result['clean'] and all(f['reaped'] for f in result['groups'])
path.write_text(json.dumps(result, indent=2) + '\n')
GROUP_EXIT
  stop_navigation_display "${display_pid:-}" /output/display-process.json /simulation/navigation_groups.py || failed=1
  if (( failed && status == 0 )); then status=1; fi
  if (( status == 0 && ${session_completed:-0} )); then
    # Local process closure only, never physical/Core success.
    python3 - <<'PYSTATUS' || status=1
import json
from pathlib import Path
Path('/output/verification.json').write_text(json.dumps(dict(passed=True, case='installed-nav2-session')))
PYSTATUS
  fi
  exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
scene=${M6_NAVIGATION_SCENE:-normal}
python3 /simulation/navigation_scene.py "$scene"
map_file=$(python3 -c 'import json; print(json.load(open("/output/navigation-scene.json"))["map_file"])')
collection_options=()
world_file=/opt/ros/humble/share/nav2_bringup/worlds/world_only.model
if [[ -n ${M6_INSPECTION_CASE:-} ]]; then
  command -v Xvfb >/dev/null || { echo 'Inspection camera requires the visual image' >&2; exit 2; }
  python3 /simulation/inspection_scene.py "$M6_INSPECTION_CASE"
  world_file=/output/inspection.world
  collection_options=(world:="$world_file")
  export DISPLAY=:96 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2
  setsid Xvfb :96 -screen 0 1600x900x24 -nolisten tcp > /output/display.log 2>&1 &
  display_pid=$!
  until=$((SECONDS+4))
  while [[ ! -S /tmp/.X11-unix/X96 ]]; do
    navigation_processes_alive display "$display_pid" || exit 1
    (( SECONDS < until )) || { echo 'Inspection display readiness timed out' >&2; exit 1; }
    sleep .05
  done
  navigation_processes_alive display "$display_pid" || exit 1
fi
if [[ ${M6_COLLECT_EVALUATION:-0} == 1 ]]; then
  # These are the installed default values, made explicit for coordinate evidence.
  collection_options=(world:="$world_file"
    x_pose:=-2.0 y_pose:=-0.5 z_pose:=0.01 roll:=0.0 pitch:=0.0 yaw:=0.0 robot_name:=turtlebot3_waffle)
  python3 /simulation/navigation_truth.py --prepare > /output/collection-prepare.log 2>&1 || true
fi
rviz_options=(use_rviz:=False)
if [[ ${M6_SHOW_VIEW:-0} == 1 ]]; then
  [[ -n ${DISPLAY:-} ]] || exit 2
  python3 /simulation/configure_view.py
  python3 - <<'VIEW'
from pathlib import Path
import yaml
import os
p = Path('/output/live-view.rviz')
v = yaml.safe_load(p.read_text())
if os.environ.get('M6_NAVIGATION_PROFILE') == 'scoped-two-context-nav2-owner-permit-v1':
    v['Visualization Manager']['Global Options']['Frame Rate'] = 5
v['Visualization Manager']['Views']['Current'].update(Scale=250, X=-.7, Y=-.5)
p.write_text(yaml.safe_dump(v, sort_keys=False))
VIEW
  rviz_options=(use_rviz:=True rviz_config_file:=/output/live-view.rviz)
fi
setsid ros2 launch /simulation/native_worker/producer_scope.launch.py headless:=True "${rviz_options[@]}" \
  use_sim_time:=True use_respawn:=False use_composition:=False "${collection_options[@]}" \
  params_file:=/output/native-params.yaml map:="$map_file" robot_sdf:=/output/scoped-waffle.model \
  > /output/launch.log 2>&1 &
launch_pid=$!
setsid python3 /simulation/producer_bridge.py > /output/producer-bridge.jsonl 2>&1 &
bridge_pid=$!
if [[ ${M6_COLLECT_EVALUATION:-0} == 1 ]]; then
  # A separate group contains only this collector and its passive query children.
  setsid python3 /simulation/navigation_truth.py > /output/collector.log 2>&1 &
  collector_pid=$!
fi
display_options=()
[[ -z $display_pid ]] || display_options=(display "$display_pid")
owner_options=(--scene "$scene")
if [[ -n ${M6_INSPECTION_CASE:-} ]]; then
  owner_options+=(--inspection-case "$M6_INSPECTION_CASE")
fi
if [[ -n ${M6_NAVIGATION_PROFILE:-} ]]; then
  owner_options+=(--profile "$M6_NAVIGATION_PROFILE")
fi
if [[ -n ${M6_CALLER_WAIT_SECONDS:-} ]]; then
  owner_options+=(--caller-wait-seconds "$M6_CALLER_WAIT_SECONDS")
fi
if [[ -n ${M6_TERMINAL_QUERY_SECONDS:-} ]]; then
  owner_options+=(--terminal-query-seconds "$M6_TERMINAL_QUERY_SECONDS")
fi
setsid python3 -m robot_harness_nav2 "${owner_options[@]}" > /output/caller.log 2>&1 &
owner_pid=$!
# Readiness only exposes the owner endpoint; it cannot create Core authority.
wait_navigation_endpoint /output/navigation-endpoint.json 160 \
  owner "$owner_pid" scene "$launch_pid" bridge "$bridge_pid" "${display_options[@]}"
endpoint=$(python3 -c 'import json; print(json.load(open("/output/navigation-endpoint.json"))["endpoint"])')
PYTHONPATH="${M6_CLIENT_PREFIX:+${M6_CLIENT_PREFIX}:}${PYTHONPATH:-}" \
  setsid python3 /navigation-example.py "$endpoint" > /output/request-client.log 2>&1 &
client_pid=$!
wait "$client_pid"
python3 - <<'PYCLIENT'
import json, time
with open('/output/caller.jsonl', 'a') as stream:
    stream.write(json.dumps(dict(event='request_client_exit', returncode=0, steady=time.monotonic()))+'\n')
PYCLIENT
wait "$owner_pid"
navigation_processes_alive scene "$launch_pid" bridge "$bridge_pid" "${display_options[@]}"
session_completed=1
