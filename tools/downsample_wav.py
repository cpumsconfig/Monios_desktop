from __future__ import annotations

import argparse
import wave
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("dest", type=Path)
    parser.add_argument("--rate", type=int, default=11025)
    args = parser.parse_args()

    with wave.open(str(args.source), "rb") as src:
        channels = src.getnchannels()
        sample_width = src.getsampwidth()
        source_rate = src.getframerate()
        frames = src.readframes(src.getnframes())

    if channels != 2 or sample_width != 2:
        raise SystemExit("expected stereo 16-bit PCM input")
    frame_size = channels * sample_width
    source_frames = len(frames) // frame_size
    target_frames = (source_frames * args.rate + source_rate - 1) // source_rate
    converted = bytearray(target_frames * frame_size)
    out_pos = 0
    for out_frame in range(target_frames):
        frame_index = (out_frame * source_rate) // args.rate
        if frame_index >= source_frames:
            frame_index = source_frames - 1
        pos = frame_index * frame_size
        converted[out_pos:out_pos + frame_size] = frames[pos:pos + frame_size]
        out_pos += frame_size
    converted = bytes(converted[:out_pos])

    with wave.open(str(args.dest), "wb") as dst:
        dst.setnchannels(channels)
        dst.setsampwidth(sample_width)
        dst.setframerate(args.rate)
        dst.writeframes(converted)


if __name__ == "__main__":
    main()
