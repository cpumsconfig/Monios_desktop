#!/usr/bin/env python3
"""Link MoniOS user objects into a PE32+ application."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]
DEFAULT_LD = "x86_64-w64-mingw32-gcc"
RUNTIME_OBJECTS = (
    "app_runtime.pe.o",
    "appsys.pe.o",
    "stdio.pe.o",
    "stdlib.pe.o",
    "unistd.pe.o",
    "string.pe.o",
)


def project_path(path: Path) -> Path:
    return path if path.is_absolute() else PROJECT_DIR / path


def main() -> int:
    output: str | None = None
    subsystem = "console"
    runtime_dir = PROJECT_DIR / "out"
    library_dir = PROJECT_DIR / "out"
    linker = os.environ.get("MONIOS_LD") or os.environ.get("APP_LD") or DEFAULT_LD
    use_runtime = True
    use_libraries = True
    forwarded: list[str] = []
    inputs: list[str] = []
    index = 0

    arguments = sys.argv[1:]
    while index < len(arguments):
        argument = arguments[index]
        if argument in ("-o", "--output"):
            if index + 1 >= len(arguments):
                print("monios-ld.py: missing output path", file=sys.stderr)
                return 2
            output = arguments[index + 1]
            index += 2
            continue
        if argument.startswith("--subsystem="):
            subsystem = argument.split("=", 1)[1]
            index += 1
            continue
        if argument == "--subsystem":
            if index + 1 >= len(arguments):
                print("monios-ld.py: missing subsystem", file=sys.stderr)
                return 2
            subsystem = arguments[index + 1]
            index += 2
            continue
        if argument.startswith("--runtime-dir="):
            runtime_dir = project_path(Path(argument.split("=", 1)[1]))
            index += 1
            continue
        if argument.startswith("--library-dir="):
            library_dir = project_path(Path(argument.split("=", 1)[1]))
            index += 1
            continue
        if argument.startswith("--monios-ld="):
            linker = argument.split("=", 1)[1]
            index += 1
            continue
        if argument == "--no-runtime":
            use_runtime = False
            index += 1
            continue
        if argument == "--no-libraries":
            use_libraries = False
            index += 1
            continue
        if argument.startswith("-"):
            forwarded.append(argument)
        else:
            inputs.append(argument)
        index += 1

    if output is None or not inputs:
        print("usage: monios-ld.py --subsystem console -o app.exe app.o", file=sys.stderr)
        return 2
    if subsystem not in ("console", "windows", "native"):
        print("monios-ld.py: subsystem must be console, windows, or native", file=sys.stderr)
        return 2

    command = [
        linker,
        "-nostdlib",
        "-nodefaultlibs",
        "-nostartfiles",
        "-Wl,-T," + str(PROJECT_DIR / "user" / "apps" / "app.ld"),
        "-Wl,--entry,_start",
        "-Wl,--image-base,0x03fff000",
        "-Wl,--subsystem," + subsystem,
        "-o",
        output,
    ]
    if use_runtime:
        missing = [name for name in RUNTIME_OBJECTS if not (runtime_dir / name).exists()]
        if missing:
            print(
                "monios-ld.py: missing runtime objects; run 'make app-runtime' or 'make hello' first: "
                + ", ".join(missing),
                file=sys.stderr,
            )
            return 1
        command.extend(str(runtime_dir / name) for name in RUNTIME_OBJECTS)
    command.extend(inputs)
    if use_libraries:
        command.extend([
            "-L" + str(library_dir),
            "-lconsole",
            "-lwindows",
            "-lmonios",
        ])
    command.extend(forwarded)
    return subprocess.run(command, cwd=PROJECT_DIR).returncode


if __name__ == "__main__":
    raise SystemExit(main())
