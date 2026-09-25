#!/usr/bin/env python3
"""
analyze_dump.py - parse MoniOS C:\\panic.dmp binary memory dumps.

Usage:
    python analyze_dump.py panic.dmp                 # print header + summary
    python analyze_dump.py panic.dmp -x <hexaddr>    # show memory at physical addr
    python analyze_dump.py panic.dmp -s <string>    # search all dumped pages for string
    python analyze_dump.py panic.dmp --strings       # dump printable strings from pages
    python analyze_dump.py panic.dmp --extract out.bin [--addr <phys> --len <n>]
                                                     # extract raw page(s) for gdb

Dump format (see include/memdump.h):
    struct memdump_header_t  (packed, 128 bytes)
    struct memdump_page_record_t { uint64 phys; uint8 data[4096]; } * num_pages
"""
import argparse
import struct
import sys

MAGIC = 0x4D4F4E49504D4450  # "MONIPDMP"
PAGE = 4096

# header layout: all little-endian uint64
HEADER_FIELDS = [
    "magic", "version", "header_size", "dump_size", "uptime_ticks",
    "vector", "error_code", "rip", "rsp", "rbp", "rflags",
    "cr2", "cr3", "kernel_phys", "kernel_size", "num_pages",
    "max_phys_scanned", "flags",
]
# then gpr[16], reserved[8]
HEADER_SIZE = (len(HEADER_FIELDS) + 16 + 8) * 8  # 384 bytes

REASON_NAMES = {
    0: "#DE divide-by-zero", 1: "#DB debug", 2: "#NMI",
    3: "#BP breakpoint", 4: "#OF overflow", 5: "#BR bound",
    6: "#UD invalid opcode", 7: "#NM device not available",
    8: "#DF double fault", 10: "#TS invalid TSS", 11: "#NP segment not present",
    12: "#SS stack fault", 13: "#GP general protection",
    14: "#PF page fault", 16: "#MF x87 FP", 17: "#AC alignment check",
    18: "#MC machine check", 19: "#XM SIMD FP",
}


def read_header(f):
    f.seek(0)
    raw = f.read(HEADER_SIZE)
    if len(raw) < HEADER_SIZE:
        sys.exit("error: file too small for a dump header")
    vals = struct.unpack("<%dQ" % (HEADER_SIZE // 8), raw)
    h = dict(zip(HEADER_FIELDS, vals[:len(HEADER_FIELDS)]))
    h["gpr"] = list(vals[len(HEADER_FIELDS):len(HEADER_FIELDS) + 16])
    if h["magic"] != MAGIC:
        sys.exit("error: bad magic 0x%x (not a MoniOS panic.dmp)" % h["magic"])
    return h


def iter_pages(f, h):
    off = h["header_size"]
    f.seek(off)
    rec = 8 + PAGE
    for _ in range(h["num_pages"]):
        chunk = f.read(rec)
        if len(chunk) < rec:
            break
        (phys,) = struct.unpack("<Q", chunk[:8])
        yield phys, chunk[8:8 + PAGE]


def hexdump(data, base=0, width=16):
    out = []
    for i in range(0, len(data), width):
        chunk = data[i:i + width]
        hexs = " ".join("%02x" % b for b in chunk)
        asci = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append("0x%08x  %-*s  |%s|" % (base + i, width * 3 - 1, hexs, asci))
    return "\n".join(out)


def print_header(h):
    print("=== MoniOS panic.dmp header ===")
    print("version:          %d" % h["version"])
    print("dump_size:        %d bytes (%.2f MB)" % (h["dump_size"], h["dump_size"] / 1e6))
    print("uptime_ticks:     %d" % h["uptime_ticks"])
    vec = h["vector"]
    print("exception:        vector=%d error_code=0x%x (%s)" %
          (vec, h["error_code"], REASON_NAMES.get(vec, "unknown")))
    print("rip:              0x%016x" % h["rip"])
    print("rsp:              0x%016x" % h["rsp"])
    print("rbp:              0x%016x" % h["rbp"])
    print("rflags:           0x%016x" % h["rflags"])
    print("cr2:              0x%016x" % h["cr2"])
    print("cr3:              0x%016x" % h["cr3"])
    print("kernel_phys:      0x%016x" % h["kernel_phys"])
    print("kernel_size:      0x%016x" % h["kernel_size"])
    print("num_pages:        %d" % h["num_pages"])
    print("max_phys_scanned: 0x%x" % h["max_phys_scanned"])
    flags = h["flags"]
    print("flags:            0x%x%s" % (flags, " [TRUNCATED]" if flags & 1 else ""))
    names = ["rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "r8",
             "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
    print("--- GPRs ---")
    for i, n in enumerate(names):
        print("  %-4s = 0x%016x" % (n, h["gpr"][i]))


def find_page(f, h, addr):
    target = addr & ~(PAGE - 1)
    for phys, data in iter_pages(f, h):
        if phys == target:
            off = addr - phys
            return phys, data[off:]
    return None, None


def search_string(f, h, needle):
    if isinstance(needle, str):
        needle = needle.encode("utf-8", "ignore")
    hits = 0
    for phys, data in iter_pages(f, h):
        idx = data.find(needle)
        if idx >= 0:
            s = max(0, idx - 16)
            e = min(len(data), idx + len(needle) + 48)
            ctx = data[s:e]
            printable = "".join(chr(b) if 32 <= b < 127 else "." for b in ctx)
            print("phys 0x%08x+0x%x: %s" % (phys, idx, printable))
            hits += 1
            if hits >= 50:
                print("... (stopped after 50 hits)")
                break
    if hits == 0:
        print("no matches for %r" % needle)


def dump_strings(f, h, minlen=6):
    for phys, data in iter_pages(f, h):
        cur = bytearray()
        start = 0
        for i, b in enumerate(data):
            if 32 <= b < 127:
                if not cur:
                    start = i
                cur.append(b)
            else:
                if len(cur) >= minlen:
                    print("0x%08x: %s" % (phys + start, cur.decode("ascii", "ignore")))
                cur = bytearray()
        if len(cur) >= minlen:
            print("0x%08x: %s" % (phys + start, cur.decode("ascii", "ignore")))


def extract(f, h, outpath, addr, length):
    with open(outpath, "wb") as o:
        if addr is not None:
            phys, data = find_page(f, h, addr)
            if data is None:
                sys.exit("error: address 0x%x not present in dump" % addr)
            if length is None:
                length = len(data)
            o.write(data[:length])
            print("wrote %d bytes from phys 0x%x -> %s" % (min(length, len(data)), phys, outpath))
        else:
            for p, data in iter_pages(f, h):
                o.write(data)
            print("wrote all dumped pages -> %s" % outpath)


def main():
    ap = argparse.ArgumentParser(description="Analyze MoniOS panic.dmp")
    ap.add_argument("dump")
    ap.add_argument("-x", "--hexaddr", help="physical address to display (hex)")
    ap.add_argument("-s", "--search", help="search pages for a string")
    ap.add_argument("--strings", action="store_true", help="print printable strings")
    ap.add_argument("--extract", metavar="OUT", help="extract raw memory to OUT")
    ap.add_argument("--len", type=lambda v: int(v, 0), help="length for -x/--extract")
    args = ap.parse_args()

    with open(args.dump, "rb") as f:
        h = read_header(f)
        print_header(h)
        print()
        if args.hexaddr is not None:
            addr = int(args.hexaddr, 16) if isinstance(args.hexaddr, str) else args.hexaddr
            phys, data = find_page(f, h, addr)
            if data is None:
                print("address 0x%x not present in dump (page not collected)" % addr)
            else:
                n = args.len if args.len else 256
                print("=== phys 0x%x ===" % addr)
                print(hexdump(data[:n], base=addr))
        if args.search is not None:
            print("=== search %r ===" % args.search)
            search_string(f, h, args.search)
        if args.strings:
            print("=== strings ===")
            dump_strings(f, h)
        if args.extract:
            addr = int(args.hexaddr, 16) if args.hexaddr else None
            extract(f, h, args.extract, addr, args.len)


if __name__ == "__main__":
    main()
