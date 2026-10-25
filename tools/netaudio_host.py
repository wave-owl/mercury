#!/usr/bin/env python3
"""Minimal host for Mercury's socket audio device (``-i TCP:<port> -o TCP:<port>``).

Python standard library only. See ``include/audioio/netaudio.h`` for the wire format
(protocol 3). The host listens on loopback, authenticates Mercury with the shared token
(``MERCURY_NETAUDIO_TOKEN`` in Mercury's environment), streams RX audio in real time
(silence unless something is queued) and collects Mercury's TX audio. It acknowledges each
end of transmission ('E') with 'D' once the transmission has been "played" in real time,
unless ``auto_drain`` is off.

:class:`Crossbar` connects two Mercury instances: what one transmits is the other's
receive audio (a virtual channel with a little noise), so two modems can run a real ARQ
session on one machine without sound cards.

Example::

    host = NetaudioHost(token="0123456789abcdef0123")
    # start: MERCURY_NETAUDIO_TOKEN=... mercury -m ARQ -n -i TCP:<port> -o TCP:<port>
    host.wait_connected(5)
"""

import hashlib
import hmac
import os
import random
import secrets
import socket
import struct
import threading
import time
from array import array

HEADER = struct.Struct("<cI")
RATE = 48000
HELLO = "MERCURY-NETAUDIO"
TOKEN_ENV = "MERCURY_NETAUDIO_TOKEN"
CHUNK_S = 0.02


class NetaudioHost:
    """One loopback listener speaking protocol 3 with one Mercury.

    Args:
        token: shared secret (16..128 characters).
        prove: False sends a wrong proof ('P') to test Mercury's refusal.
        auto_drain: acknowledge every 'E' with 'D' after the transmission's duration.
        on_tx: called with each TX chunk (``array('h')``) as it arrives.
        noise: RX noise amplitude (int16 units) added to the RX stream.
    """

    def __init__(self, token, prove=True, auto_drain=True, on_tx=None, noise=0):
        self.token = token
        self.prove = prove
        self.auto_drain = auto_drain
        self.on_tx = on_tx
        self.noise = int(noise)
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(1)
        self.port = self.server.getsockname()[1]
        self.conn = None
        self.hello = None
        self.authenticated = threading.Event()
        self.closed = threading.Event()
        self.tx = array("h")
        self.tx_lock = threading.Lock()
        self.ends = []          # (number, monotonic time of 'E', TX samples so far)
        self.drained = []       # numbers acknowledged
        self._rx_queue = array("h")
        self._rx_lock = threading.Lock()
        self._send_lock = threading.Lock()
        self._burst_start = None
        self._burst_samples = 0
        self._running = True
        threading.Thread(target=self._serve, daemon=True).start()

    @property
    def device(self):
        return "TCP:127.0.0.1:%d" % self.port

    def env(self):
        return dict(os.environ, **{TOKEN_ENV: self.token})

    # -- connection -----------------------------------------------------------------

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.conn.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("closed")
            buf += chunk
        return buf

    def _send(self, kind, payload=b""):
        with self._send_lock:
            self.conn.sendall(HEADER.pack(kind, len(payload)) + payload)

    def _serve(self):
        try:
            self.conn, _ = self.server.accept()
        except OSError:
            self.closed.set()
            return
        self.server.close()
        try:
            self.conn.settimeout(3.0)
            nonce1 = secrets.token_bytes(32)
            self._send(b"N", nonce1)
            kind, length = HEADER.unpack(self._recv_exact(HEADER.size))
            hello = self._recv_exact(length).decode("ascii")
            parts = hello.split()
            assert kind == b"H" and parts[:3] == [HELLO, "3", str(RATE)], hello
            key = self.token.encode()
            expected = hmac.new(key, nonce1 + b"engine", hashlib.sha256).hexdigest()
            assert hmac.compare_digest(parts[4], expected), "engine HMAC mismatch"
            self.hello = " ".join(parts[:3])
            proof = hmac.new(key, bytes.fromhex(parts[3]) + b"bridge", hashlib.sha256).digest()
            if not self.prove:
                proof = bytes(32)
            self._send(b"P", proof)
            self.conn.settimeout(None)
            self.authenticated.set()
        except (OSError, AssertionError, ValueError, ConnectionError):
            self.close()
            return
        threading.Thread(target=self._pace_rx, daemon=True).start()
        self._read()

    def wait_connected(self, timeout):
        return self.authenticated.wait(timeout)

    def _read(self):
        try:
            while self._running:
                kind, length = HEADER.unpack(self._recv_exact(HEADER.size))
                payload = self._recv_exact(length) if length else b""
                if kind == b"T":
                    chunk = array("h")
                    chunk.frombytes(payload)
                    with self.tx_lock:
                        if self._burst_samples == 0:
                            self._burst_start = time.monotonic()
                        self._burst_samples += len(chunk)
                        self.tx.extend(chunk)
                    if self.on_tx is not None:
                        self.on_tx(chunk)
                elif kind == b"E":
                    number = struct.unpack("<I", payload)[0]
                    with self.tx_lock:
                        start, samples = self._burst_start, self._burst_samples
                        self._burst_samples = 0
                        self.ends.append((number, time.monotonic(), len(self.tx)))
                    if self.auto_drain:
                        delay = 0.0
                        if start is not None:
                            delay = max(0.0, start + samples / RATE - time.monotonic())
                        threading.Timer(delay, self.drain, args=(number,)).start()
        except (OSError, ConnectionError, struct.error):
            pass
        self.closed.set()

    def drain(self, number):
        """Acknowledge transmission *number* ('D')."""
        try:
            self._send(b"D", struct.pack("<I", number))
            self.drained.append(number)
        except OSError:
            pass

    # -- RX -------------------------------------------------------------------------

    def queue_rx(self, samples):
        """Queue int16 samples for the RX stream (played in real time)."""
        with self._rx_lock:
            self._rx_queue.extend(samples)

    def _pace_rx(self):
        t0 = time.monotonic()
        sent = 0
        while self._running and not self.closed.is_set():
            time.sleep(CHUNK_S / 2)
            due = int((time.monotonic() - t0) * RATE) - sent
            if due <= 0:
                continue
            with self._rx_lock:
                take = self._rx_queue[:due]
                del self._rx_queue[:due]
            out = array("h", take)
            out.extend([0] * (due - len(out)))
            if self.noise:
                rnd = random.Random(sent)
                for i in range(len(out)):
                    out[i] = max(-32768, min(32767, out[i] + int(rnd.gauss(0, self.noise))))
            try:
                self._send(b"R", out.tobytes())
            except OSError:
                break
            sent += due

    def close(self):
        self._running = False
        for sock in (self.conn, self.server):
            if sock is None:
                continue
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass
        self.closed.set()


class Crossbar:
    """Two hosts whose TX audio is each other's RX audio (a virtual radio channel)."""

    def __init__(self, token, noise=30):
        self.a = NetaudioHost(token, on_tx=lambda c: self.b.queue_rx(c), noise=noise)
        self.b = NetaudioHost(token, on_tx=lambda c: self.a.queue_rx(c), noise=noise)

    def close(self):
        self.a.close()
        self.b.close()
