# Local Runtime MCP integration

Use an existing tool-using agent, such as Codex, to inspect and operate one
Core-backed [Python Session](../../bindings/python/README.md). The server provides
local stdio tools; it owns one Session client, while the existing host and policy
worker continue progressing separately. This is experimental source packaging,
not a published plugin, remote service or stable cross-version API.

## Two Agent paths

| Consumer | Owns | Runtime connection |
|---|---|---|
| Existing general-purpose agent, such as Codex | Its model/context and decisions using the available tools | This optional MCP module |
| Embodied business Agent we implement for a concrete scenario | Business goal, task state, interpretation of continuous observations, skill selection, memory and bounded recovery | Direct Python/native interfaces, or MCP when useful |
| ACT policy worker | Observation-to-joint-action prediction | Existing host-owned policy lane; neither a business Agent nor a tool-using task Agent |

The first path validates tool consumption. It does not deliver the second path,
whose task logic and application packaging belong outside Robot Harness. Both
consume the same execution/observation/receipt semantics. Native control and device
protection remain below Runtime; neither Agent can allocate Core authority.

## First run without a model or simulator

Requires POSIX, CMake3.18+, a C++17 compiler, Python3.10+ with development headers,
and the optional SDK/image dependencies. Core builds remain independent of them.
From this repository:

```sh
python3 -m venv build-mcp-venv
build-mcp-venv/bin/python -m pip install -r integrations/mcp/requirements.txt
cmake -S . -B build-mcp -DROBOT_HARNESS_BUILD_PYTHON=ON \
  -DBUILD_TESTING=ON -DPython3_EXECUTABLE="$PWD/build-mcp-venv/bin/python"
cmake --build build-mcp --target _core --parallel 2
PYTHONPATH="$PWD/build-mcp/python" build-mcp-venv/bin/python \
  examples/mcp_session.py --output "$PWD/build-mcp/example-01"
```

Expect `status: completed`, two operations with values7 and4, accepted output and
settled receipts, the same host/worker across explicit reset, and confirmed close.
The example uses the real SDK/stdin/stdout/Core path. It is a deterministic caller,
not a live model result. It generates `report.json`, `server/tools.jsonl` and stderr
logs in the new output directory. Use a different output path on the next run;
existing evidence is never overwritten. Fixture observations have no robot image.

The package is copied beside `robot_harness` in the build and optional installation.
After `cmake --install`, point `PYTHONPATH` at the installed
`<prefix>/lib/robot-harness/python` (or the configured `CMAKE_INSTALL_LIBDIR`). The
matching interpreter must have the SDK packages installed; copying source does
not install dependencies or provide a cross-version binary ABI.

## Configure the ALOHA simulation

Prepare the [qualified backend dependencies](../mujoco/README.md) and supply your
ACT worker and checkpoint. The product does not download weights or install
LeRobot. Save an operator-owned JSON file, with absolute paths:

```json
{
  "backend": "mujoco",
  "worker_script": "/absolute/skill/session_worker.py",
  "checkpoint": "/absolute/models/migrated",
  "device": "mps",
  "seed": 0,
  "startup_timeout": 90
}
```

Run the same SDK example using the matching skill environment/interpreter:

```sh
PYTHONPATH="/absolute/build-mcp/python" /absolute/skill-env/bin/python \
  /absolute/robot-harness/examples/mcp_session.py \
  --config /absolute/aloha.json --output /absolute/new-mcp-example
```

It performs two400-step-budget trials, resetting to seed1 between them. Results
and videos are under `server/episodes/`; four real camera samples become PNGs in
`server/`. Native-condition success is limited to the declared upstream criterion;
full right-hand release and stable holding remain unassessed. `device` selects the
external worker device, not proof that a different GPU/OS combination is qualified.
The current model/rendering qualification remains macOS arm64/MPS.

Without `--config`, the module selects the counter fixture. An explicit fixture
configuration may set `worker_delay_ms` and `startup_timeout`. Unknown optional
metadata is ignored; required backend names, path types and consumed values are
validated. There is no arbitrary backend import or remote executable tool.

## Connect Codex or another stdio client

Any MCP client can launch the following server using the matching interpreter and
environment. Do not run it as an interactive application expecting terminal text;
stdout is reserved for protocol traffic, diagnostics go to stderr.

```sh
PYTHONPATH="/absolute/build-mcp/python" /absolute/skill-env/bin/python \
  -m robot_harness_mcp --config /absolute/aloha.json \
  --output /absolute/new-server-run --max-trials 2 --max-tool-calls 80
```

For Codex, adapt this local configuration example to your paths. It follows the
[official MCP configuration](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).
The fixture needs no `--config` argument or simulator environment.

```toml
[mcp_servers.robot]
command = "/absolute/skill-env/bin/python"
args = ["-m", "robot_harness_mcp", "--config", "/absolute/aloha.json", "--output", "/absolute/new-server-run", "--max-trials", "2"]
startup_timeout_sec = 20
tool_timeout_sec = 120
required = true

[mcp_servers.robot.env]
PYTHONPATH = "/absolute/build-mcp/python"
```

Each server process needs a fresh output path, including a newly launched
connection. A connection cannot reopen after `close_session`; start a new server
when needed. This module does not modify global client configuration, authentication
or credentials. The operator configures the consumer model and authorizes task
effects. Tool annotations are client hints, not enforcement or a sandbox.

A first task can ask the agent to inspect capabilities and an actual observation,
perform one supported trial, follow status, then report the native outcome, output
and settlement separately. Ask it to explain whether full handoff is supported.
Explicitly authorize a second trial/reset if desired. Task prompts and Agent
strategy belong to the consumer; the server itself contains no planning loop.

## Tools and cancellation

| Tool | Behavior |
|---|---|
| `open_session` | Open the fixed operator configuration; repeated calls retain the same Session |
| `capabilities` | Read actual skill/availability, native criterion and unsupported guarantees |
| `observe` | Latest measurements; camera backends return PNG plus epoch/sequence/age |
| `submit` | Supported skill, caller request ID,1–400 steps and1–60000 ms deadline |
| `status` | Session progress or a retained operation by ID |
| `cancel` | Request task revocation; follow status for native completion/settlement |
| `reset` | Explicit reset after settlement; optional ALOHA seed0–4 |
| `close_session` | Close admission and confirm owned process reaping; retry an uncertain close |

MCP request cancellation and robot-task cancellation are different. Cancelling a
queued MCP call prevents it from starting. Cancelling a call already using the
synchronous Session waits for that call to finish safely; it does not undo a
submitted operation. Query its ID and explicitly call `cancel` if required. Native
revocation starts when the host processes it, not when the model sends a request.
One already-entered synchronous native step and host stalls have no hard stop bound.

EOF closes the owned Session. Startup failure cannot silently spawn a replacement;
failed cleanup cannot claim successful close. The default bounds are three distinct
supported submission IDs and80 tool calls, adjustable by the operator to1–64 and
1–10000 respectively. Refused/uncertain submissions consume an ID slot; duplicate
IDs still reach Runtime's existing deduplication. Close remains available after
budget exhaustion. No reconnect recovery or persistent task state is provided.

## Verification

```sh
PYTHONPATH="$PWD/build-mcp/python" build-mcp-venv/bin/python \
  -m unittest discover -s integrations/mcp/tests -v
```

The tests include real stdio/Core tasks, reset/old receipts, strict pre-effect
argument rejection, queued task cancellation and EOF during active work. A
controlled startup barrier also exercises actual MCP cancellation notifications:
the started open completes without a duplicate protocol response, a cancelled
queued submit has no effect, subsequent status works and processes are reaped.
Focused checks cover failed-startup non-replacement, failed-close retry, budgets
and invalid images. They require no model or renderer. The Ubuntu optional-Python
job includes these checks and an installed/relocated MCP example. See
[Testing](../../docs/TESTING.md#first-runtime-slice) for executed evidence and limits.
