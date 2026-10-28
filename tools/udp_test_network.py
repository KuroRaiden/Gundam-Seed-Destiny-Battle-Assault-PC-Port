"""Isolated loopback UDP impairment for native peer qualification only."""
import heapq
import random
import select
import socket
import struct
import threading
import time

class ControlledUdpLink:
    def __init__(self, first_port, lag_ms=0, jitter_ms=0, loss_percent=0, finish_ack_delay_ms=0):
        self.clients = [("127.0.0.1", first_port), ("127.0.0.1", first_port + 1)]
        self.sockets = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
        self.ports = [first_port + 2, first_port + 3]
        for channel, port in zip(self.sockets, self.ports):
            channel.bind(("127.0.0.1", port))
            channel.setblocking(False)
        self.lag = lag_ms / 1000
        self.jitter = jitter_ms / 1000
        self.loss = loss_percent / 100
        self.finish_delay = finish_ack_delay_ms / 1000
        self.stats = dict(received=0, dropped=0, forwarded=0, delayed_finish_acks=0)
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        pending = []
        serial = 0
        rng = random.Random(20261028)
        while not self.stop.is_set():
            readable, _, _ = select.select(self.sockets, [], [], 0.005)
            for channel in readable:
                side = self.sockets.index(channel)
                try:
                    data, source = channel.recvfrom(65536)
                except (BlockingIOError, ConnectionResetError):
                    continue
                if source != self.clients[side]:
                    continue
                self.stats["received"] += 1
                if rng.random() < self.loss:
                    self.stats["dropped"] += 1
                    continue
                delay = max(0, self.lag + rng.uniform(-self.jitter, self.jitter))
                if side == 0 and len(data) > 37 and data[5] == 1 and data[36] == 4 and struct.unpack_from("<I", data, 28)[0] == 0x314e504a:
                    delay += self.finish_delay
                    if self.finish_delay:
                        self.stats["delayed_finish_acks"] += 1
                serial += 1
                heapq.heappush(pending, (time.monotonic() + delay, serial, 1-side, data))
            now = time.monotonic()
            while pending and pending[0][0] <= now:
                _, _, side, data = heapq.heappop(pending)
                try:
                    self.sockets[side].sendto(data, self.clients[side])
                    self.stats["forwarded"] += 1
                except (BlockingIOError, ConnectionResetError):
                    pass

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        for channel in self.sockets:
            channel.close()
