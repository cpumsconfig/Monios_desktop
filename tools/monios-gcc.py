#!/usr/bin/env python3
"""Compile a C source file with the MoniOS user-mode ABI."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]
DEFAULT_CC = "x86_64-w64-mingw32-gcc"
DEFAULT_FLAGS = [
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-fno-asynchronous-unwind-tables",
    "-mno-red-zone",
    "-m64",
    "-mabi=sysv",
    "-fno-pie",
]


def main() -> int:
    forwarded: list[str] = []
    use_defaults = True
    compiler = os.environ.get("MONIOS_CC") or os.environ.get("APP_CC") or DEFAULT_CC

    for argument in sys.argv[1:]:
        if argument == "--monios-no-defaults":
            use_defaults = False
        elif argument.startswith("--monios-cc="):
            compiler = argument.split("=", 1)[1]
        else:
            forwarded.append(argument)

    if not forwarded:
        print("usage: monios-gcc.py -c source.c -o output.o", file=sys.stderr)
        return 2

    command = [compiler]
    if use_defaults:
        command.extend([
            "-I",
            str(PROJECT_DIR / "include"),
            "-I",
            str(PROJECT_DIR / "user" / "lib"),
            *DEFAULT_FLAGS,
        ])
    command.extend(forwarded)
    return subprocess.run(command, cwd=PROJECT_DIR).returncode


if __name__ == "__main__":
    raise SystemExit(main())
