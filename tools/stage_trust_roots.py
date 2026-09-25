from __future__ import annotations

import argparse
import struct
from pathlib import Path


MAGIC = b"MONIOS-TRUST-ROOTS1"


def build_bundle(source_dir: Path) -> bytes:
    certificates = sorted(source_dir.glob("*.crt"), key=lambda path: path.name.lower())
    output = bytearray(MAGIC)
    output.extend(struct.pack("<I", len(certificates)))
    for certificate in certificates:
        name = certificate.name.encode("ascii")
        data = certificate.read_bytes()
        if len(name) > 0xFFFF:
            raise ValueError(f"certificate name is too long: {certificate}")
        output.extend(struct.pack("<HI", len(name), len(data)))
        output.extend(name)
        output.extend(data)
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if not args.source_dir.is_dir():
        raise SystemExit(f"trust root directory not found: {args.source_dir}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(build_bundle(args.source_dir))


if __name__ == "__main__":
    main()
