"""Retain actual ROS action identities and observe native facts once."""
from robot_harness._execution import accepted


class ObservedFuture:
    def __init__(self, future, observe):
        self.future = future
        self.observe = observe
        self.observed = False
        self.value = None

    def done(self):
        return self.future.done()

    def result(self):
        if not self.done():
            raise RuntimeError('native future is unresolved')
        if not self.observed:
            value = self.future.result()
            # Mark only after observation succeeds. A thrown callback keeps the
            # underlying future and actual handle available for failure cleanup.
            self.value = self.observe(value)
            self.observed = True
        return self.value


class ObservedHandle:
    def __init__(self, handle, pending, execution):
        self.handle = handle
        self.pending = pending
        self.execution = execution
        self.accepted = handle.accepted
        self.goal_id = handle.goal_id
        self.result_future = None

    def cancel_goal_async(self):
        return self.handle.cancel_goal_async()

    def get_result_async(self):
        if self.result_future is None:
            self.result_future = ObservedFuture(self.handle.get_result_async(), self.terminal)
        return self.result_future

    def terminal(self, result):
        outcome = {4: 'succeeded', 5: 'cancelled', 6: 'failed'}.get(result.status)
        if outcome is None:
            raise ValueError('unexpected native terminal status')
        accepted(self.execution.native(self.pending['record'], outcome, self.pending['goal_id']))
        self.pending['terminal'] = True
        return result


class ObservedClient:
    def __init__(self, client, owner):
        self.client = client
        self.owner = owner

    def server_is_ready(self):
        return self.client.server_is_ready()

    def send_goal_async(self, message, *, feedback_callback, goal_uuid):
        goal_id = bytes(goal_uuid.uuid).hex()
        pending = self.owner.before_send(goal_id)
        # Reservation/logging can block. Recheck after it, immediately before
        # crossing the real ROS submission boundary, without intervening I/O.
        self.owner.permit_send(pending)
        future = self.client.send_goal_async(message, feedback_callback=feedback_callback,
                                             goal_uuid=goal_uuid)
        observed = ObservedFuture(future, lambda handle: self.acceptance(pending, handle))
        pending['future'] = observed
        return observed

    def acceptance(self, pending, handle):
        # Retain the actual handle even if identity/evidence validation fails.
        pending['handle'] = handle
        if bytes(handle.goal_id.uuid).hex() != pending['goal_id']:
            raise ValueError('native acceptance identity mismatch')
        kind = 'accepted' if handle.accepted else 'rejected'
        accepted(self.owner.execution.native(pending['record'], kind, pending['goal_id']))
        pending['acceptance'] = kind
        wrapped = ObservedHandle(handle, pending, self.owner.execution)
        pending['wrapped'] = wrapped
        return wrapped
