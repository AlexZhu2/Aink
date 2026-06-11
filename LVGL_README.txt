# LVGL setup (Phase 1)

Install the **lvgl** library in Arduino IDE / arduino-cli:

- Library Manager: search **lvgl**, install **8.3.x** (recommended for this project).
- Keep `lv_conf.h` in the `Aink` sketch folder (already included).
- `ui_lvgl.cpp` defines `LV_CONF_INCLUDE_SIMPLE` so this lv_conf.h is used.

If compile errors mention `lv_disp_drv_t`, you likely have LVGL 9.x — install LVGL **8.3.11**.

After upload, open Serial Monitor (115200). With no hardware buttons, use serial simulation (BTN_SERIAL_SIM=1 in btn_input.h):

  n = A click (next)     p = A double (prev)     b = A long (back)
  c = B click (confirm)  v = B double (voice)  h = help

Set BTN_SERIAL_SIM to 0 after wiring D6/D7.

## Phase 1 features

- 2×2 HOME grid (Weather + 3 placeholders)
- Status bar stays hand-drawn on the right
- Buttons on **D6/GPIO43 (A)** and **D7/GPIO44 (B)**:
  - A click = next, A double = prev, A long = back
  - B click = confirm, B double = voice toggle (stub log)

Regenerate weather icons from SVG:

```bash
python Aink/tools/svg_to_weather_icons.py
```

## Arduino board settings (XIAO ESP32S3 Sense + camera)

- Board: **Seeed XIAO ESP32S3 Sense** (must include camera module)
- **PSRAM: OPI PSRAM enabled** (required for camera + LVGL)
- Partition: **Huge APP (3MB+)** or custom APP ≥ 3MB
- esp-arduino **3.0.x** recommended for camera driver

Boot log should show `[Camera] ready (240x240 JPEG)`. If you see
`frame buffer malloc failed`, enable PSRAM or use a Sense board with camera.
