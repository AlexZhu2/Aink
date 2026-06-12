#!/usr/bin/env python3
"""Rasterize tile PNG icons into 1-bit C bitmask headers for ESP32."""

from __future__ import annotations

import os
import sys

try:
    from PIL import Image
except ImportError:
    print("Install deps: pip install pillow", file=sys.stderr)
    sys.exit(1)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
OUTPUT_PATH = os.path.join(PROJECT_DIR, "settings_icons.h")

ICON_SIZE = 32
RASTER_SIZE = 512
THRESHOLD = 200

ICON_FILES = [
    ("settings_gear_bitmap", "gear.png", "Gear outline (settings tile)."),
    ("vision_eye_bitmap", "eye.png", "Eye outline (AI vision tile)."),
]


def load_binary(path: str) -> Image.Image:
    img = Image.open(path).convert("RGBA")
    bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
    img = Image.alpha_composite(bg, img)
    gray = img.convert("L")
    return gray.point(lambda p: 0 if p < THRESHOLD else 255, "1")


def downsample(img: Image.Image, size: int) -> Image.Image:
    src_w, src_h = img.size
    out = Image.new("1", (size, size), 1)
    for y in range(size):
        y0 = y * src_h // size
        y1 = max(y0 + 1, (y + 1) * src_h // size)
        for x in range(size):
            x0 = x * src_w // size
            x1 = max(x0 + 1, (x + 1) * src_w // size)
            block = img.crop((x0, y0, x1, y1))
            if block.getextrema()[0] == 0:
                out.putpixel((x, y), 0)
    return out


def to_outline(img: Image.Image) -> Image.Image:
    w, h = img.size
    out = Image.new("1", (w, h), 1)
    for y in range(h):
        for x in range(w):
            if img.getpixel((x, y)) != 0:
                continue
            for dx, dy in (
                (-1, 0),
                (1, 0),
                (0, -1),
                (0, 1),
                (-1, -1),
                (1, -1),
                (-1, 1),
                (1, 1),
            ):
                nx, ny = x + dx, y + dy
                if nx < 0 or ny < 0 or nx >= w or ny >= h or img.getpixel((nx, ny)) == 1:
                    out.putpixel((x, y), 0)
                    break
    return out


def row_masks(img: Image.Image) -> list[int]:
    size = img.size[0]
    rows: list[int] = []
    for y in range(size):
        mask = 0
        for x in range(size):
            if img.getpixel((x, y)) == 0:
                mask |= 1 << (size - 1 - x)
        rows.append(mask)
    return rows


def preview(rows: list[int], size: int) -> None:
    for mask in rows:
        line = "".join("#" if (mask >> (size - 1 - x)) & 1 else "." for x in range(size))
        print(line)


def main() -> None:
    lines: list[str] = [
        "#ifndef SETTINGS_ICONS_H",
        "#define SETTINGS_ICONS_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define SETTINGS_ICON_SIZE {ICON_SIZE}",
        "",
    ]

    for var_name, filename, comment in ICON_FILES:
        png_path = os.path.join(PROJECT_DIR, filename)
        if not os.path.isfile(png_path):
            print(f"Missing {png_path}", file=sys.stderr)
            sys.exit(1)

        src = load_binary(png_path)
        w, h = src.size
        side = min(w, h)
        left = (w - side) // 2
        top = (h - side) // 2
        cropped = src.crop((left, top, left + side, top + side))
        scaled = cropped.resize((RASTER_SIZE, RASTER_SIZE), Image.Resampling.LANCZOS)
        scaled = scaled.point(lambda p: 0 if p < THRESHOLD else 255, "1")
        icon = to_outline(downsample(scaled, ICON_SIZE))
        masks = row_masks(icon)

        print(f"\n{filename}:")
        preview(masks, ICON_SIZE)

        lines.append(f"/** {comment} Source: {filename} */")
        lines.append(f"static const uint32_t {var_name}[SETTINGS_ICON_SIZE] = {{")
        for mask in masks:
            lines.append(f"  0x{mask:08X}u,")
        lines.append("};")
        lines.append("")

    lines.append("#endif")
    lines.append("")

    with open(OUTPUT_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

    print(f"\nWrote {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
