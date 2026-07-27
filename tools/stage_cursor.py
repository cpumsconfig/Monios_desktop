from __future__ import annotations

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Stage a Windows cursor for MoniOS media")
    parser.add_argument("source", type=Path)
    parser.add_argument("dest", type=Path)
    args = parser.parse_args()

    args.dest.parent.mkdir(parents=True, exist_ok=True)
    if args.source.exists() and args.source.is_file():
        args.dest.write_bytes(args.source.read_bytes())
    else:
        args.dest.write_bytes(b"")


if __name__ == "__main__":
    main()
