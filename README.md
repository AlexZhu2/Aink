<p align="center">
  <img src="assets/logo-circuit-ink.png" alt="AInk — Circuit Ink" width="360">
</p>

<p align="center">
  <strong>A pocket e-paper computer on ESP32-S3</strong><br>
  200×200 monochrome display · two-button UX · seven mini-apps · bilingual firmware
</p>

<p align="center">
  <a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a>
</p>

---

## 1. Product overview

**AInk** (*Circuit Ink*) is firmware for a minimalist handheld built around the **Seeed XIAO ESP32-S3 Sense** and a **Waveshare 1.54″** black-and-white e-paper panel. It targets users who want glanceable information and small interactive apps without a phone — and developers who want a complete, hackable reference for LVGL on constrained e-paper hardware.

| Attribute | Specification |
|-----------|----------------|
| Display | 200×200 px, 1-bit, partial refresh |
| Input | 2 buttons (A / B), optional serial simulation |
| Connectivity | WiFi + captive portal provisioning |
| On-device UI | LVGL 8.3, custom 1-bit flush pipeline |
| Locale | English / 中文 (NVS-persisted) |
| Apps | Clock, Weather, AI Vision, Answers, Stocks, Life, Settings |

**Design principles**

- **Calm UI** — tiered refresh (`FAST` / `NAV` / `QUALITY` / `FULL`) balances latency and ghosting.
- **Home vs app** — launcher keeps a 20 px status bar; every app runs **full-screen** for maximum content area.
- **Offline-first where it matters** — Answers and Life work without cloud keys; Weather/Stocks/Vision degrade gracefully.

---

## 2. System architecture

```
┌─────────────────────────────────────────────────────────┐
│  Aink.ino — boot, WiFi portal, refresh scheduler, NTP   │
├─────────────────────────────────────────────────────────┤
│  ui_nav / ui_home — paged 2×2 launcher, routing         │
│  ui_status_bar — date, WiFi, weather peek, battery      │
│  ui_* apps — Clock, Weather, Vision, Answers, …         │
├─────────────────────────────────────────────────────────┤
│  ui_lvgl + epaper_canvas — LVGL 8.3 → 1-bit framebuffer │
├─────────────────────────────────────────────────────────┤
│  Services: weather, stock, vision, voice, camera, NVS   │
└─────────────────────────────────────────────────────────┘
         │ HTTPS / gzip (puff)              │ EPD SPI
         ▼                                  ▼
   QWeather, OpenAI, MiMo, …          Waveshare 1.54″ V2
```

---

## 3. Applications

| App | Capability |
|-----|------------|
| **Clock** | 40 px digit font; optional date line |
| **Weather** | Live conditions, 3-day forecast, humidity / UV / wind / AQI / sunrise / pressure / **PM2.5 & PM10** |
| **AI Vision** | Camera JPEG → cloud vision API → ≤40-character caption |
| **Answers** | Offline Book of Answers (100 entries); photo / voice / random seeds |
| **Stocks** | Up to **6** watchlist symbols; detail view with intraday chart |
| **Life** | Conway’s Game of Life; classic seeds (Pulsar, LWSS, Gosper gun, …) |
| **Settings** | WiFi, QWeather, AI provider & key, display, about |

### Weather data pipeline

1. IP geolocation → QWeather geo lookup (LocationID)
2. `/v7/weather/now`, `/v7/weather/7d`, `/airquality/v1/current/{lat}/{lon}`
3. Auth via `X-QW-Api-Key`; gzip decompressed in-firmware (`puff.c`)
4. City label uses prefecture (`adm2`); forecast excludes today

### Cloud integrations

| Feature | Providers / APIs |
|---------|------------------|
| Weather | [QWeather](https://console.qweather.com) |
| Vision | OpenAI, Gemini, Kimi, MiMo Token Plan |
| Voice | MiMo ASR + chat (Token Plan key) |

Answers and Life require **no API key**.

---

## 4. Interaction model

| Input | Global behavior |
|-------|-----------------|
| **A** click | Next / app action |
| **A** double | Previous |
| **A** long | Back to home |
| **B** click | Open / confirm |
| **B** double | Voice record toggle |

Set `BTN_SERIAL_SIM=1` in `btn_input.h` to map `n` / `p` / `b` / `c` / `v` on serial **115200**.

---

## 5. Hardware & firmware requirements

| Component | Detail |
|-----------|--------|
| MCU | Seeed **XIAO ESP32-S3 Sense** (camera + PDM mic) |
| Panel | [EPD_1in54_V2](https://www.waveshare.com/1.54inch-e-paper.htm) |
| Button A | D6 / GPIO43 |
| Button B | D7 / GPIO44 |
| EPD pins | See `DEV_Config.h` |

| Arduino option | Value |
|----------------|--------|
| Board | Seeed XIAO ESP32S3 Sense |
| PSRAM | OPI PSRAM **Enabled** |
| Partition | Huge APP (3 MB+) |
| LVGL | **8.3.x** |
| ESP32 core | **3.x** |

---

## 6. Developer guide

### 6.1 Quick start

```text
1. Install Arduino IDE 2.x (or arduino-cli) + esp32 board package 3.x
2. Install lvgl 8.3.x
3. Open Aink.ino (this directory = sketch root)
4. Upload → serial monitor 115200
```

First boot without saved WiFi enters **captive portal** (QR / AP) for WiFi, weather key, and optional AI key.

### 6.2 Boot splash

```powershell
python tools/bmp_to_boot_splash.py path\to\splash.bmp --out boot_splash_image.h
```

Requires 200×200 1-bit BMP.

### 6.3 Vision API smoke test (PC)

```powershell
cd Aink
$env:MIMO_API_KEY = "your-key"
python tools/test_vision_api.py --provider mimo --image photo.jpg
```

### 6.4 Fonts & assets

```bash
python tools/build_cn_symbols.py
python tools/build_fonts.py          # Node npx required
python tools/build_clock_font.py
python tools/svg_to_weather_icons.py
python tools/png_to_tile_icons.py    # gear.png / eye.png in sketch root (local)
```

CJK: ~3500 chars in `tools/cn_font_symbols.txt` → `aink_3500_12.c` / `aink_3500_14.c`.  
Manual path: [LVGL Font Converter](https://lvgl.io/tools/fontconverter) (LVGL 8.x, bpp 1). Strip `.static_bitmap = 0` if present.

### 6.5 Source map

| Module | Role |
|--------|------|
| `Aink.ino` | Boot, portal, refresh orchestration |
| `epaper_canvas.*` | Framebuffer, rotation, async upload |
| `ui_nav / ui_home` | Launcher + routing |
| `ui_status_bar.*` | Status bar (home only) |
| `ui_lvgl.*` | LVGL init; screen vs fullscreen |
| `ui_*` | Per-app UI |
| `*_service.*` | Weather, stock, vision, voice backends |
| `settings_api.*` | NVS configuration |
| `app_locale.*` | EN/ZH strings |
| `puff.c` | Gzip inflate |
| `tools/` | Build & test scripts |

---

## 7. Operations & troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| Chinese □ boxes | LVGL 8.3.x; missing glyphs in `cn_font_symbols.txt`; remove `.static_bitmap` from font `.c` |
| Camera / PSRAM fail | Enable OPI PSRAM; Huge APP; Sense board with camera |
| Weather 401/403 | API Host / Key mismatch; plan lacks Air Quality |
| PM2.5 / PM10 `--` | No pollutant data for coordinates |
| `undefined reference to puff` | Clean build; ensure `puff.c` in sketch |
| Vision HTTP -1 | WiFi + PSRAM; validate with `test_vision_api.py` |

Serial tags: `[Weather]`, `[Vision]`, `[Stock]`, `[Life]`, `[LVGL]`.

---

## 8. License

MIT — see `LICENSE`. Waveshare EPD driver files retain their original header terms.

---

<p align="center">
  <sub>AInk · Circuit Ink · firmware v0.1</sub>
</p>
