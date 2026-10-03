"""Private single-writer coordination; drivers retain native work and resources."""

MAX_RECORDS = 64


def accepted(disposition):
    if disposition != 'accepted':
        raise RuntimeError(f'Core rejected evidence: {disposition}')


class ExecutionCoordinator:
    """Borrow a Gate; retain requests without inventing a second authority model.

    The owner supplies normalized arguments and correlated native facts. Settlement
    is an explicit driver assertion that its closure obligations are resolved.
    This class neither closes native work nor creates a progress thread.
    """

    def __init__(self, gate, clock):
        self.gate = gate
        self.clock = clock
        self.records = {}
        self.active = None

    def reserve(self, reference, arguments, driver_fields):
        if reference in self.records:
            record = self.records[reference]
            if record['arguments'] != arguments:
                raise ValueError('request ID reused with different arguments')
            return record, False
        if len(self.records) >= MAX_RECORDS:
            raise ValueError('session record capacity reached; open a new session')
        record = dict(driver_fields())
        record.update(request_id=reference, arguments=arguments, state='rejected',
                      operation_id=None, result=None, receipt=None)
        self.records[reference] = record
        return record, True

    def snapshot(self, record):
        result = {key: value for key, value in record.items() if key != 'arguments'}
        if record is self.active:
            result['receipt'] = self.gate.receipt(record['operation_id'])
        return result

    def _retained(self, record):
        if self.records.get(record['request_id']) is not record:
            raise ValueError('request is not retained by this coordinator')

    def _operation(self, record):
        self._retained(record)
        if record is not self.active:
            raise ValueError('request is not active')
        return record['operation_id']

    def admit(self, record, units, deadline, stamp):
        self._retained(record)
        if self.active is not None or record['operation_id'] is not None:
            raise ValueError('active operation must be released before admission')
        decision = self.gate.admit(record['request_id'], units, deadline, stamp)
        record['reason'] = decision['status']
        if decision['status'] == 'admitted':
            record.update(operation_id=decision['operation_id'], state='running')
            self.active = record
        return decision

    def dispatch(self, record):
        return self.gate.dispatch(self._operation(record), self.clock())

    def non_submission(self, record):
        return self.gate.non_submission(self._operation(record), self.clock())

    def tick(self):
        if self.active is None:
            return None
        operation = self._operation(self.active)
        self.gate.tick(operation, self.clock())
        return self.gate.receipt(operation)

    def cancel(self, record):
        self._retained(record)
        if record is not self.active:
            return False
        self.gate.cancel(self._operation(record), self.clock())
        return True

    def native(self, record, kind, native_identity=None):
        operation = self._operation(record)
        if native_identity is None:
            return self.gate.native(operation, kind, self.clock())
        return self.gate.native(operation, kind, self.clock(), native_identity)

    def dispose_result(self, record, candidate, diagnostic):
        operation = self._operation(record)
        if candidate is not None and self.gate.can_deliver(operation):
            # The retained record is the declared sink; the single writer stages
            # and retracts it before any client can observe a failed publication.
            record['result'] = candidate
            disposition = self.gate.output(operation, 'accepted', record['request_id'], self.clock())
            if disposition != 'accepted':
                record['result'] = None
                record['diagnostic'] = diagnostic()
                self.gate.tick(operation, self.clock())
                if self.gate.can_deliver(operation):
                    accepted(disposition)
                accepted(self.gate.output(operation, 'authority_revoked', record['request_id'], self.clock()))
        else:
            record['diagnostic'] = diagnostic()
            disposition = 'authority_revoked' if candidate is not None else 'no_output'
            reference = record['request_id'] if candidate is not None else ''
            accepted(self.gate.output(operation, disposition, reference, self.clock()))

    def settle(self, record):
        operation = self._operation(record)
        accepted(self.gate.settle(operation, self.clock()))
        record.update(state='finished', receipt=self.gate.receipt(operation))

    def release(self, record):
        operation = self._operation(record)
        if self.gate.receipt(operation)['settlement'] != 'settled':
            raise ValueError('cannot release an unsettled operation')
        self.active = None
