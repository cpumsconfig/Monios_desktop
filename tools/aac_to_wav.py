from __future__ import annotations

import argparse
import math
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path


def write_placeholder(path: Path, seconds: int = 8) -> None:
    rate = 44100
    frames = rate * seconds
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        for i in range(frames):
            env = 0.35 if i < frames - rate else 0.35 * (frames - i) / rate
            sample = int(12000 * env * math.sin(2.0 * math.pi * 440.0 * i / rate))
            wav.writeframesraw(struct.pack("<hh", sample, sample))


def convert_with_ffmpeg(input_path: Path, output_path: Path) -> bool:
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        return False
    subprocess.check_call(
        [
            ffmpeg,
            "-y",
            "-i",
            str(input_path),
            "-ac",
            "2",
            "-ar",
            "44100",
            "-sample_fmt",
            "s16",
            str(output_path),
        ]
    )
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert AAC/M4A to MoniOS-compatible WAV.")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--placeholder-ok", action="store_true", help="write a test WAV if ffmpeg is unavailable")
    args = parser.parse_args()

    if not args.input.exists():
        print(f"input not found: {args.input}", file=sys.stderr)
        return 2
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if convert_with_ffmpeg(args.input, args.output):
        return 0
    if args.placeholder_ok:
        write_placeholder(args.output)
        print("ffmpeg not found; wrote placeholder WAV", file=sys.stderr)
        return 0
    print("ffmpeg not found; install ffmpeg or pass --placeholder-ok", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
