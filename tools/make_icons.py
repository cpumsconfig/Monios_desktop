#!/usr/bin/env python3
"""Generate small MoniOS ICO assets used by PE resources."""

from __future__ import annotations

import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ICON_DIR = ROOT / "assets" / "icons"
SIZE = 32


def rgba(r: int, g: int, b: int, a: int = 255) -> tuple[int, int, int, int]:
    return r, g, b, a


def fill(pixels: list[list[tuple[int, int, int, int]]], x: int, y: int, w: int, h: int, color: tuple[int, int, int, int]) -> None:
    for yy in range(max(0, y), min(SIZE, y + h)):
        for xx in range(max(0, x), min(SIZE, x + w)):
            pixels[yy][xx] = color


def line(pixels: list[list[tuple[int, int, int, int]]], x0: int, y0: int, x1: int, y1: int, color: tuple[int, int, int, int]) -> None:
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        if 0 <= x0 < SIZE and 0 <= y0 < SIZE:
            pixels[y0][x0] = color
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def blank() -> list[list[tuple[int, int, int, int]]]:
    return [[rgba(0, 0, 0, 0) for _ in range(SIZE)] for _ in range(SIZE)]


def write_ico(path: Path, pixels: list[list[tuple[int, int, int, int]]]) -> None:
    row_bytes = SIZE * 4
    xor = bytearray()
    for y in range(SIZE - 1, -1, -1):
        for x in range(SIZE):
            r, g, b, a = pixels[y][x]
            xor += bytes((b, g, r, a))
    and_mask = b"\x00" * (((SIZE + 31) // 32) * 4 * SIZE)
    dib_size = 40 + len(xor) + len(and_mask)
    dib = struct.pack(
        "<IIIHHIIIIII",
        40,
        SIZE,
        SIZE * 2,
        1,
        32,
        0,
        len(xor),
        0,
        0,
        0,
        0,
    ) + xor + and_mask
    ico = (
        struct.pack("<HHH", 0, 1, 1)
        + struct.pack("<BBBBHHII", SIZE, SIZE, 0, 0, 1, 32, dib_size, 22)
        + dib
    )
    path.write_bytes(ico)


def app_icon() -> list[list[tuple[int, int, int, int]]]:
    p = blank()
    fill(p, 3, 3, 26, 26, rgba(238, 247, 255))
    fill(p, 5, 5, 22, 22, rgba(45, 120, 212))
    fill(p, 8, 8, 7, 7, rgba(117, 203, 255))
    fill(p, 17, 8, 7, 7, rgba(255, 255, 255))
    fill(p, 8, 17, 7, 7, rgba(255, 255, 255))
    fill(p, 17, 17, 7, 7, rgba(117, 203, 255))
    line(p, 3, 3, 28, 3, rgba(107, 168, 220))
    line(p, 3, 28, 28, 28, rgba(31, 74, 128))
    return p


def uac_icon() -> list[list[tuple[int, int, int, int]]]:
    p = blank()
    fill(p, 5, 3, 22, 26, rgba(255, 255, 255))
    fill(p, 7, 6, 9, 9, rgba(44, 120, 212))
    fill(p, 16, 6, 9, 9, rgba(242, 197, 66))
    fill(p, 7, 15, 9, 9, rgba(242, 197, 66))
    fill(p, 16, 15, 9, 9, rgba(44, 120, 212))
    line(p, 5, 3, 26, 3, rgba(90, 103, 118))
    line(p, 5, 3, 5, 21, rgba(90, 103, 118))
    line(p, 26, 3, 26, 21, rgba(90, 103, 118))
    line(p, 5, 21, 15, 29, rgba(90, 103, 118))
    line(p, 26, 21, 15, 29, rgba(90, 103, 118))
    return p


def driver_icon() -> list[list[tuple[int, int, int, int]]]:
    p = blank()
    fill(p, 6, 7, 20, 18, rgba(238, 247, 255))
    fill(p, 9, 10, 14, 12, rgba(47, 127, 211))
    for x in range(9, 24, 4):
        fill(p, x, 3, 2, 4, rgba(76, 130, 184))
        fill(p, x, 25, 2, 4, rgba(76, 130, 184))
    for y in range(10, 23, 4):
        fill(p, 2, y, 4, 2, rgba(76, 130, 184))
        fill(p, 26, y, 4, 2, rgba(76, 130, 184))
    line(p, 6, 7, 25, 7, rgba(76, 130, 184))
    line(p, 6, 24, 25, 24, rgba(76, 130, 184))
    line(p, 6, 7, 6, 24, rgba(76, 130, 184))
    line(p, 25, 7, 25, 24, rgba(76, 130, 184))
    return p


def main() -> int:
    ICON_DIR.mkdir(parents=True, exist_ok=True)
    write_ico(ICON_DIR / "app.ico", app_icon())
    write_ico(ICON_DIR / "uac.ico", uac_icon())
    write_ico(ICON_DIR / "driver.ico", driver_icon())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
