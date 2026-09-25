from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


def resolve_source(source: Path) -> Path | None:
    if source.is_file():
        return source

    candidates = (
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        Path("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"),
        Path("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"),
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate

    fc_match = shutil.which("fc-match")
    if fc_match:
        result = subprocess.run(
            [fc_match, "-f", "%{file}", "sans-serif"],
            check=False,
            capture_output=True,
            text=True,
        )
        matched = Path(result.stdout.strip())
        if matched.is_file():
            return matched
    return None


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: stage_font.py <source-font> <dest-font>", file=sys.stderr)
        return 1

    source = Path(sys.argv[1])
    dest = Path(sys.argv[2])

    resolved_source = resolve_source(source)
    if resolved_source is None:
        print(
            f"font source not found: {source}; install a system TTF or set UI_FONT_SOURCE",
            file=sys.stderr,
        )
        return 1

    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(resolved_source, dest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
