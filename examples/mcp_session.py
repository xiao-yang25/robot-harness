"""Deterministic SDK consumer: two real operations in one local MCP connection."""

import argparse
import asyncio
from datetime import timedelta
import json
import os
from pathlib import Path
import sys
import time

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def result(reply):
    if reply.isError:
        raise RuntimeError(reply)
    return reply.structuredContent


async def wait_result(client, request_id):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        state = result(await client.call_tool('status', {'request_id': request_id}))
        if state['state'] == 'finished':
            if state['receipt']['settlement'] != 'settled' or state['receipt']['output'] != 'accepted':
                raise RuntimeError(state)
            return state
        await asyncio.sleep(.3)
    raise TimeoutError(request_id)


async def run(output, config):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    arguments = ['-m', 'robot_harness_mcp', '--output', str(output / 'server'), '--max-trials', '2']
    if config is not None:
        arguments += ['--config', str(config.resolve())]
    params = StdioServerParameters(command=sys.executable, args=arguments, env=os.environ.copy())
    report = {'status': 'running', 'operations': [], 'image_samples': 0}
    try:
        with (output / 'server-stderr.log').open('w') as errlog:
            async with stdio_client(params, errlog=errlog) as (read, write):
                async with ClientSession(read, write, read_timeout_seconds=timedelta(seconds=120)) as client:
                    await client.initialize()
                    opened = result(await client.call_tool('open_session'))
                    cap = result(await client.call_tool('capabilities'))
                    report.update(session=opened, capability=cap)
                    for index in range(2):
                        if index:
                            result(await client.call_tool('reset', {'seed': 1} if cap['skill'] == 'aloha.transfer_cube_trial' else {}))
                        before = await client.call_tool('observe')
                        if before.isError:
                            raise RuntimeError(before)
                        report['image_samples'] += sum(item.type == 'image' for item in before.content)
                        steps = (7, 4)[index] if cap['skill'] == 'fixture.increment' else 400
                        request_id = f'trial-{index}'
                        result(await client.call_tool('submit', {'request_id': request_id, 'skill': cap['skill'], 'steps': steps}))
                        done = await wait_result(client, request_id)
                        report['operations'].append(done)
                        after = await client.call_tool('observe')
                        if after.isError:
                            raise RuntimeError(after)
                        report['image_samples'] += sum(item.type == 'image' for item in after.content)
                        if result(await client.call_tool('status', {'request_id': 'trial-0'})) != report['operations'][0]:
                            raise RuntimeError('first operation was not retained')
                        current = result(await client.call_tool('status'))
                        if (current['host_pid'], current['worker_pid']) != (opened['host_pid'], opened['worker_pid']):
                            raise RuntimeError('host/worker was replaced')
                    report['close'] = result(await client.call_tool('close_session'))
        report['status'] = 'completed'
    except BaseException as error:
        report.update(status='error', error=f'{type(error).__name__}: {error}')
        raise
    finally:
        (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--config', type=Path)
    args = parser.parse_args()
    asyncio.run(run(args.output, args.config))
