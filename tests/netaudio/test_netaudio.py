"""Socket audio device of Mercury (``-i TCP:<port> -o TCP:<port>``, protocol 3).

Run after building the headless modem (``make mercury GUI_ENABLED=0``)::

    make test-netaudio          # or: python3 -m pytest tests/netaudio -v

Nothing here needs a sound card or a radio: the host is ``tools/netaudio_host.py``.
"""

import os
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from netaudio_host import RATE, TOKEN_ENV, Crossbar, NetaudioHost  # noqa: E402

MERCURY = os.environ.get("MERCURY_BIN", str(ROOT / "mercury"))
TOKEN = "0123456789abcdef0123456789abcdef"

pytestmark = pytest.mark.skipif(not os.access(MERCURY, os.X_OK),
                                reason="build mercury first (make mercury GUI_ENABLED=0)")


def free_port_pair():
    while True:
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]
        if port < 65000:
            try:
                with socket.socket() as s2:
                    s2.bind(("127.0.0.1", port + 1))
                return port
            except OSError:
                continue


def start(args, env=None):
    return subprocess.Popen([MERCURY] + args, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                            env=env if env is not None else os.environ)


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(5)


def tnc(port, timeout=10.0):
    deadline = time.monotonic() + timeout
    while True:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=1.0)
            sock.settimeout(0.2)
            return sock
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.1)


def read_lines(sock, until, timeout):
    """Command-port lines until one starts with *until* (or the timeout)."""
    buf = b""
    lines = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            break
        buf += chunk
        while b"\r" in buf:
            line, buf = buf.split(b"\r", 1)
            text = line.decode("ascii", "replace").strip()
            if text:
                lines.append((time.monotonic(), text))
                if text.startswith(until):
                    return lines
    return lines


class TestHandshake:
    def test_tx_audio_after_mutual_authentication(self):
        host = NetaudioHost(TOKEN)
        proc = start(["-m", "TX_TEST", "-s", "1", "-i", host.device, "-o", host.device],
                     host.env())
        try:
            assert host.wait_connected(5), proc.stderr.read1() if proc.poll() else ""
            assert host.hello == "MERCURY-NETAUDIO 3 48000"
            time.sleep(3.0)
            with host.tx_lock:
                got = len(host.tx)
            # paced: real time plus at most 0.5 s ahead and one 0.1 s message
            assert 1.5 * RATE < got <= (3.0 + 0.5 + 0.2) * RATE
            assert max(abs(v) for v in host.tx[-RATE:]) > 1000
        finally:
            stop(proc)
            host.close()

    def test_refused_without_token(self):
        host = NetaudioHost(TOKEN)
        env = dict(os.environ)
        env.pop(TOKEN_ENV, None)
        proc = start(["-m", "TX_TEST", "-s", "1", "-i", host.device, "-o", host.device], env)
        try:
            assert proc.wait(10) != 0
            assert not host.authenticated.is_set()
            assert b"MERCURY_NETAUDIO_TOKEN" in proc.stderr.read()
        finally:
            stop(proc)
            host.close()

    def test_refused_with_a_wrong_host_proof(self):
        host = NetaudioHost(TOKEN, prove=False)
        proc = start(["-m", "TX_TEST", "-s", "1", "-i", host.device, "-o", host.device],
                     host.env())
        try:
            assert proc.wait(10) != 0
            assert b"did not authenticate" in proc.stderr.read()
            with host.tx_lock:
                assert len(host.tx) == 0
        finally:
            stop(proc)
            host.close()

    def test_refused_with_a_wrong_engine_token(self):
        host = NetaudioHost(TOKEN)
        env = dict(os.environ, **{TOKEN_ENV: "another-token-0123456789"})
        proc = start(["-m", "TX_TEST", "-s", "1", "-i", host.device, "-o", host.device], env)
        try:
            assert proc.wait(10) != 0
            assert not host.authenticated.is_set()
        finally:
            stop(proc)
            host.close()

    def test_non_loopback_device_refused(self):
        env = dict(os.environ, **{TOKEN_ENV: TOKEN})
        proc = start(["-m", "TX_TEST", "-s", "1", "-i", "TCP:192.0.2.1:5000",
                      "-o", "TCP:192.0.2.1:5000"], env)
        try:
            assert proc.wait(10) != 0
            assert b"not a loopback address" in proc.stderr.read()
        finally:
            stop(proc)

    def test_capture_and_playback_must_be_the_same_socket(self):
        env = dict(os.environ, **{TOKEN_ENV: TOKEN})
        proc = start(["-m", "TX_TEST", "-s", "1", "-i", "TCP:5000", "-o", "default"], env)
        try:
            assert proc.wait(10) != 0
        finally:
            stop(proc)

    def test_host_loss_ends_mercury(self):
        host = NetaudioHost(TOKEN)
        proc = start(["-m", "TX_TEST", "-s", "1", "-i", host.device, "-o", host.device],
                     host.env())
        try:
            assert host.wait_connected(5)
            time.sleep(0.5)
            host.close()
            proc.wait(10)
        finally:
            stop(proc)


class TestArq:
    def test_ports_on_loopback_and_ptt_off_only_after_drain(self):
        host = NetaudioHost(TOKEN, auto_drain=False)
        port = free_port_pair()
        proc = start(["-m", "ARQ", "-n", "-p", str(port), "-i", host.device,
                      "-o", host.device], host.env())
        try:
            assert host.wait_connected(5)
            ctl = tnc(port)
            # socket audio keeps the ARQ ports off the network by default
            out = subprocess.run(["lsof", "-nP", "-a", "-p", str(proc.pid), "-iTCP",
                                  "-sTCP:LISTEN"], capture_output=True, text=True).stdout
            listeners = [line.split()[8] for line in out.splitlines()[1:]]
            assert sorted(listeners) == sorted(["127.0.0.1:%d" % port,
                                                "127.0.0.1:%d" % (port + 1)]), out
            ctl.sendall(b"MYCALL N0CALL\r")
            ctl.sendall(b"CONNECT N0CALL N1CALL\r")
            lines = read_lines(ctl, "PTT ON", 15)
            assert any(t == "PTT ON" for _, t in lines), lines
            deadline = time.monotonic() + 20
            while not host.ends and time.monotonic() < deadline:
                time.sleep(0.05)
            assert host.ends, "no end of transmission"
            number, at, samples = host.ends[0]
            assert samples > 0.1 * RATE
            # no D yet: Mercury keeps PTT for now
            assert not any(t == "PTT OFF" for _, t in read_lines(ctl, "PTT OFF", 0.5))
            host.drain(number)
            lines = read_lines(ctl, "PTT OFF", 5)
            assert any(t == "PTT OFF" for _, t in lines), lines
            ctl.sendall(b"ABORT\r")
            ctl.close()
        finally:
            stop(proc)
            host.close()

    def test_ptt_off_without_drain_ends_mercury(self):
        host = NetaudioHost(TOKEN, auto_drain=False)
        port = free_port_pair()
        proc = start(["-m", "ARQ", "-n", "-p", str(port), "-i", host.device,
                      "-o", host.device], host.env())
        try:
            assert host.wait_connected(5)
            ctl = tnc(port)
            ctl.sendall(b"MYCALL N0CALL\r")
            ctl.sendall(b"CONNECT N0CALL N1CALL\r")
            # length + 2 s without 'D': PTT OFF, then Mercury exits
            assert proc.wait(30) is not None
            assert b"no drain acknowledgement" in proc.stderr.read()
            ctl.close()
        finally:
            stop(proc)
            host.close()

    def test_two_modems_connect_over_the_crossbar(self):
        bar = Crossbar(TOKEN)
        pa, pb = free_port_pair(), None
        a = start(["-m", "ARQ", "-n", "-s", "1", "-p", str(pa), "-i", bar.a.device,
                   "-o", bar.a.device], bar.a.env())
        try:
            assert bar.a.wait_connected(5)
            pb = free_port_pair()
            while pb in (pa, pa + 1):
                pb = free_port_pair()
            b = start(["-m", "ARQ", "-n", "-s", "1", "-p", str(pb), "-i", bar.b.device,
                       "-o", bar.b.device], bar.b.env())
            try:
                assert bar.b.wait_connected(5)
                ca, cb = tnc(pa), tnc(pb)
                cb.sendall(b"MYCALL N1CALL\r")
                cb.sendall(b"LISTEN ON\r")
                time.sleep(0.5)
                ca.sendall(b"MYCALL N0CALL\r")
                ca.sendall(b"CONNECT N0CALL N1CALL\r")
                lines = read_lines(ca, "CONNECTED", 90)
                assert any(t.startswith("CONNECTED N0CALL N1CALL") for _, t in lines), lines
                ca.sendall(b"ABORT\r")
            finally:
                stop(b)
        finally:
            stop(a)
            bar.close()
