#!/usr/bin/env python3
"""Headless end-to-end tests: boots DrakeOS in QEMU, types shell commands through the QEMU
monitor (sendkey) and checks the console output mirrored on the serial port.

Usage: python3 tests/boot_test.py [disk|grub|direct ...]   (default: disk grub)
Never runs the OS natively: everything happens inside qemu-system-i386.
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")

KEYS = {" ": "spc", "\n": "ret", "-": "minus", "/": "slash", ".": "dot", "&": "shift-7",
        "_": "shift-minus", ",": "comma", "=": "equal"}

# (command typed at the prompt, text expected on the console afterwards)
SCENARIO = [
    ("echo ready", "ready"),
    ("ls", "readme.txt"),
    ("cat motd.txt", "dragon"),
    ("run hello a b", "argv[2] = b"),
    ("run ucat /motd.txt", "dragon"),
    ("run spawn", "exited with 0"),
    ("run fault", "killed by exception 14"),
    ("run fault cli", "killed by exception 13"),
    ("format", "DrakeFS created"),
    ("write note dragons fly", ""),
    ("cat disk/note", "dragons fly"),
    ("ls disk", "note"),
    ("run ticker 30 &", "started"),
    ("ps", "ticker"),
    ("run spin 2", "round 2 done"),  # busy loop in the foreground, ticker still running
    ("mem", "kernel heap"),
    ("gfx", ""),
    (" ", ""),                        # any key leaves graphics mode
    ("echo back to text", "back to text"),
    ("uptime", "ticks"),
]


class Machine:
    def __init__(self, boot, workdir):
        self.serial_path = os.path.join(workdir, "serial.log")
        self.monitor_path = os.path.join(workdir, "monitor.sock")
        data = os.path.join(workdir, "data.img")
        with open(data, "wb") as f:
            f.truncate(4 * 1024 * 1024)
        args = ["qemu-system-i386", "-m", "128M", "-display", "none", "-no-reboot",
                "-serial", "file:" + self.serial_path,
                "-monitor", "unix:%s,server,nowait" % self.monitor_path,
                "-audiodev", "none,id=snd0", "-machine", "pc,pcspk-audiodev=snd0",
                "-drive", "file=%s,format=raw,if=ide,index=1" % data]
        if boot == "disk":
            image = os.path.join(workdir, "drakeos.img")
            shutil.copy(os.path.join(BUILD, "drakeos.img"), image)
            args += ["-drive", "file=%s,format=raw,if=ide,index=0" % image]
        elif boot == "grub":
            args += ["-cdrom", os.path.join(BUILD, "drakeos.iso"), "-boot", "d"]
        else:
            args += ["-kernel", os.path.join(BUILD, "kernel.elf")]
        self.proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.monitor = None

    def output(self):
        try:
            with open(self.serial_path, errors="replace") as f:
                return f.read()
        except FileNotFoundError:
            return ""

    def wait_for(self, text, start=0, timeout=20):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if text in self.output()[start:]:
                return True
            if self.proc.poll() is not None:
                return False
            time.sleep(0.1)
        return False

    def connect(self):
        for _ in range(50):
            try:
                s = socket.socket(socket.AF_UNIX)
                s.connect(self.monitor_path)
                self.monitor = s
                s.settimeout(1)
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("cannot connect to the QEMU monitor")

    def type(self, text):
        for ch in text:
            key = KEYS.get(ch, ch)
            if ch.isupper():
                key = "shift-" + ch.lower()
            self.monitor.sendall(("sendkey %s\n" % key).encode())
            time.sleep(0.04)
        try:
            while self.monitor.recv(4096):
                pass
        except socket.timeout:
            pass

    def stop(self):
        self.proc.kill()
        self.proc.wait()


def run(boot):
    print("== boot path: %s" % boot)
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        m = Machine(boot, tmp)
        try:
            if not m.wait_for("boot complete", timeout=30):
                print("  FAIL boot (serial log follows)\n" + m.output())
                return 1
            print("  ok   boot")
            m.connect()
            m.wait_for("drake> ", timeout=5)
            for command, expected in SCENARIO:
                start = len(m.output())
                m.type(command + ("\n" if command.strip() else ""))
                ok = m.wait_for(expected, start, timeout=15) if expected else (time.sleep(1.5) or True)
                print("  %s %s" % ("ok  " if ok else "FAIL", command.strip() or "<key>"))
                if not ok:
                    failures += 1
                    print("    expected %r; output was:\n%s" % (expected, m.output()[start:]))
            if "PANIC" in m.output():
                print("  FAIL kernel panic in log")
                failures += 1
        finally:
            m.stop()
    return failures


def main():
    paths = sys.argv[1:] or ["disk", "grub"]
    failures = sum(run(p) for p in paths)
    print("\n%s" % ("all tests passed" if not failures else "%d failure(s)" % failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
