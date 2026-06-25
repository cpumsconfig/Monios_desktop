from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


RZS_VERSION = 1
RZS_SIGNATURE_SIZE = 64
RZS_IMAGE_FLAG_CONSOLE = 0x00000001
RZS_IMAGE_FLAG_DRIVER = 0x00000004
RZS_IMAGE_FLAG_SIGNED = 0x00000008
RZS_IMAGE_FLAG_NEEDS_R0 = 0x00000010
RZS_IMAGE_FLAG_NEEDS_R2 = 0x00000020
RZS_SIGNATURE_KEY = b"MONIOS-RZS-DEV-KEY"


def rzs_signature(digest: bytes) -> bytes:
    first = hashlib.sha256(RZS_SIGNATURE_KEY + digest + RZS_SIGNATURE_KEY).digest()
    second = hashlib.sha256(digest + RZS_SIGNATURE_KEY + first).digest()
    return first + second


def main() -> None:
    parser = argparse.ArgumentParser(description="Wrap an ELF64 image as a signed MONIOS .rzs driver")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--flags", default="driver,console,r2", help="comma separated: driver,console,r0,r2")
    args = parser.parse_args()

    image = args.input.read_bytes()
    if len(image) < 4 or image[:4] != b"\x7fELF":
        raise SystemExit(f"{args.input} is not an ELF image")

    flags = RZS_IMAGE_FLAG_SIGNED
    requested = {part.strip().lower() for part in args.flags.split(",") if part.strip()}
    if "console" in requested:
        flags |= RZS_IMAGE_FLAG_CONSOLE
    if "driver" in requested:
        flags |= RZS_IMAGE_FLAG_DRIVER
    if "r2" in requested:
        flags |= RZS_IMAGE_FLAG_NEEDS_R2
    if "r0" in requested:
        flags |= RZS_IMAGE_FLAG_NEEDS_R0

    digest = hashlib.sha256(image).digest()
    signature = rzs_signature(digest)
    header = struct.pack(
        "<4sHHIII32s64s",
        b"RZS1",
        RZS_VERSION,
        116,
        len(image),
        flags,
        RZS_SIGNATURE_SIZE,
        digest,
        signature,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + image)


if __name__ == "__main__":
    main()
