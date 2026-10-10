"""Single-client endpoint owned and polled by the ROS/Core owner."""
import os
from pathlib import Path
import socket
import tempfile

from robot_harness._transport import Channel, integer


class RequestEndpoint:
    def __init__(self, requests, on_reply=lambda message, result: None, *, terminal=None):
        self.requests = requests
        self.terminal = terminal
        self.on_reply = on_reply
        self.peer = self.listener = self.directory = None
        self.closed = False
        try:
            # Shared Docker Desktop mounts may not implement socket chmod.
            self.directory = tempfile.TemporaryDirectory(prefix='robot-harness-nav2-')
            self.path = str(Path(self.directory.name) / 'navigation.sock')
            self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.listener.bind(self.path)
            os.chmod(self.path, 0o600)
            self.listener.listen(1)
            self.listener.setblocking(False)
        except BaseException:
            self.dispose()
            raise

    @property
    def replies_drained(self):
        """Bytes sent to the local socket, not a remote acknowledgement."""
        return self.peer is not None and not self.peer.outgoing

    def pump(self):
        if self.closed:
            return
        if self.terminal is not None and self.terminal.expired:
            self.requests.close()
            self.dispose()
            return
        if self.peer is None and self.listener is not None:
            try:
                sock, _ = self.listener.accept()
                self.peer = Channel(sock)
                self.listener.close()
                self.listener = None
            except BlockingIOError:
                return
        if self.peer is None:
            return
        try:
            # Every frame from both bounded pumps is handled, including frames
            # received while flushing a prior intent response.
            for _ in range(2):
                for message in self.peer.pump():
                    if self.terminal is not None and self.terminal.expired:
                        self.requests.close()
                        self.dispose()
                        return
                    rpc = integer(message.get('rpc'), 'rpc', 1, 2**53)
                    try:
                        result = (self.requests.command(message) if self.terminal is None else
                                  self.terminal.command(self.requests, message))
                        self.on_reply(message, result)
                        self.peer.queue(dict(rpc=rpc, result=result))
                    except (ValueError, KeyError) as error:
                        self.peer.queue(dict(rpc=rpc, error=str(error)))
        except (EOFError, OSError, ValueError, BufferError):
            # Authority revocation precedes local transport disposal.
            self.requests.close()
            self.dispose()

    def dispose(self):
        """Release local resources; owner separately closes execution authority."""
        self.closed = True
        if self.peer is not None:
            self.peer.close()
            self.peer = None
        if self.listener is not None:
            self.listener.close()
            self.listener = None
        if self.directory is not None:
            self.directory.cleanup()
            self.directory = None
