"""Bounded private local framing. No pickle, threads or unbounded send queues."""

import json
import socket
import struct

MAX_FRAME = 65536
MAX_BUFFER = 2 * (MAX_FRAME + 4)
MAX_PAYLOAD = 640 * 480 * 3
VERSION = 2


class Channel:
    def __init__(self, sock, *, binary=False):
        self.limit = MAX_BUFFER + (MAX_PAYLOAD if binary else 0)
        self.binary = binary
        self.sock = sock
        sock.setblocking(False)
        self.incoming = bytearray()
        self.outgoing = bytearray()

    def queue(self, message, payload=b''):
        if not isinstance(payload, bytes) or len(payload) > (MAX_PAYLOAD if self.binary else 0):
            raise BufferError('invalid binary payload')
        data = json.dumps({**message, 'version': VERSION, '_payload_size': len(payload)}, allow_nan=False,
                          separators=(',', ':')).encode()
        if len(data) > MAX_FRAME or len(self.outgoing) + len(data) + 4 + len(payload) > self.limit:
            raise BufferError('channel capacity exceeded')
        self.outgoing.extend(struct.pack('!I', len(data)))
        self.outgoing.extend(data)
        self.outgoing.extend(payload)

    def pump(self):
        if self.outgoing:
            try:
                sent = self.sock.send(self.outgoing[:MAX_FRAME])
                del self.outgoing[:sent]
            except BlockingIOError:
                pass
        if len(self.incoming) < self.limit:
            try:
                data = self.sock.recv(min(MAX_FRAME, self.limit - len(self.incoming)))
                if not data:
                    raise EOFError('peer disconnected')
                self.incoming.extend(data)
            except BlockingIOError:
                pass
        messages = []
        for _ in range(16):
            if len(self.incoming) < 4:
                break
            size = struct.unpack('!I', self.incoming[:4])[0]
            if size == 0 or size > MAX_FRAME:
                raise ValueError('invalid frame length')
            if len(self.incoming) < size + 4:
                break
            data = bytes(self.incoming[4:size + 4])
            message = json.loads(data, parse_constant=self._reject_constant)
            if not isinstance(message, dict) or type(message.get('version')) is not int or message['version'] != VERSION:
                raise ValueError('unsupported message version')
            payload_size = integer(message.pop('_payload_size', 0), 'payload size', 0,
                                   MAX_PAYLOAD if self.binary else 0)
            end = size + 4 + payload_size
            if len(self.incoming) < end:
                break
            if payload_size:
                message['_payload'] = bytes(self.incoming[size + 4:end])
            del self.incoming[:end]
            messages.append(message)
        return messages

    @staticmethod
    def _reject_constant(value):
        raise ValueError('nonfinite JSON number')

    def close(self):
        self.sock.close()


def integer(value, name, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f'invalid {name}')
    return value


def identifier(value, name):
    if not isinstance(value, str) or not value or len(value) > 128:
        raise ValueError(f'invalid {name}')
    return value
