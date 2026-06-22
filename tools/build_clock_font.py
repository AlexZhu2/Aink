#!/usr/bin/env python3
"""Regenerate aink_clock_40.c (JetBrains Mono ExtraBold + bold ￥)."""

from __future__ import annotations

import re
import shutil
import subprocess
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = Path(__file__).resolve().parent
FONTS = TOOLS / "fonts"
OUT = ROOT / "aink_clock_40.c"

JETBRAINS_URL = (
    "https://github.com/JetBrains/JetBrainsMono/raw/master/fonts/ttf/"
    "JetBrainsMono-ExtraBold.ttf"
)
JETBRAINS_TTF = FONTS / "JetBrainsMono-ExtraBold.ttf"
NOTO_VAR = FONTS / "NotoSansSC-VariableFont_wght.ttf"
NOTO_XBOLD = FONTS / "NotoSansSC-ExtraBold.ttf"

LVGL_INCLUDE_BLOCK = """\
#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif
"""

BROKEN_INCLUDE = """\
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif
"""


def ensure_jetbrains_font() -> Path:
    if JETBRAINS_TTF.is_file() and JETBRAINS_TTF.stat().st_size > 10_000:
        return JETBRAINS_TTF
    print(f"Downloading {JETBRAINS_TTF.name}...")
    FONTS.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(JETBRAINS_URL, JETBRAINS_TTF)
    return JETBRAINS_TTF


def ensure_noto_extrabold() -> Path:
    if NOTO_XBOLD.is_file() and NOTO_XBOLD.stat().st_size > 1_000_000:
        return NOTO_XBOLD
    if not NOTO_VAR.is_file():
        raise SystemExit(f"Missing font: {NOTO_VAR}")

    try:
        from fontTools.ttLib import TTFont
        from fontTools.varLib.instancer import instantiateVariableFont
    except ImportError as exc:
        raise SystemExit(
            "fonttools is required to build bold ￥: pip install fonttools"
        ) from exc

    print(f"Building {NOTO_XBOLD.name} (wght=800)...")
    font = TTFont(NOTO_VAR)
    inst = instantiateVariableFont(font, {"wght": 800})
    inst.save(NOTO_XBOLD)
    return NOTO_XBOLD


def patch_lvgl_include(text: str) -> str:
    if LVGL_INCLUDE_BLOCK.strip() not in text:
        text = re.sub(
            r"#ifdef LV_LVGL_H_INCLUDE_SIMPLE\r?\n#include \"lvgl.h\"\r?\n#else\r?\n\s*#include \"lvgl/lvgl.h\"\r?\n#endif",
            LVGL_INCLUDE_BLOCK.strip(),
            text,
            count=1,
        )
        if BROKEN_INCLUDE in text:
            text = text.replace(BROKEN_INCLUDE, LVGL_INCLUDE_BLOCK, 1)
    text = re.sub(r"\n\s*\.static_bitmap = 0,\n", "\n", text)
    return text


def main() -> None:
    jetbrains = ensure_jetbrains_font()
    noto_xbold = ensure_noto_extrabold()

    npx = shutil.which("npx") or shutil.which("npx.cmd")
    if npx is None:
        raise SystemExit("npx not found")

    cmd = [
        npx,
        "--yes",
        "lv_font_conv",
        "--bpp",
        "1",
        "--size",
        "40",
        "--no-compress",
        "--font",
        str(jetbrains),
        "--symbols",
        "0123456789:- AMP.$",
        "--range",
        "32,45,46,48-58,65,77,80,36",
        "--font",
        str(noto_xbold),
        "-r",
        "0xFFE5",
        "--format",
        "lvgl",
        "-o",
        str(OUT),
    ]
    print("Building aink_clock_40.c (JetBrains ExtraBold + Noto ￥)...")
    subprocess.run(cmd, check=True, cwd=ROOT)

    text = OUT.read_text(encoding="utf-8")
    text = patch_lvgl_include(text)
    text = text.replace("AINK_CLOCK_40_NEW", "AINK_CLOCK_40")
    text = text.replace("aink_clock_40_new", "aink_clock_40")
    if ".fallback = &aink_3500_12," not in text:
        text = text.replace(".fallback = NULL,", ".fallback = &aink_3500_12,", 1)
    if "extern const lv_font_t aink_3500_12;" not in text:
        text = text.replace(
            "/*Initialize a public general font descriptor*/",
            "extern const lv_font_t aink_3500_12;\n\n/*Initialize a public general font descriptor*/",
            1,
        )
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"Wrote {OUT}")


if __name__ == "__main__":
    main()
