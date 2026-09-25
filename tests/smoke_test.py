#!/usr/bin/env python3
"""
smoke_test.py - Monios x64 end-to-end boot smoke test.

Boots hd.img under QEMU (qemu-system-x86_64) with the serial port on stdio
and watches the serial log for boot milestones:

  1. kernel sign-on        : "KERNEL BOOT" (or the RFP123 banner)
  2. memory / MMU up       : "init paging" or "mmu:"
  3. filesystem mounted    : "filesystem mounted"  (C: drive usable)
  4. shell / UI coming up   : "init shell" or "osui: component registry ready"

If every milestone is seen within TIMEOUT seconds the test PASSES. If QEMU
is not installed the test degrades gracefully (reports SKIP, exit code 0).

Usage:
    python smoke_test.py [--hd ../hd.img] [--timeout 60]
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# (milestone name, substring to look for in the serial log)
MILESTONES = [
    ("kernel sign-on",       ["kernel boot", "rfp123"]),
    ("memory / mmu init",    ["init paging", "mmu:"]),
    ("filesystem mounted",   ["filesystem mounted"]),
    ("shell / ui started",   ["init shell", "osui: component registry ready"]),
]

QEMU_CANDIDATES = [
    "qemu-system-x86_64",
    r"C:\Program Files\qemu\qemu-system-x86_64.exe",
    r"C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
]


def find_qemu():
    for cand in QEMU_CANDIDATES:
        path = shutil.which(cand) or (os.path.isfile(cand) and cand)
        if path:
            return path
    return None


def build_cmd(qemu, hd_img):
    return [
        qemu,
        "-monitor", "none",
        "-serial", "stdio",
        "-display", "none",
        "-m", "256M",
        # Match the supported `make run_debug` boot: the kernel probes the
        # e1000 NIC and faults if no net device is present.
        "-netdev", "user,id=net0",
        "-device", "e1000,netdev=net0",
        "-device", "isa-debug-exit,iobase=0x501,iosize=0x02",
        "-drive", "file=%s,format=raw" % hd_img,
        "-no-reboot",
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hd", default=os.path.join(ROOT, "hd.img"))
    ap.add_argument("--timeout", type=float, default=60.0)
    args = ap.parse_args()

    print("Monios x64 boot smoke test")
    print("  hd image : %s" % args.hd)
    print("  timeout  : %.0fs" % args.timeout)

    qemu = find_qemu()
    if not qemu:
        print("RESULT: SKIP - qemu-system-x86_64 not found on PATH.")
        print("        Install QEMU or add it to PATH to run the E2E smoke test.")
        return 0
    print("  qemu     : %s" % qemu)

    if not os.path.isfile(args.hd):
        print("RESULT: FAIL - hd image not found: %s" % args.hd)
        return 1

    cmd = build_cmd(qemu, args.hd)
    print("  command  : %s" % " ".join(cmd))
    print("-" * 60)

    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL, bufsize=1, universal_newlines=True,
    )

    reached = [False] * len(MILESTONES)
    deadline = time.time() + args.timeout
    ok = False

    try:
        while time.time() < deadline:
            if proc.poll() is not None:
                break
            line = proc.stdout.readline()
            if not line:
                time.sleep(0.05)
                continue
            line = line.rstrip("\r\n")
            if line:
                print("  | %s" % line)
            low = line.lower()
            for i, (_, needles) in enumerate(MILESTONES):
                if not reached[i] and any(n in low for n in needles):
                    reached[i] = True
                    print("  >> milestone reached: %s" % MILESTONES[i][0])
            if all(reached):
                ok = True
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    print("-" * 60)
    for i, (name, _) in enumerate(MILESTONES):
        print("  [%s] %s" % ("PASS" if reached[i] else "WAIT", name))

    if ok:
        print("RESULT: PASS - all boot milestones reached within %.0fs" % args.timeout)
        return 0
    print("RESULT: FAIL - timeout or early exit; reached %d/%d milestones"
          % (sum(reached), len(MILESTONES)))
    return 1


if __name__ == "__main__":
    sys.exit(main())
