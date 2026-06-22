# Aink

Firmware for a pocket e-paper gadget: **Seeed XIAO ESP32-S3 Sense** + **Waveshare 1.54″ B&W EPD (200×200)**. LVGL UI, two-button navigation, WiFi captive portal, and seven mini-apps on a paged launcher.

## Hardware

| Part | Notes |
|------|--------|
| MCU | Seeed **XIAO ESP32-S3 Sense** (camera + PDM mic for Vision / Answers / Voice) |
| Display | Waveshare [EPD_1in54_V2](https://www.waveshare.com/1.54inch-e-paper.htm) (200×200) |
| Button A | D6 / GPIO43 (active LOW, internal pull-up) |
| Button B | D7 / GPIO44 (active LOW, internal pull-up) |

EPD wiring: see `DEV_Config.h` (DIN D10, CLK D8, CS D9, DC D3, RST D1, BUSY D0).

### Arduino settings

| Option | Value |
|--------|--------|
| Board | **Seeed XIAO ESP32S3 Sense** |
| PSRAM | **OPI PSRAM → Enabled** |
| Partition | **Huge APP (3MB+)** |
| LVGL | **8.3.x** (not 9.x) |
| ESP32 core | **3.x** |

Boot log should show non-zero `freePsram` and, after opening Vision, `[Camera] ready`.

## UI model

- **Home**: 2×2 tile launcher (7 apps, paginated) + **20px status bar** (date, WiFi, weather icon/temp, battery).
- **Apps**: **Full-screen 200×200** — status bar hidden while inside any app.
- **Refresh**: tiered e-paper modes — `FAST` / `NAV` / `QUALITY` / `FULL`.
- **Language**: English or 中文 (`Settings → Display → Language`, stored in NVS).

## Controls

| Input | Action |
|-------|--------|
| **A** click | Next focus (home) / app-specific |
| **A** double | Previous focus |
| **A** long | Back to home |
| **B** click | Open app / confirm |
| **B** double | Voice record toggle (global) |

With `BTN_SERIAL_SIM=1` in `btn_input.h`: `n` / `p` / `b` / `c` / `v` simulate A/B (see serial help on boot).

## Apps

| Tile | Summary |
|------|---------|
| **Clock** | Large time (40px font), optional date |
| **Weather** | Current conditions, 3-day forecast, humidity / UV / wind / AQI / sunrise / pressure / **PM2.5 & PM10** |
| **AI Vision** | Camera capture → cloud vision API → short poetic caption (≤40 CJK chars) |
| **Answers** | Offline Book of Answers — photo, voice, or random oracle modes |
| **Stocks** | Up to **6** watchlist rows; **B** opens detail (price, change, intraday chart) |
| **Life** | Conway’s Game of Life — pattern menu (Random, Pulsar, Spaceship, Oscillator, R-pent, Gosper gun) |
| **Settings** | WiFi, QWeather, AI provider/key, display, about |

### Weather (QWeather / 和风)

1. Register at [console.qweather.com](https://console.qweather.com) — copy **API Key** and project **API Host** (no `https://` prefix).
2. Configure via captive portal or **Settings → WiFi → Configure Weather**.
3. Fetch chain: `ip-api.com` (geo) → QWeather geo lookup → `weather/now` + `weather/7d` + `airquality/v1/current/{lat}/{lon}` (+ UV index fallback).
4. Auth: header **`X-QW-Api-Key`**; responses are **gzip** — firmware decompresses via embedded `puff.c`.
5. Detail page shows **city** (`adm2`). Forecast row = tomorrow + next 2 days. Refreshes ~every 30 min on WiFi.

### AI Vision

Providers: **OpenAI**, **Gemini**, **Kimi**, **MiMo Token Plan**. Configure in portal or **Settings → Model**. Camera pauses during HTTPS to reduce frame-buffer warnings.

### Answers

Fully offline. **A** cycles photo / voice / auto mode; **B** draws from 100 built-in entries. Photo mode uses JPEG bytes as a local seed only (no upload).

### Voice

**B** double: record → MiMo ASR → MiMo chat reply. **A** during playback interrupts TTS state. Requires **MiMo Token Plan** key in Settings.

### Stocks

Custom comma-separated watchlist in Settings or portal. List auto-refreshes every 5 min. Detail uses bold price font with `￥` / `$` and 5-minute intraday chart.

## Quick start

1. Arduino IDE 2.x or arduino-cli; install **esp32** board package 3.x.
2. Install **lvgl 8.3.x**.
3. Open `Aink.ino` (this folder is the sketch root).
4. Upload; serial monitor **115200**.

First boot without WiFi → captive portal AP. Scan QR or join AP to configure WiFi, weather key, and optional AI key.

### Boot splash

Replace with a 200×200 1-bit BMP:

```powershell
python tools/bmp_to_boot_splash.py path\to\splash.bmp --out boot_splash_image.h
```

### Test vision API on PC

```powershell
cd Aink
$env:MIMO_API_KEY = "your-key"
python tools/test_vision_api.py --provider mimo --image photo.jpg
```

## Fonts & icons

```bash
python tools/build_cn_symbols.py   # regenerate cn_font_symbols.txt
python tools/build_fonts.py        # aink_3500_12.c / aink_3500_14.c (needs Node npx)
python tools/build_clock_font.py   # aink_clock_40.c (digits + ￥ $)
python tools/svg_to_weather_icons.py
python tools/png_to_tile_icons.py  # needs gear.png / eye.png in sketch root (gitignored)
```

CJK fonts: ~3500 common chars + UI strings (`tools/cn_font_symbols.txt`). Manual fallback: [LVGL Font Converter](https://lvgl.io/tools/fontconverter) LVGL **8.x**, bpp **1**. Remove `.static_bitmap = 0` if the converter adds it (LVGL 9 artifact).

## Project layout

```
Aink.ino              Boot, WiFi portal, refresh orchestration
epaper_canvas.*       Framebuffer, rotation, async EPD upload
ui_home / ui_nav      Paged launcher + routing
ui_status_bar.*       Top bar (home only)
ui_lvgl.*             LVGL init, screen vs fullscreen helpers
ui_clock.*            Clock app
ui_weather.*          Weather UI
ui_stock.*            Stock list (6 rows)
ui_stock_detail.*   Quote detail + chart
ui_life.*             Conway Life game
ui_vision.*           AI Vision UI
ui_answers.*          Book of Answers
ui_voice.*            Voice interaction UI
ui_settings.*         Settings menu
weather_service.*     QWeather fetch + gzip + AQI/PM parse
stock_service.*       Watchlist quotes + intraday
vision_service.*      Camera → HTTPS vision APIs
voice_service.*       PDM mic → ASR → LLM
camera_service.*      XIAO Sense camera (240×240 JPEG)
settings_api.*        NVS preferences
app_locale.*          EN/ZH strings
boot_splash.*         Startup splash
puff.c                Embedded deflate (gzip)
tools/                Build / test scripts
```

## Troubleshooting

| Symptom | Check |
|---------|--------|
| Chinese shows □ | LVGL **8.3.x**; font `.c` has no `.static_bitmap`; chars in `cn_font_symbols.txt` |
| Camera / PSRAM errors | **OPI PSRAM Enabled**; Huge APP partition; Sense board |
| Weather HTTP / 401 / 403 | Correct API Host + Key; plan includes Air Quality API |
| PM2.5 / PM10 shows `--` | Air quality endpoint returned no pollutant data for location |
| `[Weather] gzip` / `undefined reference to puff` | Reflash with `puff.c` + `weather_gzip.cpp`; clean build |
| Vision HTTP -1 | WiFi + PSRAM; validate key with `tools/test_vision_api.py` |
| Stock detail ¥ wrong | Regenerate `aink_clock_40.c` via `tools/build_clock_font.py` |

## License

MIT — see `LICENSE`. Waveshare EPD driver files retain their original header terms.
