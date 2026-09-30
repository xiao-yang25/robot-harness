"""Deterministic candidate generator for the session fixture, never an executor."""

import socket
import sys
import time

from ._transport import Channel, integer


def main():
    channel = Channel(socket.socket(fileno=int(sys.argv[1])))
    delay_ms = int(sys.argv[2])
    pending = None
    channel.queue({'kind': 'ready'})
    try:
        while True:
            for message in channel.pump():
                if message.get('kind') == 'close':
                    return
                if message.get('kind') != 'predict' or pending is not None:
                    raise ValueError('unexpected prediction')
                count = integer(message.get('count'), 'count', 1, 100)
                pending = (time.monotonic() + delay_ms / 1000, message, count)
            if pending and time.monotonic() >= pending[0]:
                _, request, count = pending
                channel.queue({'kind': 'prediction', 'identity': request['identity'],
                               'actions': [1] * count})
                pending = None
            time.sleep(0.002)
    except (EOFError, BrokenPipeError, ConnectionResetError):
        pass
    finally:
        channel.close()


if __name__ == '__main__':
    main()
