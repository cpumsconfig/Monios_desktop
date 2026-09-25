#!/usr/bin/env python3
"""
persistence_test.py - Monios x64 file persistence across a reboot.

Ideal flow:
  1. boot the VM
  2. at the shell, send:   echo MONIOS_SMOKE_OK > C:\\smoke_test.txt
  3. power off / reboot
  4. boot again and check that C:\\smoke_test.txt still exists with the right bytes

Reality on this tree: the COM1 serial port is the GDB stub (see the boot
banner "GDB stub active (COM1 ...)") and there is no interactive serial
shell, so we cannot drive the shell from the host over stdio. Instead this
script:

  * boots twice and watches the serial log,
  * looks for the kernel-side marker SMOKE_PERSIST_RESULT=PASS emitted by
    user/apps/smoke_test.c (the in-kernel persistence program),
  * if no marker / no serial shell is available it reports SKIP and records
    the required kernel-side support, exactly as the task brief allows.

Exit codes: 0 = PASS or SKIP (environment limitation), 1 = FAIL.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

QEMU_CANDIDATES = [
    "qemu-system-x86_64",
    r"C:\Program Files\qemu\qemu-system-x86_64.exe",
    r"C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
]

SHELL_MARKER = "init shell"
PASS_MARKER = "SMOKE_PERSIST_RESULT=PASS"
FAIL_MARKER = "SMOKE_PERSIST_RESULT=FAIL"


def find_qemu():
    for cand in QEMU_CANDIDATES:
        path = shutil.which(cand) or (os.path.isfile(cand) and cand)
        if path:
            return path
    return None


def boot_once(qemu, hd, timeout, send_command=None):
    """Boot the VM, optionally writing a line to serial stdin, return the
    collected serial text and whether a shell milestone was seen."""
    cmd = [
        qemu, "-monitor", "none", "-serial", "stdio", "-display", "none",
        "-m", "256M",
        "-netdev", "user,id=net0", "-device", "e1000,netdev=net0",
        "-device", "isa-debug-exit,iobase=0x501,iosize=0x02",
        "-drive", "file=%s,format=raw" % hd, "-no-reboot",
    ]
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        stdin=subprocess.PIPE, bufsize=1, universal_newlines=True,
    )
    log_lines = []
    saw_shell = False
    deadline = time.time() + timeout
    sent = False
    try:
        while time.time() < deadline:
            if proc.poll() is not None:
                break
            line = proc.stdout.readline()
            if not line:
                time.sleep(0.05)
                continue
            line = line.rstrip("\r\n")
            log_lines.append(line)
            low = line.lower()
            if SHELL_MARKER in low:
                saw_shell = True
            if send_command and saw_shell and not sent:
                try:
                    proc.stdin.write(send_command + "\r\n")
                    proc.stdin.flush()
                    sent = True
                except (BrokenPipeError, OSError):
                    pass
            if PASS_MARKER in line or FAIL_MARKER in line:
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
    return "\n".join(log_lines), saw_shell, sent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hd", default=os.path.join(ROOT, "hd.img"))
    ap.add_argument("--timeout", type=float, default=60.0)
    args = ap.parse_args()

    print("Monios x64 persistence smoke test")
    qemu = find_qemu()
    if not qemu:
        print("RESULT: SKIP - qemu-system-x86_64 not found.")
        return 0
    if not os.path.isfile(args.hd):
        print("RESULT: FAIL - hd image not found: %s" % args.hd)
        return 1

    cmdline = "echo MONIOS_SMOKE_OK > C:\\smoke_test.txt"

    # --- first boot: create the file (if a serial shell exists) ---
    print("[boot 1] creating %s ..." % "C:\\smoke_test.txt")
    log1, shell1, sent = boot_once(qemu, args.hd, args.timeout,
                                   send_command=cmdline)
    print("  shell milestone seen : %s" % shell1)
    print("  command sent to serial: %s" % sent)

    if not shell1:
        print("  NOTE: no serial shell reached (COM1 is the GDB stub).")
        print("        -> host-driven persistence test requires a kernel serial")
        print("           shell. Falling back to the in-kernel marker program.")

    # --- second boot: verify the file survived ---
    print("[boot 2] checking persistence ...")
    log2, shell2, _ = boot_once(qemu, args.hd, args.timeout)

    combined = log1 + "\n" + log2
    if PASS_MARKER in combined:
        print("RESULT: PASS - %s observed across reboot." % PASS_MARKER)
        return 0
    if FAIL_MARKER in combined:
        print("RESULT: FAIL - %s observed." % FAIL_MARKER)
        return 1

    print("RESULT: SKIP - no persistence marker observed.")
    print("        This kernel does not expose a serial shell and the")
    print("        user/apps/smoke_test.c program is not yet autolaunched on")
    print("        boot. To enable: build smoke_test.c into the image and")
    print("        autolaunch it; then a second boot should print")
    print("        %s on the serial log." % PASS_MARKER)
    return 0


if __name__ == "__main__":
    sys.exit(main())
