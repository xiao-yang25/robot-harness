"""One stdio connection owns one serialized client to the actual Runtime host."""

from contextlib import asynccontextmanager
import functools
import io
import json
from pathlib import Path
import time
from typing import Annotated, Any

import anyio
from mcp.server.fastmcp import FastMCP, Image
from mcp.types import TextContent, ToolAnnotations
from pydantic import Field

from robot_harness import Session
from .config import Config, bounded_integer


class Bridge:
    def __init__(self, output, *, config=None, session_factory=Session,
                 max_trials=3, max_tool_calls=80):
        self.max_trials = bounded_integer(max_trials, 'max_trials', 1, 64)
        self.max_tool_calls = bounded_integer(max_tool_calls, 'max_tool_calls', 1, 10000)
        self.config = config if config is not None else Config()
        self.output = Path(output).resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.trace = (self.output / 'tools.jsonl').open('x')
        self.session_factory = session_factory
        self.session = None
        self.open_attempted = False
        self.closing = False
        self.closed = False
        self.submissions = set()
        self.calls = 0
        self.lock = anyio.Lock()

    def record(self, event):
        self.trace.write(json.dumps({'time_ns': time.monotonic_ns(), **event}, allow_nan=False) + '\n')
        self.trace.flush()

    async def call(self, name, **arguments):
        # A cancelled request waiting for the client must not start new effects.
        async with self.lock:
            try:
                # Once a synchronous call starts, finish it before releasing the
                # non-thread-safe client, even if the MCP request is cancelled.
                with anyio.CancelScope(shield=True):
                    self.calls += 1
                    sequence = self.calls
                    if sequence > self.max_tool_calls and name != 'close':
                        raise RuntimeError('tool-call budget exhausted; close the session')
                    trace_error = None
                    try:
                        self.record({'event': 'call', 'sequence': sequence, 'tool': name, 'arguments': arguments})
                    except Exception as error:
                        if name != 'close':
                            raise
                        # A failed evidence sink must not prevent owned cleanup.
                        trace_error = error
                    try:
                        result = await anyio.to_thread.run_sync(functools.partial(self.invoke, name, **arguments))
                        if name == 'observe':
                            metadata, png = result
                            if png is not None:
                                metadata['image_file'] = f'observation-{sequence}.png'
                                (self.output / metadata['image_file']).write_bytes(png)
                            logged = metadata
                        else:
                            logged = result
                        self.record({'event': 'result', 'sequence': sequence, 'tool': name, 'result': logged})
                        if trace_error is not None:
                            raise trace_error
                        return result
                    except Exception as error:
                        try:
                            self.record({'event': 'error', 'sequence': sequence, 'tool': name,
                                         'error': f'{type(error).__name__}: {error}'})
                        except Exception:
                            # Preserve the execution/cleanup error over another log failure.
                            pass
                        raise
            finally:
                # Deliver request cancellation after the client call finishes.
                # Otherwise SDK1.30 can attempt a second response to a cancelled request.
                await anyio.lowlevel.checkpoint()

    def invoke(self, name, **arguments):
        if name == 'close':
            self.closing = True
            if self.session is not None:
                self.session.close()
            self.closed = True
            return {'closed': True, 'owned_processes_reaped': self.session is not None}
        if self.closing:
            raise RuntimeError('session closing or closed')
        if name == 'open':
            if self.session is not None:
                return self.session.status()
            if self.open_attempted:
                raise RuntimeError('startup previously failed; outcome must be investigated')
            self.open_attempted = True
            self.session = self.session_factory(**self.config.session_options(self.output))
            return self.session.status()
        if self.session is None:
            raise RuntimeError('open_session first')
        if name == 'capabilities':
            result = self.session.capabilities()
            result['reset_required_between_trials'] = True
            if result['skill'] == 'aloha.transfer_cube_trial':
                result.update(
                    description='Attempt the pretrained ALOHA cube transfer from its reset distribution.',
                    native_condition='Left finger contacts cube off the table; right-hand release is not required.',
                    unsupported=['guaranteed full handoff', 'verified stable holding', 'arbitrary-state retry'])
            elif result['skill'] == 'fixture.increment':
                result.update(description='Deterministic counter fixture; no robot or model.',
                              native_condition='The requested number of increments completed.',
                              unsupported=['robot motion', 'manipulation'])
            return result
        if name == 'observe':
            observation = self.session.observe()
            rgb = observation.pop('rgb', b'')
            if not rgb:
                if self.config.backend != 'fixture':
                    raise ValueError('missing camera observation')
                return observation, None
            image = observation.get('image', {})
            shape = image.get('shape', [])
            if (observation.get('valid') is not True or image.get('dtype') != 'uint8' or
                    len(shape) != 3 or any(type(v) is not int or v <= 0 for v in shape) or
                    shape[2] != 3 or len(rgb) != shape[0] * shape[1] * 3 or
                    len(rgb) > 640 * 480 * 3):
                raise ValueError('invalid camera observation')
            from PIL import Image as PILImage
            png = io.BytesIO()
            PILImage.frombytes('RGB', (shape[1], shape[0]), rgb).save(png, format='PNG')
            observation['age_ms'] = (time.monotonic_ns() - observation['observed_at_ns']) / 1e6
            observation['age_note'] = 'Time since sample; stepped physics pauses while awaiting inference.'
            return observation, png.getvalue()
        if name == 'submit':
            skill = arguments.pop('skill')
            if skill != self.session.capabilities()['skill']:
                raise ValueError(f'unsupported skill: {skill}')
            request_id = arguments['request_id']
            if request_id not in self.submissions:
                if len(self.submissions) >= self.max_trials:
                    raise ValueError('trial budget exhausted')
                # Refused/uncertain submissions also consume a distinct-ID slot.
                self.submissions.add(request_id)
            return self.session.submit(**arguments)
        if name == 'status':
            return self.session.status(**arguments)
        if name == 'cancel':
            return self.session.cancel(**arguments)
        if name == 'reset':
            return self.session.reset(**arguments)
        raise ValueError('unsupported operation')

    async def shutdown(self):
        with anyio.CancelScope(shield=True):
            try:
                await self.call('close')
            finally:
                self.trace.close()


def make_server(bridge):
    @asynccontextmanager
    async def lifespan(server):
        try:
            yield
        finally:
            await bridge.shutdown()

    mcp = FastMCP('robot-session', lifespan=lifespan, log_level='WARNING', instructions=(
        'Use one persistent local Runtime session. Open, inspect capabilities and actual '
        'observation before submitting a supported skill. Submit returns promptly; inspect '
        'status later. Explicit reset after settlement permits another trial. Native success '
        'is not proof of unsupported task guarantees. Close when done. Settlement is not task success.'))
    read = ToolAnnotations(readOnlyHint=True, destructiveHint=False, openWorldHint=False)
    write = ToolAnnotations(readOnlyHint=False, destructiveHint=False, openWorldHint=False)
    request_id_type = Annotated[str, Field(min_length=1, max_length=128)]

    @mcp.tool(annotations=write)
    async def open_session() -> dict[str, Any]:
        """Open the operator-configured local session. Repeated calls reuse it; no skill is submitted."""
        return await bridge.call('open')

    @mcp.tool(annotations=read)
    async def capabilities() -> dict[str, Any]:
        """Read actual skill availability, its budget, native condition and unsupported guarantees."""
        return await bridge.call('capabilities')

    @mcp.tool(annotations=read, structured_output=False)
    async def observe() -> list:
        """Read actual latest measurements; camera backends also return an image with epoch/sequence/age."""
        metadata, png = await bridge.call('observe')
        content = [TextContent(type='text', text=json.dumps(metadata))]
        if png is not None:
            content.append(Image(data=png, format='png'))
        return content

    @mcp.tool(annotations=write)
    async def submit(request_id: request_id_type, skill: str,
                     steps: Annotated[int, Field(strict=True, ge=1, le=400)] = 400,
                     deadline_ms: Annotated[int, Field(strict=True, ge=1, le=60000)] = 30000) -> dict[str, Any]:
        """Submit a supported skill. Same ID/arguments returns its record; conflicts fail. Query uncertain outcomes before retrying."""
        return await bridge.call('submit', request_id=request_id, skill=skill,
                                 steps=steps, deadline_ms=deadline_ms)

    @mcp.tool(annotations=read)
    async def status(request_id: request_id_type | None = None) -> dict[str, Any]:
        """Read progress or a retained operation, with separate native outcome, output and settlement."""
        return await bridge.call('status', request_id=request_id)

    @mcp.tool(annotations=write)
    async def cancel(request_id: request_id_type) -> dict[str, Any]:
        """Request robot-task cancellation. This reply is not settlement; query status afterward."""
        return await bridge.call('cancel', request_id=request_id)

    @mcp.tool(annotations=write)
    async def reset(seed: Annotated[int, Field(strict=True, ge=0, le=4)] | None = None) -> dict[str, Any]:
        """Explicitly reset after settlement, retaining the host/worker and old receipts. ALOHA seeds: 0–4."""
        return await bridge.call('reset', seed=seed)

    @mcp.tool(annotations=write)
    async def close_session() -> dict[str, Any]:
        """Close and confirm owned process reaping. Retry explicitly if a previous close was uncertain."""
        return await bridge.call('close')

    return mcp
