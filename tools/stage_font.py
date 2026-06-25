from __future__ import annotations

import shutil
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: stage_font.py <source-font> <dest-font>", file=sys.stderr)
        return 1

    source = Path(sys.argv[1])
    dest = Path(sys.argv[2])

    if not source.exists():
        print(f"font source not found: {source}", file=sys.stderr)
        return 1

    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, dest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
