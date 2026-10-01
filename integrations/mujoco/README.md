# Experimental MuJoCo session backend

`robot_harness_mujoco.AlohaBackend` connects the optional
[Python session](../../bindings/python/README.md) to the stepped
`gym_aloha/AlohaTransferCube-v0` environment. The host owns the simulator, recording
and candidate queue. A separately supplied ACT worker receives RGB/joints and
returns candidates; model loading and task strategy stay outside this repository.
This is one qualified combination, not a generic MuJoCo robot adapter.

The current local qualification uses macOS arm64, Python3.12, MuJoCo3.8.1,
gym-aloha0.1.4 (revision `bd3325740ea8d1c97411c41ea1e0f4ce0a7de8da`),
gymnasium1.3.0, numpy2.2.6 and imageio2.37.4/imageio-ffmpeg0.6.0.
The external worker uses LeRobot revision
`e595b7902714ba51f91e47523f66f89c5181b649`, torch2.11.0 and the locally
migrated `lerobot/act_aloha_sim_transfer_cube_human` checkpoint at revision
`ba73b2766f1371cdc133ca4efb97eb090d744625`. These source identities describe
qualification, not a supported dependency matrix. The research consumer retains
its baseline; CMake does not install model dependencies.
The first [Robot Agent delivery](https://github.com/xiao-yang25/robot-agent/pull/1)
now provides a bundled worker, Mac dependency lock and explicit weight preparation
in its [pinned setup tutorial](https://github.com/xiao-yang25/robot-agent/blob/45c94dea5ea15aece6175c253cdd6586dd359821/skills/aloha/README.md).
That installed Mac application passed its local normal path with new prepared
assets, reusing the qualified Harness installation. The first consumer increment
is merged as Agent06cbee4, with this runtime pinned at3acc9aa. It remains an
experimental combination, not a stable standalone skill interface or clean-machine
reproduction. Linux Ubuntu22.04/aarch64 CPU ACT and OSMesa passed the deterministic seed0
400+50-step path with a fresh environment/Harness installation,451 decoded
frames, two correlated settled receipts and process reaping. The same-run fixed
physical evaluator passed the bounded one-second hold. A separate installed Linux
visual application subsequently passed three real camera-based decisions, the
same400+50-step task, full recording, process exits and its own fixed physical
evaluation. Both results cover only the selected normal scope.
The portable session fixture needs none of these dependencies.

## Consumer configuration

After building with `ROBOT_HARNESS_BUILD_PYTHON=ON`, use the matching interpreter
in an environment containing the qualified dependencies. The following values
are caller-supplied absolute paths, not files shipped by this package:

```python
from robot_harness import Session

with Session(startup_timeout=90, mujoco={
    "output": "/absolute/new-evidence-directory",
    "worker_script": "/absolute/session_worker.py",
    "checkpoint": "/absolute/migrated-checkpoint",
    "device": "mps",
    "seed": 0,
}) as session:
    observation = session.observe()  # RGB bytes plus camera/joints and sequence
    submission = session.submit("trial-0", steps=400)
    # Poll status("trial-0") until finished; retain its result and Core receipt.
    # Then session.reset(seed=1) permits the next trial in the same host/worker.
```

Configuration is trusted local startup input, not a remotely supplied plugin
specification. The worker executable runs as the same local user. The output
directory must be new; existing evidence is never overwritten. Every episode
records the reset frame, action attempts before submission, post-step physical
facts, video and an execution summary. Ground-truth cube/contact facts stay in
these evaluator traces; the observation API exposes camera/joints only. After a
step error, observations are marked invalid and image sampling is refused.
`steps` counts confirmed returns; an errored call can already have had an effect,
which its best-effort error trace preserves.

The skill is `aloha.transfer_cube_trial`, at most400 native steps. Actions are14
absolute joint/gripper targets, validated in their native float32 representation.
No blanket normalized clipping is applied. An ACT chunk contains at most100
candidates; the host checks Core before each single native submission.

A native termination satisfying the environment criterion yields
`native_condition_reached`; exhausting the requested step budget yields
`step_limit`, native failure and no successful output. Full handoff and stable
holding remain `unassessed`: native reward4 does not prove right-hand release.
Cancellation freezes further stepped simulation once processed by the host;
physics is paused while awaiting inference. Settlement uses the explicitly scoped
`mujoco-stepped-episode-v1` closure facts, not a physical stop or grasp guarantee.
Native-step stalls still stall the owner. Explicit reset is required after a
settled episode; a failed reset makes the session unavailable.

## Validation

The local integration ran original seeds0–4 in one host/model process, matched
all original actions and simulator traces exactly, retained all videos and
confirmed process reaping. Actual simulation checks cover a3-step budget,
nonempty-queue cancellation, and an error after an actual native effect.
These are bounded integration evidence, not task reliability estimates.

Run the backend's lightweight numeric/cleanup tests without a model or renderer:

```sh
PYTHONPATH="$PWD/build-python/python" python3 -m unittest discover \
  -s integrations/mujoco/tests -v
```

Only NumPy is needed for those tests. They and the standard session suite passed
in a local Ubuntu22.04/Python3.10 container with distro NumPy1.21.5. The optional
Ubuntu job includes the same checks; it does not run ACT or MuJoCo. Hosted workflow
results are tracked in PR checks; those hosted fixtures do not establish Linux
model/render results. The separate local Linux normal qualification above has
its own scope. See
[Testing](../../docs/TESTING.md#first-runtime-slice) for current evidence boundaries.


## Continuous handoff

Select `mujoco-stepped-handoff-v1` in the trusted local `mujoco` configuration.
`AlohaHandoffBackend` then owns native dm_control continuation: it records reward4
as a milestone and executes the fixed400-step transfer segment without applying
Gym's legacy terminal mapping. After normal delivered settlement, it preserves
the episode for fresh RGB/joints and a separately admitted50-step hold.
The legacy `mujoco-stepped-episode-v1` remains the default.

The [Session interface](../../bindings/python/README.md#continuous-skills) requires
explicit expected epoch/sequence references. Stage/resource/target checks govern
hold admission; cube contact/pose truth remains evaluator-only recording data.
A mistaken caller may hold a dropped cube target; the independent task evaluator
must report that failure instead of Runtime quietly filtering it out.

Use the [deterministic caller](../../examples/mujoco_handoff.py) with the same
qualified dependencies and externally supplied worker/checkpoint:

```sh
PYTHONPATH="$PWD/build-python/python" python3 examples/mujoco_handoff.py \
  --output /absolute/new-handoff-run \
  --worker-script /absolute/session_worker.py \
  --checkpoint /absolute/migrated-checkpoint --device mps --seed 0
```

It performs both operations, checks continuous observations/settled receipts,
rejects a stale hold reference, resets explicitly after hold, and closes/reaps the
processes. It saves a plan and partial/error/final reports alongside episode
recordings. Its status says whether the bounded execution checks completed;
it does not report independently evaluated business success. It unconditionally
selects hold as a deterministic baseline, not an observation-driven Agent.

A normal episode records450 returned steps and451 video frames. Per-operation
summaries retain400/50 counts, epoch, skill and Core operation IDs; observation
sequence and the raw trace count the whole episode. Transfer recording stays open
through hold; close it before viewing the complete file. This profile is experimental
and locally qualified only as described in [Testing](../../docs/TESTING.md#continuous-handoff-profile).
The separate Robot Agent application adds observation-driven skill selection and
its own preparation tutorial. Its visual assessment does not prove the physical
task predicate. The deterministic Linux CPU/model/render path above passed; the
separate Linux visual normal task also passed. Selected live failure cases and
combination-wide delivery closeout remain subsequent work. No dependencies or model
assets are installed by CMake.
