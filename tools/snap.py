#!/usr/bin/env python3
"""
Headless test run: boot Hatari with no window, auto-start one program,
wait, and save a screenshot. Used to check changes without clicking around.

    tools/snap.py NETTEST.TOS --wait 30 --out /tmp/shot.png
"""
import argparse
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("program", help="file in build/, e.g. NETTEST.TOS")
    ap.add_argument("--wait", type=float, default=10, help="seconds to run before the screenshot")
    ap.add_argument("--out", default="snap.png")
    ap.add_argument("-v", "--verbose", action="store_true", help="log modem traffic")
    ap.add_argument("--every", type=float, default=0,
                    help="also save a screenshot every N seconds as OUT-<secs>.png")
    ap.add_argument("--keys", default="",
                    help="text to type: letters, digits, space; \\n for Return")
    ap.add_argument("--keys-at", type=float, default=5, help="seconds after boot to start typing")
    args = ap.parse_args()

    tmp = tempfile.mkdtemp(prefix="atari-snap-")
    ctl_path = os.path.join(tmp, "ctl")
    serial = os.path.join(tmp, "serial")

    modem = subprocess.Popen([sys.executable, os.path.join(ROOT, "tools/modem.py"), "--link", serial] + (["-v"] if args.verbose else []),
                             stderr=open(os.path.join(tmp, "modem.log"), "w"))
    while not os.path.exists(serial):
        time.sleep(0.1)

    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(ctl_path)
    srv.listen(1)

    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    hatari = subprocess.Popen(
        ["hatari", "--machine", "megaste", "--cpuclock", "8", "--memsize", "4096", "--tos", "emu/etos256us.img",
         "--monitor", "mono", "--harddrive", "build", "--gemdos-drive", "C",
         "--fast-boot", "true", "--confirm-quit", "false", "--sound", "off",
         "--rs232-in", serial, "--rs232-out", serial,
         "--screenshot-dir", tmp, "--control-socket", ctl_path,
         "--auto", "C:\\" + args.program],
        cwd=ROOT, env=env, stdout=open(os.path.join(tmp, "hatari.log"), "w"), stderr=subprocess.STDOUT)

    srv.settimeout(20)
    ctl, _ = srv.accept()

    def cmd(s):
        ctl.sendall((s + "\n").encode())
        time.sleep(0.3)

    # Hatari takes a character, or a scancode for keys that aren't one.
    scancodes = {" ": "57", "\n": "28"}
    keys = args.keys.replace("\\n", "\n")
    time.sleep(args.keys_at)
    for k in keys:
        cmd("hatari-event keypress " + scancodes.get(k, k))
    elapsed = args.keys_at + 0.3 * len(keys)
    base, ext = os.path.splitext(args.out)
    while args.every and elapsed + args.every < args.wait:
        time.sleep(args.every)
        elapsed += args.every
        cmd("hatari-shortcut screenshot")
        time.sleep(1)
        shots = sorted(f for f in os.listdir(tmp) if f.startswith("grab"))
        if shots:
            shutil.copy(os.path.join(tmp, shots[-1]), f"{base}-{int(elapsed):03d}{ext}")
            print(f"checkpoint {int(elapsed)}s", flush=True)
    time.sleep(max(0, args.wait - elapsed))
    cmd("hatari-shortcut screenshot")
    time.sleep(2)

    modem.send_signal(signal.SIGUSR1)
    time.sleep(0.5)
    hatari.terminate()
    hatari.wait(5)
    modem.terminate()

    shots = sorted(f for f in os.listdir(tmp) if f.startswith("grab"))
    if shots:
        shutil.copy(os.path.join(tmp, shots[-1]), args.out)
        print(f"screenshot: {args.out}")
    print("modem log:")
    print(open(os.path.join(tmp, "modem.log")).read())
    shutil.rmtree(tmp)


if __name__ == "__main__":
    main()
