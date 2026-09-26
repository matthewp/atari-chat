#!/usr/bin/env python3
"""
Hayes-style modem emulator for Hatari: the stand-in for a WiFi modem.

Creates a pseudo-terminal, symlinks it at --link (Hatari opens that as the ST
serial port), and understands a few AT commands. After "ATDT host:port" it is
a transparent byte pipe to a TCP socket, exactly like a real WiFi modem.
It never looks at or alters the data: TLS, HTTP and JSON all happen on the ST.

Data from the network is sent to the ST at the serial baud rate (--baud),
like real hardware, so the ST's receive buffer is not flooded.

Commands: AT, ATZ, ATI, ATE0/ATE1, ATH, ATD[T|P]host[:port]
In data mode, "+++" surrounded by 1s of silence returns to command mode.
When the remote side closes, sends NO CARRIER and returns to command mode.
"""
import argparse
import fcntl
import os
import pty
import select
import signal
import socket
import struct
import sys
import termios
import time
import tty

GUARD = 1.0  # seconds of silence around "+++"


def log(msg):
    print(f"[modem] {msg}", file=sys.stderr, flush=True)


class Modem:
    def __init__(self, fd, slave_fd, verbose, baud):
        self.fd = fd
        self.slave_fd = slave_fd
        self.verbose = verbose
        self.bytes_per_sec = baud / 10   # 8N1: 10 bits per byte
        self.outq = bytearray()          # network data waiting to go to the ST
        self.budget = 0.0
        self.last_pace = time.monotonic()
        self.sock = None
        self.echo = True
        self.line = bytearray()
        self.last_rx = 0.0
        self.pending_escape = None  # time "+++" arrived
        self.tx_bytes = self.rx_bytes = 0

    def send(self, data):
        self.outq += data

    def pace(self):
        """Release queued bytes to the ST no faster than the baud rate."""
        now = time.monotonic()
        self.budget = min(self.budget + (now - self.last_pace) * self.bytes_per_sec, 64)
        self.last_pace = now
        n = min(int(self.budget), len(self.outq))
        if n > 0:
            written = os.write(self.fd, bytes(self.outq[:n]))
            del self.outq[:written]
            self.budget -= written

    def reply(self, text):
        self.send(b"\r\n" + text.encode() + b"\r\n")

    # ---- command mode ---------------------------------------------------

    def command_bytes(self, data):
        for b in data:
            if b == 0x0d:
                if self.echo:
                    self.send(b"\r")
                cmd = self.line.decode("latin-1").strip()
                self.line.clear()
                if cmd:
                    self.command(cmd)
            elif b == 0x0a:
                continue
            elif b in (0x08, 0x7f):
                if self.line:
                    self.line.pop()
                    if self.echo:
                        self.send(b"\b \b")
            else:
                self.line.append(b)
                if self.echo:
                    self.send(bytes([b]))

    def command(self, cmd):
        log(f"cmd: {cmd}")
        up = cmd.upper()
        if not up.startswith("AT"):
            self.reply("ERROR")
            return
        body = cmd[2:].strip()
        ub = body.upper()
        if ub in ("", "Z"):
            if ub == "Z":
                self.echo = True
            self.reply("OK")
        elif ub in ("E0", "E1"):
            self.echo = ub == "E1"
            self.reply("OK")
        elif ub == "I":
            self.reply("atari-chat modem emulator")
            self.reply("OK")
        elif ub in ("H", "H0"):
            self.reply("OK")
        elif ub.startswith("D"):
            target = body[1:]
            if target[:1].upper() in ("T", "P"):
                target = target[1:]
            self.dial(target.strip())
        else:
            self.reply("ERROR")

    @staticmethod
    def connect(host, port):
        """Try IPv4 addresses first; many home networks have no IPv6 route."""
        infos = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
        infos.sort(key=lambda i: i[0] != socket.AF_INET)
        err = OSError(f"no addresses for {host}")
        for family, type_, proto, _, addr in infos:
            s = socket.socket(family, type_, proto)
            s.settimeout(15)
            try:
                s.connect(addr)
                return s
            except OSError as e:
                s.close()
                err = e
        raise err

    def dial(self, target):
        host, _, port = target.rpartition(":")
        if not host:
            host, port = target, "23"
        try:
            port = int(port)
            log(f"dialing {host}:{port}")
            self.sock = self.connect(host, port)
            self.sock.setblocking(False)
            self.tx_bytes = self.rx_bytes = 0
            self.last_rx = time.monotonic()
            self.pending_escape = None
            log(f"connected to {host}:{port}")
            self.reply(f"CONNECT {int(self.bytes_per_sec * 10)}")
        except (OSError, ValueError) as e:
            log(f"dial failed: {e}")
            self.reply("NO CARRIER")

    # ---- data mode ------------------------------------------------------

    def data_bytes(self, data):
        now = time.monotonic()
        if data == b"+++" and now - self.last_rx >= GUARD and self.pending_escape is None:
            self.pending_escape = now
            return
        if self.pending_escape is not None:
            data = b"+++" + data
            self.pending_escape = None
        self.last_rx = now
        self.to_socket(data)

    def to_socket(self, data):
        if self.verbose:
            log(f"ST -> net {len(data)} bytes: {data[:64]!r}")
        self.tx_bytes += len(data)
        try:
            self.sock.sendall(data)
        except OSError as e:
            log(f"send failed: {e}")
            self.hangup(notify=True)

    def socket_readable(self):
        try:
            data = self.sock.recv(4096)
        except BlockingIOError:
            return
        except OSError as e:
            log(f"recv failed: {e}")
            data = b""
        if not data:
            log("remote closed")
            self.hangup(notify=True)
            return
        if self.verbose:
            log(f"net -> ST {len(data)} bytes: {data[:64]!r}")
        self.rx_bytes += len(data)
        self.send(data)

    def unread(self):
        """Bytes written to the serial line that the ST side hasn't taken yet."""
        buf = fcntl.ioctl(self.slave_fd, termios.FIONREAD, b"\0\0\0\0")
        return struct.unpack("i", buf)[0] + len(self.outq)

    def hangup(self, notify):
        if self.sock:
            log(f"hangup (ST sent {self.tx_bytes} bytes, received {self.rx_bytes}, "
                f"{self.unread()} not yet read by the ST)")
            self.sock.close()
            self.sock = None
        if notify:
            self.reply("NO CARRIER")

    def tick(self):
        if self.pending_escape is not None and time.monotonic() - self.pending_escape >= GUARD:
            log("escape to command mode")
            self.pending_escape = None
            self.hangup(notify=False)
            self.reply("OK")

    # ---- main loop ------------------------------------------------------

    def run(self):
        while True:
            fds = [self.fd]
            if self.sock and len(self.outq) < 4096:   # backpressure onto TCP
                fds.append(self.sock)
            readable, _, _ = select.select(fds, [], [], 0.005 if self.outq else 0.1)
            if self.fd in readable:
                try:
                    data = os.read(self.fd, 4096)
                except OSError:
                    data = b""
                if data:
                    if self.sock:
                        self.data_bytes(data)
                    else:
                        self.command_bytes(data)
            if self.sock and self.sock in readable:
                self.socket_readable()
            self.tick()
            self.pace()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--link", default="emu/serial", help="symlink to create for Hatari's --rs232-in/out")
    ap.add_argument("--baud", type=int, default=19200, help="serial speed to emulate")
    ap.add_argument("-v", "--verbose", action="store_true", help="log data passing through")
    args = ap.parse_args()

    master, slave = pty.openpty()
    tty.setraw(slave)
    slave_name = os.ttyname(slave)

    if os.path.lexists(args.link):
        os.unlink(args.link)
    os.symlink(slave_name, args.link)
    log(f"serial port {slave_name} linked at {args.link}")

    modem = Modem(master, slave, args.verbose, args.baud)
    signal.signal(signal.SIGUSR1, lambda *_: log(f"status: {modem.unread()} bytes not yet read by the ST"))
    try:
        modem.run()
    except KeyboardInterrupt:
        pass
    finally:
        if os.path.islink(args.link):
            os.unlink(args.link)


if __name__ == "__main__":
    main()
