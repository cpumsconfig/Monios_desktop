from __future__ import annotations

import argparse
import hashlib
import os
import struct
from pathlib import Path

RZS_VERSION = 1
RZS_SIGNATURE_SIZE = 64
RZS_IMAGE_FLAG_CONSOLE = 0x00000001
RZS_IMAGE_FLAG_DRIVER = 0x00000004
RZS_IMAGE_FLAG_SIGNED = 0x00000008
RZS_IMAGE_FLAG_NEEDS_R0 = 0x00000010
RZS_IMAGE_FLAG_NEEDS_R2 = 0x00000020

MANIFEST_FORMAT = "<4sHHIII32s"
MANIFEST_SIZE = struct.calcsize(MANIFEST_FORMAT)
HEADER_SIZE = 116


def build_manifest(magic: bytes, version: int, header_size: int, image_size: int, image_flags: int, signature_size: int, image_hash: bytes) -> bytes:
    return struct.pack(
        MANIFEST_FORMAT,
        magic,
        version,
        header_size,
        image_size,
        image_flags,
        signature_size,
        image_hash,
    )


def load_signing_key(path: str | None):
    key_path = path or os.environ.get("MONIOS_RZS_KEY")
    if not key_path:
        return None

    from Crypto.PublicKey import RSA

    key = RSA.import_key(Path(key_path).read_bytes())
    if not key.has_private():
        raise SystemExit(f"{key_path} does not contain an RSA private key")
    if key.size_in_bytes() != RZS_SIGNATURE_SIZE:
        raise SystemExit(f"{key_path} must be a {RZS_SIGNATURE_SIZE * 8}-bit RSA key for RZS v1")
    return key


def main() -> None:
    parser = argparse.ArgumentParser(description="Wrap an ELF64 image as a MONIOS .rzs package")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--flags", default="driver,console,r2", help="comma separated: driver,console,r0,r2")
    parser.add_argument("--key", type=Path, help="RSA private key PEM for signing; defaults to MONIOS_RZS_KEY")
    parser.add_argument("--allow-unsigned", action="store_true", help="write a package with a zero signature for non-privileged test images")
    args = parser.parse_args()

    image = args.input.read_bytes()
    if len(image) < 4 or image[:4] != b"\x7fELF":
        raise SystemExit(f"{args.input} is not an ELF image")

    flags = 0
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
    manifest = build_manifest(b"RZS1", RZS_VERSION, HEADER_SIZE, len(image), flags, RZS_SIGNATURE_SIZE, digest)
    key = load_signing_key(str(args.key) if args.key else None)
    if key is None:
        if not args.allow_unsigned:
            raise SystemExit("RZS signing key required: pass --key or set MONIOS_RZS_KEY")
        signature = bytes(RZS_SIGNATURE_SIZE)
    else:
        from Crypto.Hash import SHA256
        from Crypto.Signature import pkcs1_15

        signature = pkcs1_15.new(key).sign(SHA256.new(manifest))
        if len(signature) != RZS_SIGNATURE_SIZE:
            raise SystemExit("unexpected signature size")

    header = struct.pack(
        "<4sHHIII32s64s",
        b"RZS1",
        RZS_VERSION,
        HEADER_SIZE,
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
