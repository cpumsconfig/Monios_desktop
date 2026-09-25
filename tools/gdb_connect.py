#!/usr/bin/env python3
"""
gdb_connect.py - bridge between MoniOS's COM1 GDB stub and host GDB.

MoniOS runs its GDB remote protocol over COM1 (0x3F8).  Under QEMU you can
expose that serial port as a TCP socket instead of stdio:

    qemu-system-x86_64 -serial tcp:127.0.0.1:1235,server,nowait ...

Then this script listens on a second local port (default 1234) for GDB and
forwards bytes both ways to QEMU's serial socket on 1235:

    python tools/gdb_connect.py --qemu-port 1235 --gdb-port 1234

Finally, in GDB:
    file out/kernel.elf
    target remote localhost:1234
    continue

If you run QEMU with `-serial stdio`, the GDB stub shares the console with
kernel log output; use this bridge only when you give the serial its own
TCP socket.  GDB can also connect straight to the QEMU socket
(`target remote localhost:1235`) without this bridge; the bridge is mainly
useful when the stub lives behind a real COM port (pyserial path) or when
you want a fixed, stable GDB port while QEMU's port may vary.
"""
import argparse
import socket
import sys
import threading


def pipe(a, b):
    try:
        while True:
            data = a.recv(4096)
            if not data:
                break
            b.sendall(data)
    except OSError:
        pass
    finally:
        for s in (a, b):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def open_qemu(args):
    if args.com:
        import serial  # pyserial
        ser = serial.Serial(args.com, 115200, timeout=0)
        return ser
    s = socket.create_connection((args.qemu_host, args.qemu_port))
    return s


def pump_serial_to_socket(ser, sock):
    try:
        while True:
            data = ser.read(4096)
            if data:
                sock.sendall(data)
    except OSError:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--qemu-host", default="127.0.0.1")
    ap.add_argument("--qemu-port", type=int, default=1235,
                    help="TCP port QEMU exposes the serial on")
    ap.add_argument("--gdb-port", type=int, default=1234,
                    help="local port GDB should connect to")
    ap.add_argument("--com", default=None,
                    help="optional: use a real COM port (e.g. COM4) instead of TCP")
    args = ap.parse_args()

    ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", args.gdb_port))
    ls.listen(1)
    print("gdb_connect: waiting for GDB on localhost:%d ..." % args.gdb_port)
    gdb, _ = ls.accept()
    print("gdb_connect: GDB connected, opening QEMU serial ...")

    if args.com:
        ser = open_qemu(args)
        threading.Thread(target=pump_serial_to_socket, args=(ser, gdb),
                         daemon=True).start()
        try:
            while True:
                data = gdb.recv(4096)
                if not data:
                    break
                ser.write(data)
        except OSError:
            pass
    else:
        q = open_qemu(args)
        print("gdb_connect: bridged localhost:%d <-> %s:%d" %
              (args.gdb_port, args.qemu_host, args.qemu_port))
        threading.Thread(target=pipe, args=(gdb, q), daemon=True).start()
        pipe(q, gdb)

    print("gdb_connect: closed.")


if __name__ == "__main__":
    main()
