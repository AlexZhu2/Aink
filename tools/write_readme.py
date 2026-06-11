#!/usr/bin/env python3
"""Write README.md with UTF-8 encoding."""
from pathlib import Path

README = """# Aink

Aink is a 2x2 app platform for **Seeed XIAO ESP32-S3** + **Waveshare 1.54 inch B&W e-paper** (200x200). It ships with WiFi provisioning, LVGL UI, button navigation, tiered e-paper refresh, and pluggable mini-apps.

## Hardware

| Part | Notes |
|------|-------|
| MCU | Seeed Studio XIAO ESP32-S3 |
| Display | Waveshare [EPD_1in54_V2](https://www.waveshare.com/1.54inch-e-paper.htm) (200x200) |
| Button A | D6 / GPIO43 (active LOW, internal pull-up) |
| Button B | D7 / GPIO44 (active LOW, internal pull-up) |

See `DEV_Config.h` for EPD wiring (DIN D10, CLK D8, CS D9, DC D3, RST D1, BUSY D0).

## Features

### Platform

- WiFi captive portal + QR setup; credentials stored in NVS
- Status bar (hand-drawn 8x16 ASCII on the physical right edge)
- 2x2 launcher: A = move focus, B = open app, A long = back
- LVGL 8.3 main UI (200x180) + 1-bit e-paper flush
- Refresh modes: FAST / NAV / QUALITY / FULL
- NTP (UTC+8)

### Apps

| Tile | Status |
|------|--------|
| Weather | Forecast, metrics, AQI, 3-day outlook |
| App 2 / 3 | Placeholder |
| Settings | WiFi, Model, Display, About; EN/ZH language toggle |

### i18n

- Default language: **English**
- Switch: **Settings -> Display -> Language** (press B to toggle)
- Preference saved in NVS (`lang` key)
- UI fonts: `aink_12.c` / `aink_14.c` (Noto Sans SC subset + ASCII)

## Quick start

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software) or arduino-cli
2. Board package: **esp32 by Espressif** (3.x), board **XIAO ESP32S3**
3. Library: **lvgl 8.3.x** (not 9.x)
4. Open `Aink.ino` (this folder is the sketch root)
5. Upload; serial monitor **115200**

First boot without saved WiFi enters AP portal mode automatically.

### Serial button simulation

With `BTN_SERIAL_SIM=1` in `btn_input.h`:

| Key | Action |
|-----|--------|
| n | A next |
| p | A prev |
| b | A long (back) |
| c | B confirm |
| v | B double (voice stub) |
| h | help |

## Chinese fonts

Regenerate fonts with [LVGL Font Converter](https://lvgl.io/tools/fontconverter) (**LVGL 8.x**):

- Range: `0x20-0x7F`
- Symbols: see `tools/cn_font_symbols.txt`
- Output: `aink_12.c`, `aink_14.c` (remove `.static_bitmap` if converter emits LVGL 9 fields)

```bash
python tools/build_cn_symbols.py
python tools/svg_to_weather_icons.py
```

## Project layout

```
Aink.ino              Boot, WiFi portal, status bar, refresh orchestration
epaper_canvas.*       Framebuffer, rotation, EPD upload
ui_home / ui_nav      Launcher and navigation
ui_weather.*          Weather app
ui_settings.*         Settings app (multi-level menu)
app_locale.*          EN/ZH strings
settings_api.*        NVS helpers (WiFi, language, portal flag)
weather_service.*     Open-Meteo + air quality + geo
aink_12.c / aink_14.c LVGL CJK fonts
tools/                Icon/font tooling
```

## Notes for contributors

- Do not break status-bar orientation or portal-only horizontal mirror (`epaper_canvas.cpp`).
- E-paper is 1-bit: LVGL grays are thresholded in flush; avoid large gray fills.
- Open-Meteo / ip-api are used without API keys; city names may need extra font glyphs for Chinese locales.

## License

MIT — see `LICENSE`. Waveshare EPD driver files retain their original header terms.
"""


def main() -> None:
    path = Path(__file__).resolve().parent.parent / "README.md"
    path.write_text(README, encoding="utf-8", newline="\n")
    print(f"Wrote {path}")


if __name__ == "__main__":
    main()
