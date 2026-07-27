from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "assets" / "boot.bmp"
WIDTH = 400
HEIGHT = 225


def build_image() -> Image.Image:
    image = Image.new("RGB", (WIDTH, HEIGHT), (5, 13, 30))

    glow_layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow_layer)
    glow_draw.rounded_rectangle((160, 49, 240, 115), radius=14, fill=(40, 179, 255, 80))
    glow_layer = glow_layer.filter(ImageFilter.GaussianBlur(18))
    image = Image.alpha_composite(image.convert("RGBA"), glow_layer)

    draw = ImageDraw.Draw(image)
    mark = [
        (160, 115),
        (160, 55),
        (176, 55),
        (200, 90),
        (224, 55),
        (240, 55),
        (240, 115),
        (225, 115),
        (225, 84),
        (205, 112),
        (195, 112),
        (175, 84),
        (175, 115),
    ]
    draw.polygon(mark, fill=(57, 195, 255, 255))
    draw.polygon(
        [(176, 55), (185, 55), (200, 77), (215, 55), (224, 55), (200, 90)],
        fill=(164, 235, 255, 255),
    )

    return image.convert("RGB")


def main() -> None:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    build_image().save(OUTPUT, format="BMP")
    print(f"boot image: {OUTPUT} ({WIDTH}x{HEIGHT})")


if __name__ == "__main__":
    main()
