import asyncio
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest

import anyio

from robot_harness_mcp.config import Config
from robot_harness_mcp.server import Bridge


class FakeSession:
    def __init__(self, **options):
        self.active = 0
        self.maximum_active = 0
        self.closed = False
        self.options = options
        self.lock = threading.Lock()
        time.sleep(.03)

    def status(self, **arguments):
        with self.lock:
            self.active += 1
            self.maximum_active = max(self.maximum_active, self.active)
        try:
            time.sleep(.02)
            if self.closed:
                raise RuntimeError('raced close')
            return {'host_pid': 123, 'worker_pid': 456, 'session_id': 'one'}
        finally:
            with self.lock:
                self.active -= 1

    def capabilities(self):
        return {'skill': 'fixture.increment', 'available': True}

    def submit(self, **arguments):
        return {'state': 'running', **arguments}

    def close(self):
        assert self.active == 0
        self.closed = True


class BridgeTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.bridge = Bridge(Path(self.tmp.name) / 'output', session_factory=FakeSession)

    async def asyncTearDown(self):
        await self.bridge.shutdown()
        self.tmp.cleanup()

    async def test_repeated_open_and_parallel_calls_share_one_serialized_client(self):
        first, second = await asyncio.gather(self.bridge.call('open'), self.bridge.call('open'))
        self.assertEqual(first, second)
        await asyncio.gather(*(self.bridge.call('status') for _ in range(3)))
        self.assertEqual(self.bridge.session.maximum_active, 1)

    async def test_request_cancel_during_startup_finishes_before_close(self):
        with anyio.move_on_after(.01) as scope:
            await self.bridge.call('open')
        self.assertTrue(scope.cancel_called)
        self.assertIsNotNone(self.bridge.session)
        await self.bridge.call('close')
        self.assertTrue(self.bridge.session.closed)

    async def test_cancelled_queued_submit_does_not_start(self):
        await self.bridge.call('open')
        acquired = anyio.Event()
        release = anyio.Event()
        async def hold_client():
            async with self.bridge.lock:
                acquired.set()
                await release.wait()
        async with anyio.create_task_group() as tasks:
            tasks.start_soon(hold_client)
            await acquired.wait()
            with anyio.move_on_after(.01) as scope:
                await self.bridge.call('submit', request_id='queued', skill='fixture.increment')
            release.set()
        self.assertTrue(scope.cancel_called)
        self.assertEqual(self.bridge.submissions, set())
        events = [json.loads(line) for line in (self.bridge.output / 'tools.jsonl').read_text().splitlines()]
        self.assertFalse(any(event.get('tool') == 'submit' for event in events))

    async def test_uncertain_close_can_only_be_explicitly_retried(self):
        await self.bridge.call('open')
        close = self.bridge.session.close
        def fail():
            raise RuntimeError('cleanup unconfirmed')
        self.bridge.session.close = fail
        with self.assertRaisesRegex(RuntimeError, 'unconfirmed'):
            await self.bridge.call('close')
        self.assertFalse(self.bridge.closed)
        with self.assertRaisesRegex(RuntimeError, 'closing'):
            await self.bridge.call('open')
        self.bridge.session.close = close
        await self.bridge.call('close')
        self.assertTrue(self.bridge.closed)

    async def test_distinct_id_budget_retains_uncertain_submission(self):
        await self.bridge.call('open')
        self.bridge.max_trials = 1
        submit = self.bridge.session.submit
        def uncertain(**arguments):
            raise RuntimeError('outcome unknown')
        self.bridge.session.submit = uncertain
        with self.assertRaisesRegex(RuntimeError, 'unknown'):
            await self.bridge.call('submit', request_id='one', skill='fixture.increment')
        with self.assertRaisesRegex(ValueError, 'budget'):
            await self.bridge.call('submit', request_id='two', skill='fixture.increment')
        self.bridge.session.submit = submit
        self.assertEqual((await self.bridge.call('submit', request_id='one', skill='fixture.increment'))['request_id'], 'one')

    async def test_unsupported_skill_does_not_reserve_a_trial(self):
        await self.bridge.call('open')
        with self.assertRaisesRegex(ValueError, 'unsupported'):
            await self.bridge.call('submit', request_id='bad', skill='robot.walk')
        self.assertEqual(self.bridge.submissions, set())

    async def test_tool_budget_still_permits_close(self):
        self.bridge.max_tool_calls = 1
        await self.bridge.call('open')
        with self.assertRaisesRegex(RuntimeError, 'budget'):
            await self.bridge.call('status')
        self.assertTrue((await self.bridge.call('close'))['closed'])

    async def test_trace_failure_cannot_prevent_owned_cleanup(self):
        await self.bridge.call('open')
        record = self.bridge.record
        def fail(event):
            raise OSError('trace unavailable')
        self.bridge.record = fail
        try:
            with self.assertRaisesRegex(OSError, 'trace unavailable'):
                await self.bridge.call('close')
            self.assertTrue(self.bridge.session.closed)
        finally:
            self.bridge.record = record

    async def test_failed_startup_is_not_replaced(self):
        attempts = []
        def fail(**options):
            attempts.append(options)
            raise RuntimeError('startup failed')
        self.bridge.session_factory = fail
        with self.assertRaisesRegex(RuntimeError, 'startup failed'):
            await self.bridge.call('open')
        with self.assertRaisesRegex(RuntimeError, 'previously failed'):
            await self.bridge.call('open')
        self.assertEqual(len(attempts), 1)

    async def test_invalid_camera_is_not_encoded_or_recorded_as_an_image(self):
        await self.bridge.call('open')
        self.bridge.session.observe = lambda: {'valid': False, 'rgb': b'abc',
                                              'image': {'shape': [1, 1, 3], 'dtype': 'uint8'}}
        with self.assertRaisesRegex(ValueError, 'invalid camera'):
            await self.bridge.call('observe')
        self.assertEqual(list(self.bridge.output.glob('*.png')), [])


class ConfigTests(unittest.TestCase):
    def test_operator_paths_and_optional_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            worker = root / 'worker.py'
            worker.touch()
            checkpoint = root / 'model'
            checkpoint.mkdir()
            config_file = root / 'config.json'
            config_file.write_text(json.dumps({'backend': 'mujoco', 'worker_script': str(worker),
                'checkpoint': str(checkpoint), 'device': 'mps', 'startup_timeout': 90,
                'optional_note': 'kept outside execution'}))
            config = Config.read(config_file)
            options = config.session_options(root / 'output')
            self.assertEqual(options['mujoco']['worker_script'], str(worker))
            self.assertEqual(options['mujoco']['output'], str(root / 'output/episodes'))
            with self.assertRaisesRegex(ValueError, 'absolute'):
                Config(backend='mujoco', worker_script='worker.py', checkpoint=str(checkpoint))

    def test_invalid_config_fails_before_output_creation(self):
        for values in ({'backend': 'unknown'}, {'startup_timeout': float('nan')},
                       {'worker_delay_ms': True}, {'seed': 5}):
            with self.assertRaises(ValueError):
                Config(**values)


if __name__ == '__main__':
    unittest.main()
