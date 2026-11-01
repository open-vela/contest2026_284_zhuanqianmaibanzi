"""SPDX-License-Identifier: Apache-2.0
Bounded loopback fault injection in front of the real C# receiver.
"""
import collections
import socket
import threading


class FaultProxy:
    def __init__(self, backend, corrupt=0, drop_ack=False):
        self.backend = backend
        self.corrupt = corrupt
        self.drop_ack = drop_ack
        self.dropped = False
        self.attempts = collections.Counter()
        self.errors = []
        self.stop = threading.Event()
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.listener.settimeout(0.2)
        self.port = self.listener.getsockname()[1]
        self.worker = threading.Thread(target=self.run, daemon=True)

    def __enter__(self):
        self.worker.start()
        return self

    def __exit__(self, *args):
        self.stop.set()
        self.worker.join(10)
        self.listener.close()
        if self.worker.is_alive():
            raise AssertionError("Fault proxy did not stop")
        if self.errors:
            raise AssertionError(self.errors)

    @staticmethod
    def reply(source, destination):
        while True:
            line = source.readline()
            if not line:
                raise EOFError("Receiver closed before reply")
            destination.write(line)
            if line != b"WAIT\n":
                return line

    def session(self, client):
        with socket.create_connection(("127.0.0.1", self.backend), timeout=8) as backend:
            client.settimeout(8)
            with client.makefile("rwb", buffering=0) as incoming, backend.makefile("rwb", buffering=0) as outgoing:
                outgoing.write(incoming.readline())
                self.reply(outgoing, incoming)
                while True:
                    header = incoming.readline()
                    if not header:
                        return
                    outgoing.write(header)
                    command = header.split()
                    response = self.reply(outgoing, incoming)
                    if command[0] in (b"END", b"DONE"):
                        return
                    if command[0] != b"FILE":
                        continue
                    name = command[1].decode()
                    self.attempts[name] += 1
                    if response == b"HAVE\n":
                        continue
                    if response != b"SEND\n":
                        return
                    remaining = int(command[2])
                    first = True
                    while remaining:
                        data = incoming.read(min(4096, remaining))
                        if not data:
                            return
                        remaining -= len(data)
                        if first and name == "fail/fail_00000001.bmp" and self.attempts[name] <= self.corrupt:
                            data = bytes([data[0] ^ 128]) + data[1:]
                        first = False
                        outgoing.write(data)
                    if self.drop_ack and not self.dropped and name == "fail/fail_00000001.bmp":
                        assert outgoing.readline() == b"OK\n"
                        self.dropped = True
                        return
                    if self.reply(outgoing, incoming) == b"ERROR\n":
                        return

    def run(self):
        while not self.stop.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            try:
                with client:
                    self.session(client)
            except (BrokenPipeError, ConnectionResetError):
                pass  # Device cancels and closes failed sessions intentionally.
            except Exception as error:
                self.errors.append(error)
