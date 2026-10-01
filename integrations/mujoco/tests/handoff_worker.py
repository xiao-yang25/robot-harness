"""Test-only binary candidate peer; never imports a policy or simulator."""
import json
from pathlib import Path
import socket
import sys
import time

from robot_harness._transport import Channel


def main():
    channel = Channel(socket.socket(fileno=int(sys.argv[1])), binary=True)
    delay, log = float(sys.argv[2]), Path(sys.argv[3])
    channel.queue({'kind': 'ready'})
    try:
        while True:
            for request in channel.pump():
                if request['kind'] == 'close':
                    return
                with log.open('a') as output:
                    output.write(json.dumps({'identity': request['identity'], 'count': request['count']}) + '\n')
                time.sleep(delay)
                start = request['observation']['sequence']
                channel.queue({'kind': 'prediction', 'identity': request['identity'],
                               'actions': [[float(start + i + 1)] * 14 for i in range(request['count'])]})
            time.sleep(0.002)
    except (EOFError, BrokenPipeError, ConnectionResetError):
        pass
    finally:
        channel.close()


if __name__ == '__main__':
    main()
