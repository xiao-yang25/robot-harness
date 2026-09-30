import asyncio
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

import anyio
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp.shared.message import SessionMessage
from mcp.types import JSONRPCMessage


def result(reply):
    if reply.isError:
        raise AssertionError(reply)
    return reply.structuredContent


async def finished(client, request_id):
    with anyio.fail_after(8):
        while True:
            state = result(await client.call_tool('status', {'request_id': request_id}))
            if state['state'] == 'finished':
                return state
            await anyio.sleep(.02)


async def processes_absent(opened):
    for pid in (opened['host_pid'], opened['worker_pid']):
        with anyio.fail_after(8):
            while True:
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    break
                await anyio.sleep(.02)


class ProtocolTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.errlog = (self.root / 'stderr.log').open('w')

    async def asyncTearDown(self):
        self.errlog.close()
        self.tmp.cleanup()

    def parameters(self):
        return StdioServerParameters(command=sys.executable,
            args=['-m', 'robot_harness_mcp', '--output', str(self.root / 'server')],
            env=os.environ.copy())

    async def test_real_stdio_tasks_reset_receipts_cancel_and_eof_cleanup(self):
        async with stdio_client(self.parameters(), errlog=self.errlog) as (read, write):
            async with ClientSession(read, write) as client:
                await client.initialize()
                names = {tool.name for tool in (await client.list_tools()).tools}
                self.assertEqual(names, {'open_session', 'capabilities', 'observe', 'submit',
                                        'status', 'cancel', 'reset', 'close_session'})
                opened = result(await client.call_tool('open_session'))
                self.assertEqual(result(await client.call_tool('open_session'))['session_id'], opened['session_id'])
                cap = result(await client.call_tool('capabilities'))
                self.assertEqual(cap['skill'], 'fixture.increment')
                self.assertTrue(cap['available'])
                self.assertFalse((await client.call_tool('observe')).isError)
                self.assertTrue((await client.call_tool('submit', {'request_id': 'bad', 'skill': 'robot.walk'})).isError)
                for invalid in (True, 0, 401):
                    self.assertTrue((await client.call_tool('submit', {
                        'request_id': 'invalid', 'skill': cap['skill'], 'steps': invalid})).isError)
                self.assertEqual(result(await client.call_tool('status'))['observation']['sequence'], 0)
                args = {'request_id': 'first', 'skill': cap['skill'], 'steps': 7}
                result(await client.call_tool('submit', args))
                first = await finished(client, 'first')
                self.assertEqual(first['result']['value'], 7)
                self.assertEqual(first['receipt']['settlement'], 'settled')
                self.assertEqual(result(await client.call_tool('submit', args)), first)
                self.assertTrue((await client.call_tool('submit', {**args, 'steps': 8})).isError)
                result(await client.call_tool('reset'))
                result(await client.call_tool('submit', {'request_id': 'second', 'skill': cap['skill'], 'steps': 4}))
                self.assertEqual((await finished(client, 'second'))['result']['value'], 4)
                self.assertEqual(result(await client.call_tool('status', {'request_id': 'first'})), first)
                current = result(await client.call_tool('status'))
                self.assertEqual((current['host_pid'], current['worker_pid']), (opened['host_pid'], opened['worker_pid']))
                result(await client.call_tool('reset'))
                result(await client.call_tool('submit', {'request_id': 'cancelled', 'skill': cap['skill']}))
                with anyio.fail_after(8):
                    while True:
                        state = result(await client.call_tool('status', {'request_id': 'cancelled'}))
                        if state['steps'] > 1 and state.get('queue_remaining', 0) > 10:
                            break
                        await anyio.sleep(.02)
                cancelled = result(await client.call_tool('cancel', {'request_id': 'cancelled'}))
                stopped = await finished(client, 'cancelled')
                self.assertEqual(stopped['steps'], cancelled['revoked_at_step'])
                self.assertEqual(stopped['receipt']['native_outcome'], 'cancelled')
                self.assertIsNone(stopped['result'])
                self.assertTrue((await client.call_tool('submit', {'request_id': 'fourth', 'skill': cap['skill']})).isError)
                # No explicit close: normal transport EOF must reap the owner/worker.
        await processes_absent(opened)

    async def test_eof_during_active_work_reaps_processes(self):
        async with stdio_client(self.parameters(), errlog=self.errlog) as (read, write):
            async with ClientSession(read, write) as client:
                await client.initialize()
                opened = result(await client.call_tool('open_session'))
                state = result(await client.call_tool('submit', {
                    'request_id': 'active', 'skill': 'fixture.increment', 'steps': 400}))
                self.assertEqual(state['state'], 'running')
        await processes_absent(opened)

    async def test_actual_cancel_notification_finishes_started_call_and_drops_queued_submit(self):
        params = StdioServerParameters(command=sys.executable,
            args=[str(Path(__file__).with_name('blocked_start_server.py')), str(self.root)],
            env=os.environ.copy())
        async with stdio_client(params, errlog=self.errlog) as (read, write):
            async def send(message):
                await write.send(SessionMessage(JSONRPCMessage.model_validate({'jsonrpc': '2.0', **message})))

            async def receive(request_id):
                with anyio.fail_after(8):
                    while True:
                        message = await read.receive()
                        if isinstance(message, Exception):
                            raise message
                        payload = message.message.model_dump(by_alias=True, exclude_none=True)
                        if payload.get('id') == request_id:
                            return payload

            await send({'id': 1, 'method': 'initialize', 'params': {
                'protocolVersion': '2025-11-25', 'capabilities': {},
                'clientInfo': {'name': 'cancellation-test', 'version': '1'}}})
            self.assertIn('result', await receive(1))
            await send({'method': 'notifications/initialized'})
            await send({'id': 2, 'method': 'tools/call', 'params': {'name': 'open_session'}})
            try:
                with anyio.fail_after(8):
                    while not (self.root / 'startup-entered').exists():
                        await anyio.sleep(.002)
                await send({'method': 'notifications/cancelled', 'params': {'requestId': 2, 'reason': 'test'}})
                self.assertEqual((await receive(2))['error']['message'], 'Request cancelled')
                await send({'id': 3, 'method': 'tools/call', 'params': {'name': 'submit',
                    'arguments': {'request_id': 'queued', 'skill': 'fixture.increment', 'steps': 400}}})
                await send({'method': 'notifications/cancelled', 'params': {'requestId': 3}})
                self.assertEqual((await receive(3))['error']['message'], 'Request cancelled')
            finally:
                (self.root / 'release-startup').touch()
            await send({'id': 4, 'method': 'tools/call', 'params': {'name': 'status'}})
            reply = await receive(4)
            opened = reply['result']['structuredContent']
            self.assertEqual(opened['observation']['sequence'], 0)
            events = [json.loads(line) for line in (self.root / 'server/tools.jsonl').read_text().splitlines()]
            self.assertEqual(sum(event.get('event') == 'result' and event.get('tool') == 'open' for event in events), 1)
            self.assertFalse(any(event.get('tool') == 'submit' for event in events))
            await send({'id': 5, 'method': 'tools/call', 'params': {'name': 'close_session'}})
            self.assertTrue((await receive(5))['result']['structuredContent']['owned_processes_reaped'])
        await processes_absent(opened)


if __name__ == '__main__':
    unittest.main()
